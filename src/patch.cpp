#include <cmath>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include "patch.h"

static_assert(sizeof(void*) == 4, "x86 only");

// HLSDK / Metamod headers
#include <extdll.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <meta_api.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace as_master_shim {

static ptrdiff_t g_pevOffset = DEFAULT_PEV_OFFSET;

struct PatchedVtableEntry {
    void** vtable;
    size_t slotIndex;
    void* originalFunc;
    const char* className;
};

static std::vector<PatchedVtableEntry> g_patchedVtables;
static uintptr_t g_installedBase = 0;

#if defined(_WIN32)
static bool IsRet1Stub(const void* fn)
{
    // stub: mov al,1 / ret 4
    static const unsigned char sig[] = { 0xB0, 0x01, 0xC2, 0x04, 0x00 };
    return std::memcmp(fn, sig, sizeof(sig)) == 0;
}

static bool __fastcall IsTriggeredHook(void* self, void* /*edx*/, void* /*activator*/)
{
    if (!self) return true;

    auto vt = *reinterpret_cast<void***>(self);
    // slot 9: int ObjectCaps()
    auto pfnObjectCaps = reinterpret_cast<int(__fastcall*)(void*, void*)>(vt[SLOT_OBJECTCAPS]);
    int caps = pfnObjectCaps(self, nullptr);

    // If FCAP_MASTER (0x80) is not set, behave as default (return true)
    if (!(caps & 0x80)) {
        return true;
    }

    // pvPrivateData -> pev is at offset +4
    char* pev = *reinterpret_cast<char**>(static_cast<char*>(self) + 4);
    if (!pev) {
        return true;
    }

    // Opt-in contract:
    // 0: unmanaged -> return true (preserves old behaviour)
    // 1: OFF -> return false
    // 2: ON -> return true
    int v = *reinterpret_cast<int*>(pev + g_pevOffset);
    return v != 1;
}
#else
static bool IsTriggeredHook(void* self, void* /*activator*/)
{
    if (!self) return true;
    return true; // Stub for linux build testing
}
#endif

#if defined(_WIN32)

// Structure of MSVC x86 RTTI Complete Object Locator
struct _RTTICompleteObjectLocator {
    unsigned long signature;             // 0 in 32-bit
    unsigned long offset;                // offset of this vtable in the complete class
    unsigned long cdOffset;              // constructor displacement offset
    unsigned long pTypeDescriptor;       // TypeDescriptor* (32-bit va)
    unsigned long pClassDescriptor;      // _RTTIClassHierarchyDescriptor* (32-bit va)
};

static bool ProtectMemory(void* addr, size_t size, DWORD newProtect, DWORD* oldProtect) {
    return VirtualProtect(addr, size, newProtect, oldProtect) != 0;
}

static bool FindServerModule(uintptr_t& baseAddr, size_t& moduleSize) {
    HMODULE hMod = GetModuleHandleA("server.dll");
    if (!hMod) {
        hMod = GetModuleHandleA("server");
    }
    if (!hMod) {
        return false;
    }

    MODULEINFO modInfo = {0};
    if (!GetModuleInformation(GetCurrentProcess(), hMod, &modInfo, sizeof(modInfo))) {
        return false;
    }

    baseAddr = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    moduleSize = modInfo.SizeOfImage;
    return true;
}

// Search for byte pattern in memory range
static const void* FindPattern(const void* start, size_t length, const void* pattern, size_t patternLen) {
    if (!start || length < patternLen || patternLen == 0) return nullptr;
    const unsigned char* pStart = static_cast<const unsigned char*>(start);
    const unsigned char* pEnd = pStart + length - patternLen;
    const unsigned char* pPat = static_cast<const unsigned char*>(pattern);
    unsigned char first = pPat[0];

    for (const unsigned char* p = pStart; p <= pEnd; ++p) {
        if (*p == first && std::memcmp(p, pPat, patternLen) == 0) {
            return p;
        }
    }
    return nullptr;
}

static void** FindPrimaryVTableForMangledName(uintptr_t base, size_t size, const char* mangledName) {
    size_t nameLen = std::strlen(mangledName) + 1;
    const void* pName = FindPattern(reinterpret_cast<const void*>(base), size, mangledName, nameLen);
    if (!pName) {
        return nullptr;
    }

    // In MSVC x86 TypeDescriptor:
    // void* pVFTable;
    // void* spare;
    // char name[];
    // So &TypeDescriptor = pName - 8
    uintptr_t pTypeDesc = reinterpret_cast<uintptr_t>(pName) - 8;

    // Scan for RTTICompleteObjectLocator pointing to pTypeDesc with offset == 0
    const uintptr_t pEnd = base + size - sizeof(_RTTICompleteObjectLocator);
    uintptr_t pFoundCOL = 0;

    for (uintptr_t p = base; p <= pEnd; p += 4) {
        auto col = reinterpret_cast<const _RTTICompleteObjectLocator*>(p);
        if (col->signature == 0 && col->offset == 0 && col->pTypeDescriptor == pTypeDesc) {
            pFoundCOL = p;
            break;
        }
    }

    if (!pFoundCOL) {
        return nullptr;
    }

    // In MSVC RTTI, vtable[-1] == &RTTICompleteObjectLocator
    const uintptr_t pEndSearch = base + size - 4;
    for (uintptr_t p = base; p <= pEndSearch; p += 4) {
        if (*reinterpret_cast<const uintptr_t*>(p) == pFoundCOL) {
            // Found vtable[-1]!
            // vtable begins at p + 4
            return reinterpret_cast<void**>(p + 4);
        }
    }

    return nullptr;
}

