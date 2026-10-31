# 中断与时间接口

这些接口目前是内核模块接口，不是稳定的驱动 ABI。

## 中断控制

- `scene_arch_interrupts_init()`：安装 IRQ IDT entry、重映射并屏蔽 8259A。
- `scene_arch_interrupts_activate_apic(boot_info)`：从 ACPI MADT 发现并启用
  Local APIC/IOAPIC。
- `scene_arch_interrupts_enable()` / `disable()`：控制当前 CPU 的 IF。
- `scene_arch_interrupts_save()`：保存调用前的中断状态并关闭本地可屏蔽中断；
  返回值是交给 `scene_arch_interrupts_restore(state)` 的不透明 token。
- `scene_arch_interrupts_restore(state)`：只恢复该 token 中的中断开关状态；
  同 CPU、栈式配对，不是完整寄存器恢复或 SMP 锁。
- `scene_arch_interrupts_enabled()`：查询当前 CPU 的中断开关。
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

## 本地硬件定时器

头文件为 `scene/local_timer.h`。当前仅支持 BSP 的 xAPIC 评估路径；调用限于
单 CPU 串行的普通内核上下文，不是稳定 ABI，也不接受用户回调。

- `scene_local_timer_init(calibration_ticks)`：时钟与 PIT 就绪、当前 IF 开启时，
  用 2–16 个 PIT tick 校准一次；优先以 HPET 计量经过时间，否则使用 PIT。
  未激活 APIC、重复初始化、参数或测量无效、等待超时均返回 `false`。
- `scene_local_timer_ready()`：成功校准后返回 `true`；停止不会取消校准状态。
- `scene_local_timer_start_one_shot(delay_ns)`：延迟向上取整到非零硬件计数；
  零延迟、装载值超过 32 位等无效请求返回 `false`。
- `scene_local_timer_start_periodic(frequency_hz)`：按最近整数装载值启动周期
  模式；频率必须非零且不超过分频后的计数频率，实际速率可能与请求值不同。
- `scene_local_timer_stop()`：停止倒计数、屏蔽中断并置停止态；可重复调用。
- `scene_local_timer_get_info(out)`：复制一致的状态快照；未校准或空指针返回
  `false` 且不修改目标。快照归调用方持有。

两种启动调用都要求已校准、处于停止态且没有旧中断等待处理；失败不会替换活动
定时器。停止前已产生的中断仍可能送达，停止态不增加事件计数，但照常 EOI。
若因此暂不能重新启动，应恢复中断交付后重试，不要在关闭 IF 时无界重试。单次
中断被接受后模式自动回到 `SCENE_LOCAL_TIMER_STOPPED`。配置、停止和快照读取
均保持调用方原有 IF 状态；这些局部临界区不提供 SMP/NMI 安全保证。

`scene_local_timer_info` 字段：

| 字段 | 含义 |
| --- | --- |
| `counter_frequency_hz` | 校准得到的 16 分频后倒计数速率，范围 1–UINT32_MAX Hz |
| `interrupts` | 累计接受的活动态中断数；不含停止后送达的旧事件，也不在重启时清零 |
| `reload_count` | 最近一次成功启动的装载值；停止后保留，不是实时剩余值 |
| `mode` | `SCENE_LOCAL_TIMER_STOPPED`、`ONE_SHOT` 或 `PERIODIC`（同一枚举前缀） |
| `calibrated_with_hpet` | `true` 表示 HPET 参考，`false` 表示 PIT 参考 |

事件计数不是单调时钟，周期中断也不能保证记录每次硬件到期（屏蔽中断会合并
待处理事件）。这里不调度软件队列、不替换 `scene_timer_ticks()`，不承诺跨休眠
或跨 CPU 的速率稳定性。

`scene_arch_local_timer_*` 是架构适配层：`available` 查询硬件路径；`pending`
检查待处理/在服务中的事件；`configure` 编程倒计数；`current_count` 读取剩余
计数；`stop` 停止硬件。配置和停止由本地定时器核心在关闭本地中断时调用，外部
驱动不得绕过核心修改硬件与模式状态。架构入口调用内部 `on_interrupt` 后统一 EOI。

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
