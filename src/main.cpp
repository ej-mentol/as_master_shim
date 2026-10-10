#include <cmath>
#include <cstring>
#include "patch.h"

// Include engine and metamod headers
#include <extdll.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <meta_api.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wwrite-strings"
#endif

// Global vars required by Metamod
meta_globals_t *gpMetaGlobals;
gamedll_funcs_t *gpGamedllFuncs;
mutil_funcs_t *gpMetaUtilFuncs;
enginefuncs_t g_engfuncs;
globalvars_t *gpGlobals;

#if defined(_WIN32) && defined(_M_IX86)
#pragma comment(linker, "/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8")
#endif

// Plugin info
plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,
    "as_master_shim",
    "0.0.1",
    "2026/10/10",
    "",
    "",
    "ASMSHIM",
    PT_STARTUP,
    PT_NEVER
};

static DLL_FUNCTIONS g_dll_hooks;

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion) {
    if (!pFunctionTable || !interfaceVersion) {
        return FALSE;
    }

    if (*interfaceVersion != INTERFACE_VERSION) {
        *interfaceVersion = INTERFACE_VERSION;
        return FALSE;
    }

    std::memset(&g_dll_hooks, 0, sizeof(g_dll_hooks));
    std::memcpy(pFunctionTable, &g_dll_hooks, sizeof(DLL_FUNCTIONS));
    return TRUE;
}

#if defined(_WIN32)
extern "C" C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t *pengfuncsFromEngine, globalvars_t *pGlobals) {
#else
extern "C" C_DLLEXPORT void GiveFnptrsToDll(enginefuncs_t *pengfuncsFromEngine, globalvars_t *pGlobals) {
#endif
    if (pengfuncsFromEngine) {
        std::memcpy(&g_engfuncs, pengfuncsFromEngine, sizeof(enginefuncs_t));
    }
    gpGlobals = pGlobals;
}

static META_FUNCTIONS gMetaFunctionTable = {
    NULL,           // pfnGetEntityAPI
    NULL,           // pfnGetEntityAPI_Post
    GetEntityAPI2,  // pfnGetEntityAPI2
    NULL,           // pfnGetEntityAPI2_Post
    NULL,           // pfnGetNewDLLFunctions
    NULL,           // pfnGetNewDLLFunctions_Post
    NULL,           // pfnGetEngineFunctions
    NULL,           // pfnGetEngineFunctions_Post
};

C_DLLEXPORT int Meta_Query(char *interfaceVersion, plugin_info_t **plinfo, mutil_funcs_t *pMetaUtilFuncs) {
    if (!interfaceVersion || !plinfo || !pMetaUtilFuncs) {
        return FALSE;
    }

    *plinfo = &Plugin_info;
    gpMetaUtilFuncs = pMetaUtilFuncs;
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS *pFunctionTable, meta_globals_t *pMGlobals, gamedll_funcs_t *pGamedllFuncs) {
    if (!pFunctionTable || !pMGlobals) {
        return FALSE;
    }

    gpMetaGlobals = pMGlobals;
    gpGamedllFuncs = pGamedllFuncs;

    std::memcpy(pFunctionTable, &gMetaFunctionTable, sizeof(META_FUNCTIONS));

    if (!as_master_shim::InstallHooks()) {
        return FALSE;
    }

    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason) {
    as_master_shim::RemoveHooks();
    gpGamedllFuncs = nullptr;
    gpMetaGlobals = nullptr;
    gpMetaUtilFuncs = nullptr;
    return TRUE;
}
