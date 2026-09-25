# MEMORY.md - WoTR QoL & Widescreen Mod (`d3d9.dll`)

## 1. Module Overview
- **Project**: `wotr_qol`
- **Location**: `C:\Users\caio_\source\repos\wotr_qol`
- **Target Binary**: `d3d9.dll` (Win32 x86 PE DLL)
- **Target Game**: `Rings.exe` (ImageBase: `0x00400000`, No ASLR)
- **Configuration File**: `d3d9.ini` (located in game root directory)
- **Compilation**: Visual Studio MSBuild (`/std:c++20`, Release | Win32)

---

## 2. Proxy Architecture & Export Forwarding
The DLL acts as an in-place proxy for Microsoft DirectX 9.

- **Exported Symbols** (linker pragmas in `dllmain.cpp`):
  - `/EXPORT:Direct3DCreate9=_Direct3DCreate9@4`
  - `/EXPORT:Direct3DCreate9Ex=_Direct3DCreate9Ex@8`
- **Forwarding Target**: `C:\Windows\System32\d3d9.dll` loaded dynamically via `LoadLibraryA` on first invocation.
- **Initialization**: `DllMain` (`DLL_PROCESS_ATTACH`) spawns `PatchWorkerThread` to safely execute memory patches outside the PE loader lock.

---

## 3. Technical Implementation & Memory Offsets
Base address: `base = (DWORD_PTR)GetModuleHandleA(NULL)`.

### 1. Always-On Health Bars
- **Offset**: `base + 0x047E1E`
- **Target**: Overhead unit HUD draw condition.
- **Original Bytes**: `A0 18 A6 B9 00` (`mov al, ds:[0x00B9A618]`)
- **Patch Bytes**: `B0 01 90 90 90` (`mov al, 1; nop; nop; nop`)
- **INI Key**: `AlwaysOnHealthBars=1`

### 2. Full HD 1080p Resolution Tables & Aspect Ratio
- **Engine Resolution Table 1** (`base + 0x08CAC3`, 104 bytes):
  - Overwrites hardcoded 4:3 display resolutions with widescreen modes.
  - Slot 1 (`0x08CAC3`): Width `1920` (`0x0780`), Height `1080` (`0x0438`).
- **UI Resolution Table 2** (`base + 0x1A6530`, 104 bytes):
  - Overwrites in-game options menu resolution list.
  - Slot 1 (`0x1A6530`): Width `1920` (`0x0780`), Height `1080` (`0x0438`).
- **Aspect Ratio Constant** (`base + 0x30CF10`):
  - Replaces default 4:3 float (`0.77999997f`) with 16:9 aspect correction `1.0402162f` (`CE 25 85 3F`), eliminating horizontal stretching of 3D models.
- **INI Key**: `Enable1080pAndFOV=1`

### 3. Resolution Persistence Hook (Fixes Startup Reset)
- **Offset**: `base + 0x1A65C7` (`0x005A65C7`)
- **Function**: Mode lookup converting `(Width, Height)` -> `ModeIndex`.
- **Root Cause**:
  - At startup (`0x00412490`), the engine reads `Width` and `Height` from `Rings.ini` and calls `0x005A65C7`.
  - Vanilla code only checked 4:3 modes (640, 800, 1024...). For 1920x1080, it returned `-1` (unsupported).
  - Fallback logic at `0x00412576` caught `-1` and forcibly rewrote `Rings.ini` to Mode 4 (1024x768 / 800x600).
- **Hook**:
  - 6-byte inline hook (`jmp Hook_ResolutionLookup; nop`).
  - Matches `1920x1080` and returns `Mode 2`.
  - Falls back to `Mode 2` for unknown wide resolutions instead of returning `-1`.

### 4. Dynamic Camera FOV Hook (Fixes Loading Flare Misalignment)
- **Offset**: `base + 0x157212` (`0x00557212`)
- **Target Function**: Single perspective projection generator used by all viewports.
- **Root Cause of Ring Misalignment**:
  - The 3D One Ring camera (`0x00B51100`) uses the global projection matrix.
  - The loading flare glow routine (`0x004078D6`) hardcodes a 45.59° FOV projection calculation (`2 * atan(14.5 / 34.5)`).
  - Globally forcing 60° FOV shifts the 3D ring down while the flare remains stationary, misaligning them.
- **Hook Mechanics**:
  - Checks engine loading flag `0x0080E466` (`base + 0x40E466`, `1` = loading screen active, `0` = inactive).
    - If `[0x0080E466] != 0`: Forces 45° (`0x3F490FDB`). This prevents saved world cameras deserializing midway through slow savegame loading from prematurely switching to 60° while the loading bar is still active.
  - If `[0x0080E466] == 0`: Inspects viewport type flag at `[esi + 0x2A]`:
    - `0` (Fullscreen / Menus): Sets global FOV `[0x00B4D200]` to `0x3F490FDB` (45° = `0.785398 rad`).
    - `1` (Gameplay matches): Sets global FOV `[0x00B4D200]` to `g_GameplayFovHex` (configurable via `FOV=` in `d3d9.ini`, default 60° = `0x3F860A92`).
  - Executes original 6 bytes (`push ebp; mov ebp, esp; sub esp, 0x24`) and jumps back to `base + 0x157218`.

---

## 4. Configuration Specification (`d3d9.ini`)
```ini
[QoL]
; Always show unit health bars overhead at all times (1 = Enabled, 0 = Disabled)
AlwaysOnHealthBars = 1

; Enables native 1920x1080 Widescreen and dynamic FOV (1 = Enabled, 0 = Disabled)
Enable1080pAndFOV = 1

; Field of View (FOV) in degrees for in-game matches (default: 60, range: 30-90)
; Menus and loading screen automatically preserve 45 to keep elements aligned
FOV = 60
```

---

## 5. Technical Rules for Mod Maintenance
1. **Never alter Menu FOV**: `[esi + 0x2A] == 0` must strictly receive 45° (`0x3F490FDB`).
2. **Never return -1 in Resolution Lookup**: Any unrecognized resolution must resolve to a valid mode index (Mode 2) to prevent `0x00412576` fallback to 1024x768.
3. **No Thread Lock Contention**: All memory patching happens in `PatchWorkerThread` with `PAGE_EXECUTE_READWRITE` via `VirtualProtect`.

