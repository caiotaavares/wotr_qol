// dllmain.cpp : Proxy DLL for d3d9.dll (WoTR Quality of Life, Widescreen 1080p, and Dynamic FOV Mod)
#include "pch.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:Direct3DCreate9=_Direct3DCreate9@4")
#pragma comment(linker, "/EXPORT:Direct3DCreate9Ex=_Direct3DCreate9Ex@8")
#endif

// ============================================================================
// PROXY - Direct3DCreate9 & Direct3DCreate9Ex
// ============================================================================
typedef void* (WINAPI* PFN_Direct3DCreate9)(UINT SDKVersion);
typedef HRESULT (WINAPI* PFN_Direct3DCreate9Ex)(UINT SDKVersion, void** ppD3D);

static PFN_Direct3DCreate9 pOriginalDirect3DCreate9 = NULL;
static PFN_Direct3DCreate9Ex pOriginalDirect3DCreate9Ex = NULL;

extern "C" __declspec(dllexport) void* WINAPI Direct3DCreate9(UINT SDKVersion)
{
    if (!pOriginalDirect3DCreate9)
    {
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, "\\d3d9.dll");

        HMODULE hRealD3D9 = LoadLibraryA(sysPath);
        if (hRealD3D9)
        {
            pOriginalDirect3DCreate9 = (PFN_Direct3DCreate9)GetProcAddress(hRealD3D9, "Direct3DCreate9");
        }
    }

    if (pOriginalDirect3DCreate9)
    {
        return pOriginalDirect3DCreate9(SDKVersion);
    }

    return NULL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, void** ppD3D)
{
    if (!pOriginalDirect3DCreate9Ex)
    {
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, "\\d3d9.dll");

        HMODULE hRealD3D9 = LoadLibraryA(sysPath);
        if (hRealD3D9)
        {
            pOriginalDirect3DCreate9Ex = (PFN_Direct3DCreate9Ex)GetProcAddress(hRealD3D9, "Direct3DCreate9Ex");
        }
    }

    if (pOriginalDirect3DCreate9Ex)
    {
        return pOriginalDirect3DCreate9Ex(SDKVersion, ppD3D);
    }

    return E_FAIL;
}

