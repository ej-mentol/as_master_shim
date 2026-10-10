#pragma once

#include <cstddef>

namespace as_master_shim {

// Configurable offset of the int field in entvars_t to read state from.
// Default: iuser3. In standard HLSDK / Sven Co-op x86:
// spawnflags = 416 (0x1A0)
// iuser3 offset 588 was observed on the tested build (see RULES.md)
//
// Encoding contract:
// 0: unmanaged (default / untouched) -> returns true (original behaviour preserved)
// 1: master is OFF -> returns false
// 2: master is ON -> returns true
// Hook evaluates: (pev->iuser3 != 1)
constexpr ptrdiff_t DEFAULT_PEV_OFFSET = 588;

// Vtable slot indices
constexpr size_t SLOT_OBJECTCAPS = 9;
constexpr size_t SLOT_ISTRIGGERED = 27;

bool InstallHooks();
void RemoveHooks();

} // namespace as_master_shim
