# Script entities as `master` in Sven Co-op: analysis notes

AI-generated report. Written by an AI assistant from a working session with a human who ran the in-game tests and the IDA sessions. The assistant never ran the game or the plugin. Nothing here has been independently reviewed. Treat every claim as "reported", and use the checks in the last section to confirm it on your own build.

This document describes how it was done and what was observed. It is not an installation guide and not a request to adopt anything.

---

### TL;DR
An AngelScript entity (`ScriptBaseEntity`) can set `FCAP_MASTER` in `ObjectCaps()`, but a native entity whose `master` points at it is never gated: the engine-side `IsTriggered` slot of the script wrapper is a native stub that always returns true.

**Cause (from decompiled `server.dll`)**: `ObjectCaps()` (vtable slot 9) is dispatched into AngelScript, `IsTriggered()` (slot 27) is not.

**Workaround; an earlier revision was tested on a live server**: a small Metamod plugin (`as_master_shim`) replaces slot 27 in the wrapper vtables with a function that reads an `int` from `entvars_t` (`pev->iuser3`) that the script writes.

**Scope**: Windows x86 `server.dll`, one build, one tester. Linux not done. Side effects are not fully characterised.

---

### 1. Symptom
`env_state` (a Spirit of Half-Life entity ported to AngelScript) has `FCAP_MASTER` and a script-side `IsTriggered()` returning its state. Used as `master` of a `func_door`, the door behaves as if no master was set. Per a message from the port's author, it cannot be used as a source/master.

---

### 2. How the cause was located
Static analysis of `server.dll` in IDA (Hex-Rays). Steps in order:
1. String search for `Master entity '%s' can't be used as a master.` and `Master entity '%s' not found.`. Cross-reference leads to one function: the native master check (below).
2. In that function, two virtual calls on the found entity were identified by byte offset: `+36` (slot 9) and `+108` (slot 27).
3. Script-visible API registration was read for confirmation: `int ObjectCaps()` and `bool IsTriggered(CBaseEntity@ pActivator)` are both registered as methods the script can call; their thunks forward to `vtable+36` and `vtable+108`. (This is the script-to-native direction only; it says nothing about native-to-script dispatch.)
4. `FCAP_MASTER` is registered in the script constants as `128`; the native `CMultiSource::ObjectCaps` returns `128`.
5. Vtables of the script wrapper classes were found through MSVC RTTI (type descriptors named like `CBaseCustomBaseEntity<CBaseEntity>`, `CBaseCustomBaseAnimating<CBaseAnimating>`, `CBaseCustomMonsterEntity<CBaseMonster>`, and so on). A script that dumped slots 0 to 47 and decompiled each slot was used. A slot was labelled "AS dispatch" when its decompiled body referenced a method-declaration string (for example `"int ObjectCaps()"`) and the bridge call. This labelling is a heuristic.

#### 2.1 Native master check (decompiled, names are descriptive)
```c
char UTIL_IsMasterTriggered(name, activator)
{
  if (!name) return 1;
  ent = FIND_ENTITY_BY_STRING(0, "targetname", name);
  if (ent && valid(ent))
  {
    p = ent->pvPrivateData;
    if (!p || (char)p->vtable[9]() >= 0)          // ObjectCaps, bit 0x80 required
    {
      Msg("Master entity '%s' can't be used as a master.\n");
      return 1;
    }
    return p->vtable[27](activator);              // IsTriggered
  }
  Msg("Master entity '%s' not found.\n");
  return 1;
}
```
Every failure path returns 1 ("passed"), so a broken master is silent.

#### 2.2 Native reference, `CMultiSource`
- **slot 9**: returns `128`.
- **slot 27**: returns false if `spawnflags & 1`, false if any stored trigger flag is zero, else checks globalstate when set, else true.

#### 2.3 Script wrapper, primary vtable of `CBaseCustomBaseEntity<CBaseEntity>`
```text
slot  9 (+36)   ObjectCaps   AS dispatch; returns 2 when the script has no ObjectCaps()
slot 27 (+108)  IsTriggered  native stub: mov al,1 / ret 4  (decompiled as "return 1")
slots 28-30                  one shared native stub (returns 0)
```
AS-dispatched slots seen in that table: 4 `KeyValue`, 9 `ObjectCaps`, 21 `TakeDamage`, 22 `TakeHealth`, 23 `TakeArmor`, 25 `BloodColor`, 46 `OnControls`. Slot 27 is not among them.

Slot 9 dispatch and the slot 27 stub were confirmed by decompilation for the primary vtables of the Entity, Animating and Monster wrappers. For the other wrappers only the runtime guard result is known (section 4).

Wrapper classes also have secondary vtables (`ICustomEntity` adjustor thunks) where slot indices mean something different. They must not be patched by slot number.

---

### 3. What the workaround does
A Metamod plugin (`as_master_shim`), x86 Windows only:
1. Finds the vtable of each wrapper class at load time through MSVC RTTI under Structured Exception Handling (SEH): locate the type-descriptor name string, find the complete-object-locator that references it with offset 0, find the vtable whose slot `[-1]` points at that locator.
2. Guards before patching: slot 27 bytes must equal `B0 01 C2 04 00` (`mov al, 1 / ret 4`), and slots 28, 29, 30 must share one pointer.
   *On the tested build the guard accepted 7 of 8 tables, which also confirms the stub byte pattern against the actual memory contents.*
3. Overwrites slot 27 with a hook function that:
   - calls slot 9 and returns true if bit `0x80` is not set (old behaviour kept for non-masters);
   - otherwise reads `pev->iuser3` and returns a function of it.
