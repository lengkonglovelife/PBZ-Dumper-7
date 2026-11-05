
# Dumper-7

This is a PBZ / Unreal Engine 5.7 adaptation of [Encryqed/Dumper-7](https://github.com/Encryqed/Dumper-7), based on upstream commit `2c1eae303f3d775f6f59aee6e95990d9ce2e1966`.

## PBZ build

The PBZ changes cover object/container layouts, executable-section scanning, startup readiness and invalid-pointer handling. The retained runtime validation was performed on PBZ build `302256`; other builds need their own verification.

The default SDK export root is **`C:\Dumper-7`**, matching upstream. Game/version subdirectories are created below that root. Without `LogFilePath`, output stays in the console. Existing per-game or global INI files can override these defaults.

Injection opens a `Dumper-7 - PBZ` console window. Press **F6** after generation to unload the dumper. With `ShouldAutoUnload=1`, the dumper unloads and its console closes automatically when generation finishes. The optional example configuration enables this automatic unload.

Build the DLL with Visual Studio C++ Build Tools and CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The output is `build/bin/Release/Dumper-7.dll`. `Dumper-7.ini.example` is optional: rename a copy to `Dumper-7.ini` next to the host executable when you need an override.

The optional configuration helper also defaults to `C:\Dumper-7`. Supply the host path explicitly; it records and restores an existing INI:

```powershell
./tools/Manage-Dumper7Config.ps1 -Action Prepare -GameExe "X:\Game\Game.exe"
./tools/Manage-Dumper7Config.ps1 -Action Restore
```

This source tree contains the dumper and its bundled dependencies. SDK dumps, game files, build artifacts, local configuration and unrelated runtime projects are excluded. See [ORIGIN.md](ORIGIN.md) for provenance.

SDK Generator for all Unreal Engine games. Supported versions are all of UE4 and UE5.

## How to use

- Compile the dll in x64-Release
- Inject the dll into your target game
- The SDK is generated into the path specified by `Settings::SDKGenerationPath`, by default this is `C:\\Dumper-7`
- **See [UsingTheSDK](UsingTheSDK.md) for a guide to get started, or to migrate from an old SDK.**
## Overriding Offsets

- ### Only override any offsets if the generator doesn't find them, or if they are incorrect
- All overrides are made in **Generator::InitEngineCore()** inside of **Generator.cpp**

- GObjects (see [GObjects-Layout](#overriding-gobjects-layout) too)
  ```cpp
  ObjectArray::Init(/*GObjectsOffset*/, /*ChunkSize*/, /*bIsChunked*/);
  ```
  ```cpp
  /* Make sure only to use types which exist in the sdk (eg. uint8, uint64) */
  InitObjectArrayDecryption([](void* ObjPtr) -> uint8* { return reinterpret_cast<uint8*>(uint64(ObjPtr) ^ 0x8375); });
  ```
- FName::AppendString
  - Forcing GNames:
    ```cpp
    FName::Init(/*bForceGNames*/); // Useful if the AppendString offset is wrong
    ```
  - Overriding the offset:
    ```cpp
    FName::Init(/*OverrideOffset, OverrideType=[AppendString, ToString, GNames], bIsNamePool*/);
    ```
- ProcessEvent
  ```cpp
  Off::InSDK::InitPE(/*PEIndex*/);
  ```
## Overriding GObjects-Layout
- Only add a new layout if GObjects isn't automatically found for your game.
- Layout overrides are at roughly line 30 of `ObjectArray.cpp`
- For UE4.11 to UE4.20 add the layout to `FFixedUObjectArrayLayouts`
- For UE4.21 and higher add the layout to `FChunkedFixedUObjectArrayLayouts`
- **Examples:**
  ```cpp
  FFixedUObjectArrayLayout // Default UE4.11 - UE4.20
  {
      .ObjectsOffset = 0x0,
      .MaxObjectsOffset = 0x8,
      .NumObjectsOffset = 0xC
  }
  ```
  ```cpp
  FChunkedFixedUObjectArrayLayout // Default UE4.21 and above
  {
      .ObjectsOffset = 0x00,
      .MaxElementsOffset = 0x10,
      .NumElementsOffset = 0x14,
      .MaxChunksOffset = 0x18,
      .NumChunksOffset = 0x1C,
  }
  ```

## Config File
You can optionally dynamically change settings through a `Dumper-7.ini` file, instead of modifying `Settings.h`.
- **Per-game**: Create `Dumper-7.ini` in the same directory as the game's exe file.
- **Global**: Create `Dumper-7.ini` under `C:\Dumper-7`.
- Profiles do not merge. In other words your global profile does not change the default settings.
  
- **SleepTimeout:**
  - If non-zero dump will start after a delay
  - Values under 1000 assumed to be in seconds, otherwise in milliseconds
  - If both SleepTimeout and DumpKey are set whichever occurs first will trigger the dump
- **DumpKey:** 
  - If non-zero dump will start upon key press
  - Value should be a hex or decimal integer corresponding to a [virtual keycode](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes).
  - Hex integers should start with 0x.
- **SDKNamespaceName:**
  - Changes the namespace in the generated files.
- **SDKGenerationPath:**
  - Generate output at the specified path instead of `C:/Dumper-7`.
  - Paths are relative to game executable unless you use an absolute path including drive letter.
  - Use `..` to access parent directories. Do not include quotes.
- **LogFilePath:**
  - Write the dumper's output to this file instead of the console.
- **ShouldAutoUnload:**
  - If non-zero the dumper unloads itself after generation, instead of waiting for F6.
- **ExecuteSDKTestScript:**
  - Overrides `Settings::Debug::bExecuteSDKTestScript`. If non-zero the SDK compilation test script is executed after generation.


### Example:
```ini
[Settings]
SleepTimeout=30
SDKNamespaceName=MyOwnSDKNamespace
DumpKey=0x77
SDKGenerationPath=./
```
- These settings would generate the SDK in the same folder as the game and would start after 30 seconds or upon pressing F8.

## Issues

If you have any issues using the Dumper, please create an Issue on this repository\
and explain the problem **in detail**.

- Should your game be crashing while dumping, attach Visual Studios' debugger to the game and inject the Dumper-7.dll in debug-configuration.
Then include screenshots of the exception causing the crash, a screenshot of the callstack, as well as the console output.

- For compiler-errors in the SDK (first verify you followed all of the steps from [UsingTheSDK](UsingTheSDK.md) correctly):
  1. Send screenshots of the errors in the error-window, make sure to select **`Build Only`**, not `Build + Intellisense`, as Intellisense often reports false positives.
  2. Also send a screenshot of the first error in the **Output**-window (NOT the error-window) and one of the code location causing that exact error.
  3. If you're able to fix the error (eg. missing padding in predefined struct, or incorrect use of macro name) please still report it in an issue with the fix included.
  4. If you're not able to resolve the issue yourself please upload the entire generated folder (not just CppSDK) somewhere and attach the link to your issue.

- Should your own dll-project crash, verify your code thoroughly to make sure the error actually lies within the generated SDK.
