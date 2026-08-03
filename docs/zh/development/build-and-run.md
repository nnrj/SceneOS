# 构建、运行与环境引导

## 主机工具

- Python 3.10+
- CMake 3.25+
- Ninja
- Clang 与 LLD
- NASM
- QEMU（必需）
- Bochs（可选）
- 制作发布 ISO：Linux/macOS 或 CI 中的 xorriso

运行诊断：

```text
python tools/scenectl.py doctor
```

工具不在 PATH 时可设置 `SCENEOS_CMAKE`、`SCENEOS_NINJA`、
`SCENEOS_CLANG`、`SCENEOS_LLD`、`SCENEOS_NASM`、
`SCENEOS_XORRISO`、`SCENEOS_QEMU` 或 `SCENEOS_BOCHS`，值为对应
可执行文件的绝对路径。Windows 下还会自动查找 WinGet Links 以及 CMake、
LLVM、QEMU 的标准安装目录。

## 项目本地 bootstrap

Windows：

```powershell
.\bootstrap.ps1
```

Linux：

```sh
./bootstrap.sh
```

当前 bootstrap 只下载经过大小和 SHA-256 校验的固定版 Limine 到
`.scene-sdk/`。它不安装或修改其他主机工具，不要求管理员权限，不修改注册表、
全局 PATH、shell profile 或系统目录。

## 构建和运行

```powershell
.\build.ps1
.\run.ps1
python tools/scenectl.py smoke
python tools/scenectl.py smoke --page-fault
.\run.ps1 --emulator bochs
```

```sh
./build.sh
./run.sh
python3 tools/scenectl.py smoke
./run.sh --emulator bochs
```

直接使用底层工具：

```text
cmake --preset dev
cmake --build --preset dev
python tools/scenectl.py image
python tools/scenectl.py run
```

Windows 下 `run` 自动使用 QEMU + EDK2 的 UEFI 虚拟 FAT 介质，仍可一键
运行。`image` 使用 xorriso 产生可发布 ISO，推荐在 Linux/macOS 或 CI 中
执行。Bochs 目前仍要求 ISO。

不使用 oscdimg 生成 SceneOS 镜像。其官方二进制会声明仅许可用于生成
Microsoft 授权内容，而 SceneOS 不应默认假设拥有该授权。检测到 oscdimg 时，
`doctor` 只报告并忽略它。

所有生成物位于 `build/<preset>/`。构建不复制文件到 `nnos`，不覆盖源文件，
不写入虚拟机工具目录。

GitHub Actions 会在临时 Linux runner 中安装工具、构建 ISO，并通过 QEMU 串口
执行启动冒烟测试。该 CI 环境与物理开发机隔离。

## 验收

QEMU 串口应输出 `SceneOS kernel bootstrap`、GDT/TSS/IDT、bootloader、内存
映射、`Local APIC + IOAPIC`、PIT GSI、硬件 tick、HPET/TSC 时钟源、定时器队列、
首次物理页和 framebuffer 信息。图形窗口应显示深蓝桌面、任务栏和一个验证
窗口。
