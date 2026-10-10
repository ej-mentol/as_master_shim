# as_master_shim

A lightweight Metamod compatibility shim for Sven Co-op allowing AngelScript entities (`ScriptBaseEntity` subclasses, e.g. `env_state`) to act as a `master` for native entities (`func_door`, `trigger_*`, etc.).

---

## English

### What it does
1. On startup, resolves primary vtables of AngelScript wrapper classes (`CBaseCustomBaseEntity`, etc.) via MSVC RTTI under Structured Exception Handling (SEH).
2. Verifies slot 27 (`IsTriggered`) matches the exact native stub bytes `B0 01 C2 04 00` (`mov al, 1 / ret 4`), and neighbor slots 28, 29, 30 point to a single shared stub.
3. Hooks slot 27:
   - If the entity lacks `FCAP_MASTER` (`0x80`), returns `1` (unmodified engine behaviour).
   - If the entity is a master, reads `pev->iuser3` via the contract `return iuser3 != 1`:
     - `0` (unmanaged / default) -> `true` (preserves legacy behaviour)
     - `1` (master OFF) -> `false` (locks native entities)
     - `2` (master ON) -> `true` (unlocks native entities)
4. Disables runtime unloading (`PT_NEVER`).

### Build (Windows x86)
Run `compile_win.bat`. It invokes CMake + NMake (MSVC x86) and outputs `bin\as_master_shim.dll`.

### Installation
1. Place `as_master_shim.dll` into `svencoop/addons/metamod/plugins/`.
2. Add to `svencoop/addons/metamod/plugins.ini`:
   ```ini
   win32 addons/metamod/plugins/as_master_shim.dll
   ```

### Script Usage
In AngelScript entities, synchronize state via the contract (`m_iState + 1`):
- `m_iState = STATE_OFF` (0) -> `pev.iuser3 = 1`
- `m_iState = STATE_ON` (1) -> `pev.iuser3 = 2`

A reference implementation is in `script/env_state.as`. The `MapInit()` block inside is strictly **test-only**; remove it when including into map scripts with an existing `MapInit()`. See `RULES.md` for project rules and specifications.

---

## Русский

Плагин-прослойка (shim) для Sven Co-op, позволяющая скриптовым AngelScript-энтити (`ScriptBaseEntity`, например `env_state`) работать в качестве `master` для нативных энтити (`func_door`, `trigger_*` и др.).

### Установка
1. Скопируйте `as_master_shim.dll` в `svencoop/addons/metamod/plugins/`.
2. Добавьте строку в `svencoop/addons/metamod/plugins.ini`:
   ```ini
   win32 addons/metamod/plugins/as_master_shim.dll
   ```
