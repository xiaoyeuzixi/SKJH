# SKJH

Windows x64 C++ project using DirectX 11, ImGui, and the bundled DMA SDK dependencies.

## Features

- DMA-based memory reading for game entity data (ESP, skeleton, item detection)
- DirectX 11 overlay rendering with ImGui
- Automated SDK export and signature probing
- Runtime diagnostics and validation tooling

## Build

Open `SKJH.sln` in Visual Studio and build `Release | x64`, or run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  ".\SKJH.sln" /m /p:Configuration=Release /p:Platform=x64
```

Generated SDK exports, runtime diagnostics, local caches, and build outputs are intentionally excluded from version control.

## SDK Update

The current compiled profile matches the `8.16` export in
`SKJH DummyDll 8.16.7z`. The required `dump.cs` and `script.json` files are
kept under `SKJH DummyDll 8.16/`; the resolver retains older compiled
profiles as diagnostic fallbacks.

Run the update checks from the solution root after rebuilding:

```powershell
.\x64\Release\SKJH.exe --sdk-check
.\x64\Release\SKJH.exe --signature-check --timeout=30000
.\x64\Release\SKJH.exe --probe --timeout=30000 --probe-out=dma_probe.json
.\x64\Release\SKJH.exe --bone-probe --timeout=30000 --probe-out=bone_probe.json
.\x64\Release\SKJH.exe --player-probe --timeout=30000 --probe-out=player_probe.json
```

To check the DMA device and enumerate the remote process without waiting for
the renderer, run:

```powershell
.\x64\Release\SKJH.exe --dma-check --target-process=SKJH.exe
```

All online checks expect the game process to be running on the DMA target.

## DMA module export

The active 8.16 SDK is selected automatically from `SKJH DummyDll 8.16/` and
cached beside that export. To capture a target module from the remote DMA
machine, run the following from the solution root:

```powershell
.\x64\Release\SKJH.exe --memory-export=GameAssembly.dll `
  --target-process=SKJH.exe --timeout=30000 --export-out=dma_export
```

The command writes `GameAssembly.dll.mem.bin`, `GameAssembly.dll.memory.json`,
and `GameAssembly.dll.exports.json`. The binary is a virtual-image dump in
module-image order; the JSON manifest records the remote base, image size, and
any unreadable 4 KiB pages. `--export-functions=GameAssembly.dll` can be used
alone when only the module's EAT is needed.

## Contact / QQ Group

**QQ交流群：835775657**

欢迎加入交流群讨论项目相关问题和开发进展。

## License

This project is provided for educational and research purposes only.
