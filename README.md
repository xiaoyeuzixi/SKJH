# SKJH

Windows x64 C++ project using DirectX 11, ImGui, and the bundled DMA SDK dependencies.

## Build

Open `SKJH.sln` in Visual Studio and build `Release | x64`, or run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  ".\SKJH.sln" /m /p:Configuration=Release /p:Platform=x64
```

Generated SDK exports, runtime diagnostics, local caches, and build outputs are intentionally excluded from version control.
