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

核心层维护固定 16 项 legacy IRQ 注册表。驱动通过 `scene_irq_attach()` 注册
handler；只有架构路由成功后注册才生效。重复注册、越界 line 和不匹配的注销
会失败，`scene_irq_detach()` 在清除 handler 前重新屏蔽 IOAPIC/PIC 输入。

MADT 的全部 ISA Interrupt Source Override 会保存到每 line 的 GSI/flags，而非
只处理 IRQ0。PIT 现在使用同一注册接口附着 IRQ0；公共中断分派器只查表调用，
随后按当前控制器发送 EOI。目前不支持共享 IRQ，注册与注销也只允许在串行启动
阶段使用；并发安全和动态向量分配仍待实现。

## PIT 与 watchdog

PIT channel 0 以 rate-generator 模式配置为约 100 Hz。启动自测打开中断并
等待三个 tick；等待循环同时使用 TSC 的 20 亿周期上限作为启动 watchdog，
避免错误路由导致 QEMU 永久等待。

ACPI 遍历已从 APIC 驱动抽成共享查询层：校验 RSDP、XSDT/RSDT 与每张目标表的
checksum，并限制固件声明的长度。Q35 默认找到 64 位、100 MHz HPET，映射其
MMIO 页面、复位并启用主计数器，以它作为首选单调时钟。

没有合格 HPET 时，核心时钟等待 PIT tick 边界，再以八个 tick 的 TSC 差值校准
频率。`python tools/scenectl.py smoke --tsc-clock` 会强制走这条回退路径。QEMU
当前没有声明 invariant TSC，因此回退时钟不能保证跨 CPU、休眠、迁移或频率
状态变化时的精度。

两种来源使用同一纳秒换算：把商与余数分开计算，避免
`elapsed * 1,000,000,000` 直接溢出。时钟原点是来源初始化完成时刻，不是开机
或 Unix epoch。后续仍需使用 CPUID 频率信息交叉校验，并评估 Local APIC timer。

## 早期定时器队列

核心层使用容量 32 的固定小顶堆，以纳秒 deadline 和递增 ID 排序。接口支持
绝对/相对调度和取消；`scene_timer_run_due()` 在普通内核上下文中取出到期项
再调用回调，IRQ0 本身只更新 tick，不运行任意回调。当前队列仅限 BSP 串行
使用，尚未加锁或支持动态扩容。

启动自测安排 5 ms 和 10 ms 两个回调，取消 7.5 ms 项，再验证执行顺序、取消
语义与空队列。等待窗口由 PIT IRQ 提供，但到期判断只使用选定的单调时钟，
不会假定若干硬件 IRQ 等于精确的墙钟时长。

## 自动验证

```powershell
python tools/scenectl.py smoke
python tools/scenectl.py smoke --page-fault
```

默认测试必须看到 `Interrupt controller: Local APIC + IOAPIC`、PIT GSI、
IRQ 生命周期验证、单个已注册 handler、100 Hz 频率、至少三个硬件 tick、
`Clock source: HPET` 和定时器顺序/取消验证。
`smoke --tsc-clock` 要求 `Clock source: calibrated TSC`。页故障测试也检查这些
步骤已完成，确保异常路径和 IRQ/时间路径可以共存。