// ============================================================================
// MEMORY UTILITIES
// ============================================================================
static bool WriteMemorySafe(void* address, const void* data, size_t size)
{
    __try
    {
        DWORD oldProtect;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            return false;
        }

        memcpy(address, data, size);
        VirtualProtect(address, size, oldProtect, &oldProtect);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ============================================================================
// QUALITY OF LIFE - ALWAYS-ON HEALTH BARS
// ============================================================================
static void ApplyHealthBarPatch()
{
    DWORD_PTR base = (DWORD_PTR)GetModuleHandleA(NULL);
    const unsigned char patch[] = { 0xB0, 0x01, 0x90, 0x90, 0x90 };
    WriteMemorySafe((void*)(base + 0x047E1E), patch, sizeof(patch));
}

// ============================================================================
// WIDESCREEN 1080p & DYNAMIC FOV
// ============================================================================
static DWORD g_ContinueCameraProjection = 0;
static DWORD g_GameplayFovHex = 0x3F860A92; // 60 degrees (1.047198 rad)
static const DWORD g_MenuFovHex = 0x3F490FDB; // 45 degrees (0.785398 rad - vanilla)

static void __declspec(naked) Hook_CameraProjection()
{
    __asm {
        push eax
        // esi = Camera / Viewport object pointer
        cmp byte ptr [esi + 0x2A], 0
        jne is_gameplay

        // Fullscreen / Menu / Loading Screen: 45 degrees (perfect flare alignment!)
        mov eax, g_MenuFovHex
        mov dword ptr ds:[0x00B4D200], eax
        jmp hook_done

    is_gameplay:
        // In-game 3D world: 60 degrees (or configured in d3d9.ini)
        mov eax, g_GameplayFovHex
        mov dword ptr ds:[0x00B4D200], eax

    hook_done:
        pop eax

        // Original 6 bytes at 0x00557212:
        // push ebp
        // mov ebp, esp
        // sub esp, 0x24
        push ebp
        mov ebp, esp
        sub esp, 0x24

        // Jump back to continue execution at 0x00557218
        jmp dword ptr [g_ContinueCameraProjection]
    }
}

static void __declspec(naked) Hook_ResolutionLookup()
{
    __asm {
        // eax = DWORD* pMode
        // edx = width
        // esi = height

        // Mode 2: 1920x1080 (Primary Full HD)
        cmp edx, 1920
        jne check_1280
        cmp esi, 1080
        jne check_1920_1200
        mov dword ptr [eax], 2
        ret

    check_1920_1200:
        // Mode 9: 1920x1200
        cmp esi, 1200
        jne fallback_1080
        mov dword ptr [eax], 9
        ret

    check_1280:
        // Mode 3: 1280x720
        cmp edx, 1280
        jne check_1360
        cmp esi, 720
        jne fallback_1080
        mov dword ptr [eax], 3
        ret

    check_1360:
        // Mode 4: 1360x768
        cmp edx, 1360
        jne check_1366
        cmp esi, 768
        jne fallback_1080
        mov dword ptr [eax], 4
        ret

    check_1366:
        // Mode 5: 1366x768
        cmp edx, 1366
        jne check_1440
        cmp esi, 768
        jne fallback_1080
        mov dword ptr [eax], 5
        ret

    check_1440:
        // Mode 6: 1440x900
        cmp edx, 1440
        jne check_1680
        cmp esi, 900
        jne fallback_1080
        mov dword ptr [eax], 6
        ret

    check_1680:
        // Mode 7: 1680x1050
        cmp edx, 1680
        jne check_1600
        cmp esi, 1050
        jne fallback_1080
        mov dword ptr [eax], 7
        ret

    check_1600:
        // Mode 8: 1600x900
        cmp edx, 1600
        jne fallback_1080
        cmp esi, 900
        jne fallback_1080
        mov dword ptr [eax], 8
        ret

    fallback_1080:
        // If resolution is unknown or >= 1920, default to Mode 2 (1920x1080)
        // instead of returning -1 (which caused the game to reset to 4:3 1024x768)
        mov dword ptr [eax], 2
        ret
    }
}

static void ApplyFullWidescreenFovMod()
{
    DWORD_PTR base = (DWORD_PTR)GetModuleHandleA(NULL);

    // 1. Engine Resolution Table 1 (base + 0x08CAC3)
    const unsigned char resTable1[104] = {
        0xC7, 0x01, 0x80, 0x07, 0x00, 0x00, 0xC7, 0x00, 0x38, 0x04, 0x00, 0x00, 0xC3,
        0xC7, 0x01, 0x00, 0x05, 0x00, 0x00, 0xC7, 0x00, 0xD0, 0x02, 0x00, 0x00, 0xC3,
        0xC7, 0x01, 0x50, 0x05, 0x00, 0x00, 0xC7, 0x00, 0x00, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x01, 0x56, 0x05, 0x00, 0x00, 0xC7, 0x00, 0x00, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x01, 0xA0, 0x05, 0x00, 0x00, 0xC7, 0x00, 0x84, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x01, 0x90, 0x06, 0x00, 0x00, 0xC7, 0x00, 0x1A, 0x04, 0x00, 0x00, 0xC3,
        0xC7, 0x01, 0x40, 0x06, 0x00, 0x00, 0xC7, 0x00, 0x84, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x01, 0x80, 0x07, 0x00, 0x00, 0xC7, 0x00, 0xB0, 0x04, 0x00, 0x00, 0xC3
    };
    WriteMemorySafe((void*)(base + 0x08CAC3), resTable1, sizeof(resTable1));

    // 2. Menu/UI Resolution Table 2 (base + 0x1A6530)
    const unsigned char resTable2[104] = {
        0xC7, 0x07, 0x80, 0x07, 0x00, 0x00, 0xC7, 0x06, 0x38, 0x04, 0x00, 0x00, 0xC3,
        0xC7, 0x07, 0x00, 0x05, 0x00, 0x00, 0xC7, 0x06, 0xD0, 0x02, 0x00, 0x00, 0xC3,
        0xC7, 0x07, 0x50, 0x05, 0x00, 0x00, 0xC7, 0x06, 0x00, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x07, 0x56, 0x05, 0x00, 0x00, 0xC7, 0x06, 0x00, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x07, 0xA0, 0x05, 0x00, 0x00, 0xC7, 0x06, 0x84, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x07, 0x90, 0x06, 0x00, 0x00, 0xC7, 0x06, 0x1A, 0x04, 0x00, 0x00, 0xC3,
        0xC7, 0x07, 0x40, 0x06, 0x00, 0x00, 0xC7, 0x06, 0x84, 0x03, 0x00, 0x00, 0xC3,
        0xC7, 0x07, 0x80, 0x07, 0x00, 0x00, 0xC7, 0x06, 0xB0, 0x04, 0x00, 0x00, 0xC3
    };
    WriteMemorySafe((void*)(base + 0x1A6530), resTable2, sizeof(resTable2));

    // 3. Aspect Ratio 16:9 Constant (base + 0x30CF10)
    const unsigned char aspectRatio[] = { 0xCE, 0x25, 0x85, 0x3F };
    WriteMemorySafe((void*)(base + 0x30CF10), aspectRatio, sizeof(aspectRatio));

    // 4. Dynamic FOV Hook at base + 0x157212 (0x00557212)
    void* hookTarget = (void*)(base + 0x157212);
    g_ContinueCameraProjection = (DWORD)(base + 0x157218);

    unsigned char jmpPatch[6];
    jmpPatch[0] = 0xE9; // JMP rel32
    DWORD relAddr = (DWORD)&Hook_CameraProjection - ((DWORD)hookTarget + 5);
    memcpy(jmpPatch + 1, &relAddr, 4);
    jmpPatch[5] = 0x90; // NOP

    WriteMemorySafe(hookTarget, jmpPatch, sizeof(jmpPatch));

    // 5. Resolution Lookup Hook at base + 0x1A65C7 (0x005A65C7)
    void* resLookupTarget = (void*)(base + 0x1A65C7);
    unsigned char jmpResPatch[6];
    jmpResPatch[0] = 0xE9; // JMP rel32
    DWORD relResAddr = (DWORD)&Hook_ResolutionLookup - ((DWORD)resLookupTarget + 5);
    memcpy(jmpResPatch + 1, &relResAddr, 4);
    jmpResPatch[5] = 0x90; // NOP

    WriteMemorySafe(resLookupTarget, jmpResPatch, sizeof(jmpResPatch));
}

// ============================================================================
// CONFIGURATION & WORKER THREAD
// ============================================================================
static void LoadConfigAndApplyMods()
{
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(iniPath, "d3d9.ini");

    int healthBars = GetPrivateProfileIntA("QoL", "AlwaysOnHealthBars", 1, iniPath);
    int enable1080pAndFOV = GetPrivateProfileIntA("QoL", "Enable1080pAndFOV", 1, iniPath);
    int fovDeg = GetPrivateProfileIntA("QoL", "FOV", 60, iniPath);
    if (fovDeg < 30) fovDeg = 30;
    if (fovDeg > 90) fovDeg = 90;
    float fovRad = (float)(fovDeg * 3.14159265358979323846 / 180.0);
    g_GameplayFovHex = *(DWORD*)&fovRad;

    if (healthBars == 1) ApplyHealthBarPatch();
    if (enable1080pAndFOV == 1) ApplyFullWidescreenFovMod();
}

static DWORD WINAPI PatchWorkerThread(LPVOID lpParam)
{
    LoadConfigAndApplyMods();
    return 0;
}

// ============================================================================
// ENTRY POINT
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        {
            HANDLE hThread = CreateThread(NULL, 0, PatchWorkerThread, NULL, 0, NULL);
            if (hThread)
            {
                CloseHandle(hThread);
            }
            else
            {
                LoadConfigAndApplyMods();
            }
        }
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
