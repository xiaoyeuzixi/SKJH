# SKJH

Windows x64 C++ project using DirectX 11, ImGui, and the bundled DMA SDK dependencies.

## Build

Open `SKJH.sln` in Visual Studio and build `Release | x64`, or run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  ".\SKJH.sln" /m /p:Configuration=Release /p:Platform=x64
```

Generated SDK exports, runtime diagnostics, local caches, and build outputs are intentionally excluded from version control.

## SDK Update

The current compiled profile matches the `2026-07-31` export in
`DummyDll.7z`. Extract the archive at the solution root so the active files
are available under `DummyDll/`; the resolver keeps the older
`SKJH SDK 7.23/DummyDll/` export as a diagnostic fallback.

Run the update checks from the solution root after rebuilding:

```powershell
.\x64\Release\SKJH.exe --sdk-check
.\x64\Release\SKJH.exe --signature-check --timeout=30000
.\x64\Release\SKJH.exe --probe --timeout=30000 --probe-out=dma_probe.json
.\x64\Release\SKJH.exe --bone-probe --timeout=30000 --probe-out=bone_probe.json
.\x64\Release\SKJH.exe --player-probe --timeout=30000 --probe-out=player_probe.json
```

All online checks expect the game process to be running on the DMA target.