static void** FindVT(uintptr_t b, size_t s, const char* n) {
#if defined(_MSC_VER)
    __try {
        return FindPrimaryVTableForMangledName(b, s, n);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
#else
    return FindPrimaryVTableForMangledName(b, s, n);
#endif
}

#endif // _WIN32

bool InstallHooks() {
#if defined(_WIN32)
    uintptr_t base = 0;
    size_t size = 0;
    if (!FindServerModule(base, size)) {
        LOG_CONSOLE(PLID, "[as_master_shim] server.dll not found");
        return false;
    }

    g_installedBase = base;

#ifdef _DEBUG
    LOG_CONSOLE(PLID, "[as_master_shim] server.dll base: 0x%08X, size: 0x%08X, pev offset: %d", (unsigned)base, (unsigned)size, (int)g_pevOffset);
#endif

    // List of candidate wrapper classes (primary vtables)
    static const char* const s_wrapperClasses[] = {
        ".?AV?$CBaseCustomBaseEntity@VCBaseEntity@@@@",
        ".?AV?$CBaseCustomBaseAnimating@VCBaseAnimating@@@@",
        ".?AV?$CBaseCustomMonsterEntity@VCBaseMonster@@@@",
        ".?AV?$CBaseCustomTankEntity@VCFuncTank@@@@",
        ".?AV?$CBaseCustomPlayerItemEntity@VCBasePlayerItem@@@@",
        ".?AV?$CBaseCustomPlayerWeaponEntity@VCBasePlayerWeapon@@@@",
        ".?AV?$CBaseCustomItemEntity@VCItem@@@@",
        ".?AV?$CBaseCustomPlayerAmmoEntity@VCBasePlayerAmmo@@@@"
    };

    g_patchedVtables.clear();

    for (const char* className : s_wrapperClasses) {
        void** vt = FindVT(base, size, className);
        if (!vt) {
            continue;
        }

        void* s27 = vt[SLOT_ISTRIGGERED];

        if (s27 == reinterpret_cast<void*>(&IsTriggeredHook)) {
            continue;
        }

        // Strict guard:
        // 1. Slot 27 must be exact stub: mov al, 1 / ret 4 (B0 01 C2 04 00)
        // 2. Neighbor slots 28, 29, 30 must share identical function pointer
        if (!IsRet1Stub(s27) || vt[28] != vt[29] || vt[29] != vt[30]) {
            LOG_CONSOLE(PLID, "[as_master_shim] %s: slot27 mismatch, skipped", className);
            continue;
        }

        // Apply hook
        DWORD oldProt = 0;
        if (!ProtectMemory(&vt[SLOT_ISTRIGGERED], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
            continue;
        }

        vt[SLOT_ISTRIGGERED] = reinterpret_cast<void*>(&IsTriggeredHook);
        ProtectMemory(&vt[SLOT_ISTRIGGERED], sizeof(void*), oldProt, &oldProt);

        PatchedVtableEntry entry;
        entry.vtable = vt;
        entry.slotIndex = SLOT_ISTRIGGERED;
        entry.originalFunc = s27;
        entry.className = className;
        g_patchedVtables.push_back(entry);

        LOG_CONSOLE(PLID, "[as_master_shim] hooked %s", className);
    }

    LOG_CONSOLE(PLID, "[as_master_shim] patched %u vtable(s)", (unsigned)g_patchedVtables.size());
    return !g_patchedVtables.empty();
#else
    return false;
#endif
}

void RemoveHooks() {
#if defined(_WIN32)
    HMODULE hMod = GetModuleHandleA("server.dll");
    if (!hMod) {
        hMod = GetModuleHandleA("server");
    }

    if (reinterpret_cast<uintptr_t>(hMod) != g_installedBase) {
        g_patchedVtables.clear();
        return;
    }

    for (const auto& entry : g_patchedVtables) {
        if (entry.vtable[entry.slotIndex] != reinterpret_cast<void*>(&IsTriggeredHook)) {
            continue;
        }

        DWORD oldProt = 0;
        if (ProtectMemory(&entry.vtable[entry.slotIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
            entry.vtable[entry.slotIndex] = entry.originalFunc;
            ProtectMemory(&entry.vtable[entry.slotIndex], sizeof(void*), oldProt, &oldProt);
        }
    }
    g_patchedVtables.clear();
#endif
}

} // namespace as_master_shim
