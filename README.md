# SKJH

基于 Windows x64 的 C++ 项目，使用 DirectX 11、ImGui 以及内置的 DMA SDK 依赖库。

## 功能特性

- 基于 DMA 的内存读取，用于获取游戏实体数据（ESP、骨骼、物品检测）
- DirectX 11 叠加层渲染，集成 ImGui 界面
- 自动化 SDK 导出与签名探测
- 运行时诊断与验证工具链

## 快速开始

### 1. 下载 SDK 依赖

从 [GitHub Release v8.16](https://github.com/xiaoyeuzixi/SKJH/releases/tag/v8.16) 下载
`SKJH DummyDll 8.16.7z`，解压后将 `SKJH DummyDll 8.16/` 文件夹放到项目根目录。

### 2. 编译

在 Visual Studio 中打开 `SKJH.sln`，选择 `Release | x64` 进行编译，或通过命令行运行：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  ".\SKJH.sln" /m /p:Configuration=Release /p:Platform=x64
```

SDK 导出文件、运行时诊断产物、本地缓存及编译输出均已排除在版本控制之外。

## SDK 更新

当前编译的配置文件对应 `SKJH DummyDll 8.16.7z` 中的 `8.16` 导出。
所需的 `dump.cs` 和 `script.json` 文件位于 `SKJH DummyDll 8.16/` 目录下；
解析器会保留旧版编译配置作为诊断回退。

重新编译后，在解决方案根目录运行以下更新检查命令：

```powershell
.\x64\Release\SKJH.exe --sdk-check
.\x64\Release\SKJH.exe --signature-check --timeout=30000
.\x64\Release\SKJH.exe --probe --timeout=30000 --probe-out=dma_probe.json
.\x64\Release\SKJH.exe --bone-probe --timeout=30000 --probe-out=bone_probe.json
.\x64\Release\SKJH.exe --player-probe --timeout=30000 --probe-out=player_probe.json
```

如需检查 DMA 设备并枚举远程进程（无需等待渲染器），可运行：

```powershell
.\x64\Release\SKJH.exe --dma-check --target-process=SKJH.exe
```

所有在线检查命令均要求游戏进程在 DMA 目标机上运行。

## DMA 模块导出

程序会自动从 `SKJH DummyDll 8.16/` 中选择当前生效的 8.16 SDK 并缓存。
如需从远程 DMA 主机捕获目标模块，在解决方案根目录运行：

```powershell
.\x64\Release\SKJH.exe --memory-export=GameAssembly.dll `
  --target-process=SKJH.exe --timeout=30000 --export-out=dma_export
```

该命令将生成 `GameAssembly.dll.mem.bin`、`GameAssembly.dll.memory.json`
和 `GameAssembly.dll.exports.json`。二进制文件为按模块映像顺序排列的虚拟映像转储；
JSON 清单记录了远程基址、映像大小及不可读的 4 KiB 页面信息。
若仅需模块的导出地址表（EAT），可单独使用 `--export-functions=GameAssembly.dll`。

## 交流群

**QQ 交流群：835775657**

欢迎加入交流群讨论项目相关问题与开发进展。

## 许可声明

本项目仅供教育与学习研究用途。
