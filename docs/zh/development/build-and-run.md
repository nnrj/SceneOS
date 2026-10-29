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

移动仓库后，旧的 CMake 缓存仍可能引用原来的绝对路径。可在新仓库目录执行：

```powershell
python tools/scenectl.py build --preset dev --fresh
python tools/scenectl.py build --preset release --fresh
```

`--fresh` 使用 CMake 的缓存重建机制，重建所选 preset 下的 `CMakeCache.txt`
和 `CMakeFiles/`，然后重新配置和构建；不清理 `.scene-sdk/` 或历史日志。
`configure` 也支持该选项。它会重置手动缓存配置，随后由当前 preset 和命令参数
重新确定选项。日常构建不需要 `--fresh`。

GitHub Actions 会在临时 Linux runner 中安装工具、构建 ISO，并通过 QEMU 串口
执行启动冒烟测试。该 CI 环境与物理开发机隔离。

## 验收

QEMU 串口应输出 `SceneOS kernel bootstrap`、GDT/TSS/IDT、bootloader、内存
映射、`Local APIC + IOAPIC`、IRQ 注册、PIT GSI、硬件 tick、HPET/TSC 时钟源、
定时器队列、首次物理页和 framebuffer 信息。图形窗口应显示深蓝桌面、任务栏
和一个验证窗口，串口还应报告 PS/2 键盘已就绪。

使用 `python tools/scenectl.py smoke --keyboard --timeout 10` 执行输入自动验收。
脚本等待内核进入测试点，通过 QEMU monitor 发送 `A`，并要求 IRQ1 交付语义
按键事件且队列无丢失。该测试为 headless，不会向宿主桌面输入字符。

对应的指针验收命令是 `python tools/scenectl.py smoke --mouse --timeout 10`。
它通过 QEMU Monitor 发送相对移动，并要求 IRQ12 交付指针事件。两种输入自测都
不会永久改变普通交互构建；下一次 configure 会显式选择本次所需选项。

`python tools/scenectl.py smoke --clock-check --timeout 10` 验证 CPUID 频率
解析和诊断边界；追加 `--tsc-clock` 可验证 TSC 回退与自测共存。日志中的真实
CPU 诊断与人工输入的边界用例分开报告，详见[中断与时间](interrupts-and-time.md)。
