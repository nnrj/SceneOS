# 实现路线图

路线图描述顺序，不承诺日期。每个里程碑必须有可自动验证的完成条件。

## M0：可复现启动骨架（已完成）

- C17 + NASM 的 x86-64 freestanding 工程。
- CMake Presets + Ninja。
- 固定版本 Limine，兼容 BIOS/UEFI 启动镜像。
- 串口日志、panic、boot info、内存映射摘要。
- framebuffer 启动场景；无 framebuffer 时继续 headless。
- Windows/Linux 包装脚本、工具诊断和项目本地 bootstrap。
- QEMU UEFI 虚拟 FAT 冒烟测试；Windows 无 ISO 工具也可运行。

## M1：异常与内存（进行中）

- GDT、IDT、异常入口和寄存器现场。（已完成初版）
- 区域位图物理页分配、释放与复用。（已完成初版）
- SceneOS 自有 CR3、四级页表映射/取消映射与高半内核堆。（已完成初版）
- 页故障诊断、地址翻译和跨页堆测试。（已完成初版）
- TSS、Ring 0 栈与关键异常 IST。（已完成初版）
- 可拆分、合并、释放和复用的内核堆。（已完成初版）
- 中间页表回收、保留区登记、连续页分配、堆并发与 slab 分配。

## M2：时间、中断与输入（进行中）

- PIC 兼容启动、ACPI MADT 探测与 Local APIC/IOAPIC 迁移。（已完成初版）
- PIT 100 Hz 硬件 tick 与中断 watchdog。（已完成初版）
- 单调时间换算、时钟源校准与定时器队列。
- PS/2 键盘鼠标与统一输入事件。

## M3：线程、进程与用户态

- 软件上下文切换、抢占调度、等待队列。
- Ring 3、每进程地址空间、系统调用和 IPC。
- ELF64 用户程序、initramfs、基础 libc。

## M4：存储与 Unix 命令行

- 块设备、ATA PIO 验证驱动，后续 AHCI/virtio。
- VFS、FAT32 读写、文件描述符和路径语义。
- Shell、管道、重定向和基础 Unix 工具。

## M5：一等桌面

- 用户态窗口服务器、32 位 ARGB 合成器和窗口协议。
- Unicode 字体、输入法、剪贴板、拖放、DPI 与主题。
- 任务栏、开始入口、文件管理器、设置和终端。

## M6 以后

- PCI、ACPI、SMP、USB、音频和现代存储。
- 网络栈及 virtio-net/e1000。
- AI 服务、结构化操作协议、权限和审计。
- 动态链接、应用包、并列运行库。
- 稳定 ABI、自举工具链、旧程序持续兼容。
- GPU 加速、3D 合成与 VR Shell。
