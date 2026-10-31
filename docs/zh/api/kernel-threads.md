# 内核线程接口

`scene/thread.h` 提供尚未稳定的内核内部 API，仅用于单 BSP、同一内核地址空间
的协作式线程。创建、yield、wait、exit、reap 和初始化必须在普通上下文且 IF 开启时
调用；其他状态返回 false，exit 的无效调用则 panic。NMI/异常处理器不得调用。

| 接口 | 契约 |
| --- | --- |
| `scene_thread_init()` | 页表已由内核接管、已启用中断后调用一次；启动线程 ID 为 1，不分配工作栈；重复调用失败 |
| `scene_thread_create(entry, context, id)` | 创建 READY 工作线程，最多 8 个；空入口/输出、满容量或分配失败返回 false 且不修改输出 ID；context 可为空，由调用方持有 |
| `scene_thread_yield()` | 主动轮转到下一个 READY 线程，返回时继续原调用；没有其他候选者也返回 true；初始化前或 IF 关闭时返回 false |
| `scene_thread_exit()` | 不返回；只允许工作线程调用，入口自然返回也会退出；标记 EXITED 并保留栈和槽位 |
| `scene_thread_reap(id)` | 只回收其他已 EXITED 工作线程，释放栈页和槽位；启动线程、运行/就绪/阻塞线程、无效或已回收 ID 均失败 |
| `scene_thread_get_info(id, info)` | 复制当前快照；无效 ID、空输出或未初始化时失败且不修改输出；保留调用时 IF |
| `scene_thread_current()` | 当前线程 ID，初始化前为 0 |
| `scene_thread_count()` | 已分配记录数，包含启动线程和退出未回收线程，初始化前为 0 |
| `scene_thread_wait(queue, condition, context)` | 仅工作线程可等待；条件检查与入队在 IF 关闭时原子完成，条件为真才返回 true；唤醒后会重检，条件仍为假就再次 BLOCKED |
| `scene_wait_queue_wake_one(queue)` | FIFO 解除一个等待，标记 READY；成功返回 true，空/无效队列或未初始化返回 false；可在可屏蔽 IRQ 中调用，不立即切换线程 |
| `scene_wait_queue_wake_all(queue)` | 解除所有当前等待并返回数量；无效/空队列返回 0；与 wake-one 相同的上下文规则 |
| `scene_wait_queue_pending(queue)` | 返回队列中的 BLOCKED 数量，未初始化或无效队列为 0；已经 READY 的等待者不计入 |

`scene_thread_id` 为 64 位单调 ID，0 无效；复用槽位不会复用旧 ID，计数耗尽后
拒绝继续创建。`scene_thread_info` 包含 `id`、READY/RUNNING/BLOCKED/EXITED `state`、
`stack_bottom` 和不包含的 `stack_top`。工作栈为 32 KiB，前后保护页不映射；
启动栈不归此模块管理，因此快照的两个界限均为 0。快照不会延长线程生命周期，
调用方不得借其地址修改其他线程栈或把它当作稳定用户态 ABI。

查询与唤醒不会调度，可在 IF 关闭时调用，保留原 IF；其他修改接口要求 IF 开启。
`scene_wait_queue` 用 `{0}` 初始化，内部字段不可修改，活动队列不可复制或重置。
队列、条件函数及上下文在所有等待调用返回前必须保持有效，即使 pending 已为 0。
条件函数在关中断时执行，不得阻塞或开启中断；可在返回 true 时原子取得资源。
条件状态应先更新再唤醒；条件与 IRQ 共享时需用同一中断保护。启动线程不能 wait，
必须继续运行/yield，以便被唤醒的工作线程获得执行机会。没有超时或取消 API。
入口函数与 context 指向的内容必须保持有效直至线程结束。数据共享只适用于
当前 BSP 协作模型，不能把 yield 当作 SMP 内存屏障。浮点/SIMD、TLS、用户
地址空间切换、抢占、sleep/join 和优先级尚无支持。详见
[实现与验收](../development/kernel-threads.md)。
