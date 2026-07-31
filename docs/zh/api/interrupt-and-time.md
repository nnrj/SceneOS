# 中断与时间接口

这些接口目前是内核模块接口，不是稳定的驱动 ABI。

## 中断控制

- `scene_arch_interrupts_init()`：安装 IRQ IDT entry、重映射并屏蔽 8259A。
- `scene_arch_interrupts_activate_apic(boot_info)`：从 ACPI MADT 发现并启用
  Local APIC/IOAPIC。
- `scene_arch_interrupts_enable()` / `disable()`：控制当前 CPU 的 IF。
- `scene_arch_interrupt_controller()`：返回当前控制器名称，用于诊断。

调用顺序固定为：异常 IDT、PIC 兼容初始化、分页接管、APIC 激活、设备定时器
初始化，最后才允许 `sti`。当前实现仅支持 BSP 和单一 IOAPIC 的早期场景。

## 时间接口

- `scene_timer_init(frequency_hz)`：配置 PIT channel 0 并路由 IRQ0。
- `scene_timer_frequency()`：返回除数实际对应的频率，不保证等于请求值。
- `scene_timer_ticks()`：返回启动后收到的 tick 数。
- `scene_timer_wait_ticks(count)`：等待相对 tick 数；watchdog 超时返回
  `false`。

`scene_timer_ticks()` 当前依赖 x86-64 对齐 64 位读写。它还没有 SMP 同步保证，
不能作为稳定 wall clock、超时安全来源或用户态 ABI。驱动也不得直接调用
`scene_timer_handle_tick()`，该入口只属于 IRQ 分派器。
