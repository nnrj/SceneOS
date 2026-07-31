# 硬件中断与早期时间基准

## 启动顺序

异常 IDT 安装完成后，架构层先把 8259A 重映射到向量 32–47，并屏蔽全部 IRQ。
内存模块接管 CR3 后，中断层校验 Limine 提供的 ACPI RSDP，遍历 XSDT/RSDT，
校验 MADT checksum，并读取 Local APIC 地址、IOAPIC 地址和 Interrupt Source
Override。MMIO 页面映射到 SceneOS 专用高半地址，不向核心代码暴露物理寄存器。

Local APIC 启用时，初始化代码屏蔽其本地向量项和全部 IOAPIC redirection
entry，设置 spurious vector，再只开放 PIT 所在 GSI。Q35 的典型 MADT 把
ISA IRQ0 覆盖为 GSI 2；实现使用表内数据，不硬编码这个覆盖关系。若 RSDP 或
MADT 不可用，则保留 8259A 兼容路径。

## 中断入口

NASM 为 legacy IRQ0–15 生成向量 32–47 的入口，并另设向量 255 作为 APIC
spurious vector。公共入口保存通用寄存器、按 SysV ABI 对齐 C 调用栈，调用
分派器后恢复现场并执行 `iretq`。IRQ0 更新 64 位 tick，再按当前控制器发送
Local APIC EOI 或 8259A EOI。

目前只有 IRQ0 被开放；其余 IOAPIC 和 PIC 输入保持屏蔽，因此驱动不能自行
占用向量。后续通用 IRQ 注册层需要管理向量分配、共享中断、屏蔽、注销以及
并发安全。

## PIT 与 watchdog

PIT channel 0 以 rate-generator 模式配置为约 100 Hz。启动自测打开中断并
等待三个 tick；等待循环同时使用 TSC 的 20 亿周期上限作为启动 watchdog，
避免错误路由导致 QEMU 永久停在 `hlt`。当前 tick 只是早期可观测时间基准，
还不是纳秒级单调时钟。

下一步需要校准 TSC，评估 invariant TSC、HPET 与 Local APIC timer，定义统一
单调时间单位，并在其上实现无动态分配依赖的早期定时器队列。

## 自动验证

```powershell
python tools/scenectl.py smoke
python tools/scenectl.py smoke --page-fault
```

默认测试必须看到 `Interrupt controller: Local APIC + IOAPIC`、PIT GSI、
100 Hz 频率和至少三个硬件 tick。页故障测试也检查中断初始化已完成，确保
异常路径和 IRQ 路径可以共存。
