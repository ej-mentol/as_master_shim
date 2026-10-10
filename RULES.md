# Project Rules: `as_master_shim` Metamod Plugin

## 1. Status
**Experimental.** This plugin was designed through static analysis of Sven Co-op's `server.dll` and built with AI assistance. It works exclusively on the tested 32-bit Windows binary build specified in Section 3. It replaces a single virtual table slot (`IsTriggered`, slot 27) at runtime. It is **not** an official fix and is used at the user's discretion and risk.

---

## 2. Contract (Script <-> Plugin)
To avoid interfering with existing script entities or unrelated entities sharing bit `0x80`, the plugin uses an opt-in communication contract through `pev->iuser3`:

| `pev.iuser3` | State / Meaning | Hook Return Value (`UTIL_IsMasterTriggered`) | Behaviour |
| :---: | :---: | :---: | :---: |
| **`0`** | Unmanaged / Default | `true` | Preserves legacy game behaviour (master passes) |
| **`1`** | Master is **OFF** | `false` | Master is closed / locks native entities |
| **`2`** | Master is **ON** | `true` | Master is open / triggers native entities |

### Implementation Rules:
1. **Evaluation Rule**: The hook strictly evaluates: `return (pev->iuser3 != 1);`.
2. **Single State Setter**: Script entities MUST modify `m_iState` and `pev.iuser3` via a single centralized setter method (`SetState(int newState)`):
   ```angelscript
   void SetState(int iState)
   {
       m_iState = iState;
       pev.iuser3 = iState + 1; // 0 -> 1 (OFF), 1 -> 2 (ON)
   }
   ```
   No other method or script block may assign to `m_iState` or `pev.iuser3`.
3. **Master Capability**: The script entity must return `FCAP_MASTER` (`0x80`) from `ObjectCaps()`.
4. **Target Classes**: Only wrapper primary vtables whose slot 27 matches the safety signature are hooked (see Section 4).

---

## 3. Supported Environment
- **Platform**: Windows x86 (32-bit) only.
- **Game Release**: 5.26 (as reported by the tester).
- **Engine Build**:
  - Protocol: 48
  - Engine version: 5.0.1.8
  - Build: 10257 (Nov 23 2024)
- **Binary Metadata (`server.dll`)**:
  - File size: `UNVERIFIED`
  - Date modified: `UNVERIFIED`
  - SHA-256: `UNVERIFIED`
- **Build Headers**: Metamod-P (`Bots-United/metamod-p`)
- **Runtime Metamod**: Not recorded.
- **Linux Support**: **NOT SUPPORTED**. `InstallHooks()` on non-Windows targets immediately returns `false`.

---

## 4. Safety Guards
1. **Structured Exception Handling (SEH)**: The RTTI scan across `.rdata` / module memory is isolated inside `FindVT()` using `__try / __except (EXCEPTION_EXECUTE_HANDLER)` under MSVC, preventing crashes on protected or discarded memory pages.
2. **Exact Stub Byte Signature**: Before patching, slot 27 must match the exact 5-byte sequence of `mov al, 1 / ret 4`:
   `B0 01 C2 04 00`
3. **Neighbor Slot Invariant**: Neighboring slots 28, 29, and 30 must share the exact same function pointer (`vt[28] == vt[29] && vt[29] == vt[30]`), indicating this is the primary wrapper table layout.
4. **Idempotence**: If slot 27 already points to `IsTriggeredHook`, the table is skipped.
5. **No Runtime Unloading**: The plugin registers with `PT_STARTUP, PT_NEVER`. Runtime `meta unload as_master_shim` is refused by Metamod.
6. **Module Base Validation**: `RemoveHooks()` stores the module base address at initialization (`g_installedBase`). If called during detachment, it ensures `server.dll` has not been reloaded or remapped before writing memory.
7. **Failure Fast**: `Meta_Attach` returns `FALSE` if zero tables were successfully patched, preventing the plugin from reporting a false positive healthy state.

---

## 5. Rules for Adding Any New Hook
A new virtual table slot may be hooked if and only if all of the following conditions are met:
1. **Disassembly Verification**: The contributor must provide concrete static analysis proof that the target slot in original native code is an unmanaged stub.
2. **Byte & Pattern Verification**: The code must check the exact opcode bytes and neighboring slot layout before applying the patch.
3. **Zero Impact on Unmanaged Entities**: The hook must preserve original behaviour for any entity that has not opted in.
4. **Documented Opt-in Contract**: The contract, field encoding, and default handling must be documented in Section 2.
5. **Test Checklist**: A test procedure for the hook must be added to Section 6.

*If any of these conditions cannot be satisfied, the hook must not be implemented.*

---

## 6. Known Side Effects and Limitations
1. **Memory Access**: The plugin performs read-only access on `pev` (specifically reading `+4` for `pev` pointer and `+588` for `iuser3`). It writes only to the targeted vtable slot pointers in memory.
2. **Double `ObjectCaps` Call**: The native engine function `UTIL_IsMasterTriggered` calls slot 9 (`ObjectCaps()`) before invoking slot 27. The hook calls slot 9 once more to confirm the `0x80` bit. This introduces negligible overhead, provided the script's `ObjectCaps` has no side effects.
3. **Native `iuser3` Collisions**: Unverified whether any non-player native entities utilize `iuser3`. Since the contract checks for `FCAP_MASTER` and requires `iuser3 == 1` to lock, risk of interference is minimal.
4. **Plugin Collisions**: Other Metamod plugins hooking virtual table slots of `CBaseCustom*` classes could conflict if not using cooperative hooking patterns.

---

## 7. Test Checklist
To be verified on a game server:
- [ ] **1. Startup Logs**: Console reports `[as_master_shim] hooked ...` for candidate classes and `[as_master_shim] patched N vtable(s)` on startup.
- [ ] **2. Master Gating**: `env_state` starting OFF locks a native `func_door`. Activating via button toggles state between locked and unlocked.
- [ ] **3. Spawnflag**: `SF_ENVSTATE_START_ON` (1) correctly initializes door to open/unlocked at map load.
- [ ] **4. Native Multisource**: Native `multisource` entities continue to function unchanged as masters.
- [ ] **5. Unmanaged Script Master**: A script entity with `FCAP_MASTER` that leaves `iuser3` as `0` counts as passed (`true`), preserving legacy behaviour.
- [ ] **6. Non-Master Script Entity**: A script entity lacking `FCAP_MASTER` triggers the engine log `"can't be used as a master"` and passes (`true`).
- [ ] **7. Level Transitions**: `changelevel` and round restarts do not crash the server and maintain correct state.
- [ ] **8. Removal**: Removing `as_master_shim.dll` from `plugins.ini` cleanly restores standard game behaviour.
- [ ] **9. Unload Refusal**: Console command `meta unload as_master_shim` is rejected by Metamod (`PT_NEVER`).
