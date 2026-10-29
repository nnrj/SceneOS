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

## IRQ 注册

- `scene_irq_attach(irq, handler, context)`：注册独占 handler，并在架构层开放
  对应输入；路由失败时回滚注册。
- `scene_irq_detach(irq, handler, context)`：要求 handler/context 完全匹配，
  先屏蔽输入再清除注册。
- `scene_irq_dispatch(irq)`：由架构中断入口调用；没有 handler 时返回 `false`。
- `scene_irq_registered_count()`：返回当前独占注册数量。

当前 `SCENE_IRQ_LINE_COUNT` 为 16，只表示 legacy ISA IRQ line，不是任意 IDT
vector 或 GSI。一个 line 只能注册一个 handler；接口尚未提供共享 IRQ、优先级、
并发注册或用户态可见能力。驱动不得自行编辑 IDT、IOAPIC redirection entry 或
发送 EOI。

## 时间接口

- `scene_timer_init(frequency_hz)`：配置 PIT channel 0 并路由 IRQ0。
- `scene_timer_frequency()`：返回除数实际对应的频率，不保证等于请求值。
- `scene_timer_ticks()`：返回启动后收到的 tick 数。
- `scene_timer_wait_ticks(count)`：等待相对 tick 数；watchdog 超时返回
  `false`。

`scene_timer_ticks()` 当前依赖 x86-64 对齐 64 位读写。它还没有 SMP 同步保证，
不能作为稳定 wall clock、超时安全来源或用户态 ABI。PIT 的 tick handler 是
定时器模块通过 IRQ 注册层安装的内部实现，驱动不得直接调用。

## 单调时钟

- `scene_clock_init(boot_info, calibration_ticks)`：优先初始化 ACPI HPET，并用
  PIT tick 测量 TSC 用于诊断；HPET 不可用时使用该实测频率回退。
- `scene_clock_ready()`：报告所选时钟源是否就绪。
- `scene_clock_source_name()`：返回 `HPET` 或 `calibrated TSC`。
- `scene_clock_source_stable()`：HPET 返回 `true`；TSC 取决于 CPU 是否声明
  invariant TSC。
- `scene_clock_source_frequency_hz()`：返回当前来源频率。
- `scene_clock_monotonic_ns()`：返回从校准原点开始的纳秒数。
- `scene_clock_get_diagnostics(out)`：复制本次初始化的诊断快照；时钟未就绪或
  参数为空时返回 `false`，不写入目标。
- `scene_clock_tsc_check_name()`：返回当前 TSC 交叉校验状态的诊断字符串。

`scene_clock_diagnostics` 包含 `tsc_reported_hz`、`tsc_measured_hz`、
`tsc_deviation_ppm`、`tsc_invariant` 和 `tsc_check`。声明频率来自架构层的
`scene_arch_timestamp_counter_frequency_hz()`；缺失 CPUID.15H 或必需字段时
为零，实测频率为零表示 PIT 测量失败。快照由调用方持有，不暴露内部可变状态。

`tsc_check` 区分 `UNAVAILABLE`（无声明）、`INVALID`（非零频率超出
1 MHz–10 GHz）、`UNMEASURED`（有合格声明但无测量）、`MATCH` 和 `MISMATCH`。
枚举前缀均为 `SCENE_CLOCK_TSC_`。偏差字段仅对后两者有意义；其值向下取整，
但分类在取整前比较。容差由 `SCENE_CLOCK_TSC_TOLERANCE_PPM` 定义为 50,000
ppm（5%），以声明频率为基准。该状态只用于诊断，不改变时钟选择。

返回值不代表 wall clock，也不含时区或日期。当前只接受 64 位 HPET 主计数器；
非 invariant TSC 上只保证当前 BSP 运行期间读数不倒退，不保证长期速率准确。
`scene_clock_source_stable()` 的 invariant 判定与频率交叉校验是不同维度，
不能用一个布尔值替代完整诊断。HPET 可用时，TSC 测量失败不会使初始化失败。

## 定时器队列

- `scene_timer_schedule_at(deadline_ns, callback, context, id)`：按绝对单调
  deadline 调度。
- `scene_timer_schedule_after(delay_ns, callback, context, id)`：按相对延迟
  调度。
- `scene_timer_cancel(id)`：取消仍在队列中的项目。
- `scene_timer_run_due()`：在调用方上下文运行所有已到期回调并返回数量。
- `scene_timer_pending()`：返回等待项目数。

容量由 `SCENE_TIMER_QUEUE_CAPACITY` 固定为 32。相同 deadline 按递增 timer ID
稳定排序。队列尚不支持并发调用；回调允许继续调度项目，但不得假定自己运行
在硬件中断上下文。