4. Reads `pev` at `this+4`, the same way the native `CMultiSource::IsTriggered` does (`spawnflags` is at `pev+416` in that function).

#### 3.1 Script side
The script writes state to `entvars_t::iuser3`. Its offset on the tested build (588) was inferred from standard HLSDK layout and confirmed by live behaviour.

To prevent desynchronisation, state is updated strictly through one centralized setter:
```angelscript
void SetState(int iState)
{
    m_iState = iState;
    pev.iuser3 = iState + 1; // 0 -> 1 (OFF), 1 -> 2 (ON)
}
```

#### 3.2 Hook semantics (Revision B; startup log observed, door behaviour on this revision not yet reported)
Opt-in encoding: `return iuser3 != 1;`:
- `0`: unmanaged / default (old behaviour: master passes).
- `1`: master is OFF -> returns `false` (locks native entities).
- `2`: master is ON -> returns `true` (unlocks native entities).

*Why*: Revision A (`iuser3 != 0`) would permanently lock doors referencing any script entity that sets `FCAP_MASTER` (or `FCAP_FORCE_TRANSITION`, also `128`) but never writes the field. Revision B makes participation completely opt-in.

---

### 4. What was observed (by the human tester, reported)
1. **Startup vtable patching**:
   On the test server, the safety guards hooked **7 out of 8 candidate vtables**:
   - `CBaseCustomBaseEntity` (hooked)
   - `CBaseCustomBaseAnimating` (hooked)
   - `CBaseCustomTankEntity` (hooked)
   - `CBaseCustomPlayerItemEntity` (hooked)
   - `CBaseCustomPlayerWeaponEntity` (hooked)
   - `CBaseCustomItemEntity` (hooked)
   - `CBaseCustomPlayerAmmoEntity` (hooked)
   - `CBaseCustomMonsterEntity`: skipped by the slot 27 guard (the log does not say whether the stub-byte check or the slots 28-30 check failed).
2. **Door gating**:
   Tested on revision A (`iuser3 != 0`): With the hook active, a `func_door` with `master` set to the script entity was locked while the entity was off and unlocked while it was on, following a toggle button, in both directions.
   Before the hook: master had no effect.
3. **Field writing**:
   Writing the field with the admin plugin's `.ent_keyvaluename st iuser3 <n>` printed a success message, but a following read returned the old value. Writing from the script worked, and the same command read the script-written value correctly. Cause unknown.
4. **Console assertions**:
   The console flooded with `SteamEngine.*` "already registered" assertions from the Steam client when starting a local map. Unrelated to the plugin.
5. **Door movement**:
   A door jitter was seen with and without the master key. Not attributed to the plugin.

---

### 5. Known limits and side effects
1. The hook calls the script's `ObjectCaps()` once more on every check. A script whose `ObjectCaps()` calls `IsTriggered()` could recurse.
2. Native use of `iuser3` by non-player entities was not checked. The newest changelog mentions native player use of `iuser4`.
3. Behaviour with other Metamod plugins that patch the same vtables is unknown.
4. Runtime unloading is disabled (`PT_NEVER`) to prevent dangling vtable pointers if the game DLL is unmapped.
5. While 7 vtables were patched, master behaviour has only been actively tested on `ScriptBaseEntity` (`CBaseCustomBaseEntity`). Master gating on Tank, Item, and other wrappers remains unverified.
6. All addresses in this report belong to one build:
   ```text
   Protocol version 48
   Engine version 5.0.1.8 (svencoop)
   Engine build: 10:08:09 Nov 23 2024 (10257)
   ```
   The game release number reported by the tester for this installation is 5.26. File size, date and SHA-256 of `server.dll` were not recorded.
7. Newer builds were not checked.

---

### 6. Notes for a Linux port (not attempted)
Differences expected from the Itanium ABI (unverified against a `server.so`):
- **Vtable layout**: two words (offset-to-top, typeinfo pointer) precede function pointers. Find vtables through `_ZTV` symbols or typeinfo name strings.
- **Virtual destructors**: take two slots, so slot indices after a destructor may shift by one. Do not reuse 9 and 27 blindly.
- **Calling convention**: cdecl with `this` as the first stack argument (`__fastcall` does not apply).
- **Memory protection**: `mprotect` on page-aligned `.data.rel.ro` instead of `VirtualProtect` on `.rdata`.
- The repository's non-Windows branch is a stub that patches nothing.

---

### 7. How to check these claims yourself
1. Search the strings in section 2 step 1, follow the cross-reference, confirm the two virtual calls and the "return 1 on failure" behaviour.
2. Dump wrapper vtables through RTTI and decompile slots 9 and 27. Expected: slot 9 dispatches `"int ObjectCaps()"`, slot 27 is a trivial stub.
3. Read the stub bytes in a hex view and compare with `B0 01 C2 04 00`.
4. Run the toggle-button test: entity off, door locked; entity on, door open; and control cases: native `multisource` as master unchanged, script entity with `FCAP_MASTER` leaving `iuser3 == 0` keeps old behaviour (revision B).

---

### 8. Open questions
1. Why does the admin plugin's key-write command not change the `iuser3` field while the script-side write does?
2. Do any native code paths use `iuser3` of script entities?
3. Which part of the guard rejected `CBaseCustomMonsterEntity`: the stub bytes, or slots 28-30 not sharing one pointer?
4. Is there an intended reason slot 27 is not dispatched to AngelScript?
