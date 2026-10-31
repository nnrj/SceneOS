# 内核线程接口

`scene/thread.h` 提供尚未稳定的内核内部 API，仅用于单 BSP、同一内核地址空间
的协作式线程。创建、yield、exit、reap 和初始化必须在普通上下文且 IF 开启时
调用；其他状态返回 false，exit 的无效调用则 panic。NMI/异常处理器不得调用。

| 接口 | 契约 |
| --- | --- |
| `scene_thread_init()` | 页表已由内核接管、已启用中断后调用一次；启动线程 ID 为 1，不分配工作栈；重复调用失败 |
| `scene_thread_create(entry, context, id)` | 创建 READY 工作线程，最多 8 个；空入口/输出、满容量或分配失败返回 false 且不修改输出 ID；context 可为空，由调用方持有 |
| `scene_thread_yield()` | 主动轮转到下一个 READY 线程，返回时继续原调用；没有其他候选者也返回 true；初始化前或 IF 关闭时返回 false |
| `scene_thread_exit()` | 不返回；只允许工作线程调用，入口自然返回也会退出；标记 EXITED 并保留栈和槽位 |
| `scene_thread_reap(id)` | 只回收其他已 EXITED 工作线程，释放栈页和槽位；启动线程、运行/就绪线程、无效或已回收 ID 均失败 |
| `scene_thread_get_info(id, info)` | 复制当前快照；无效 ID、空输出或未初始化时失败且不修改输出；保留调用时 IF |
| `scene_thread_current()` | 当前线程 ID，初始化前为 0 |
| `scene_thread_count()` | 已分配记录数，包含启动线程和退出未回收线程，初始化前为 0 |

`scene_thread_id` 为 64 位单调 ID，0 无效；复用槽位不会复用旧 ID，计数耗尽后
拒绝继续创建。`scene_thread_info` 包含 `id`、READY/RUNNING/EXITED `state`、
`stack_bottom` 和不包含的 `stack_top`。工作栈为 32 KiB，前后保护页不映射；
启动栈不归此模块管理，因此快照的两个界限均为 0。快照不会延长线程生命周期，
调用方不得借其地址修改其他线程栈或把它当作稳定用户态 ABI。

查询不会调度，可在 IF 关闭时读取；修改接口要求 IF 开启并自行保护临界区。
入口函数与 context 指向的内容必须保持有效直至线程结束。数据共享只适用于
当前 BSP 协作模型，不能把 yield 当作 SMP 内存屏障。浮点/SIMD、TLS、每线程
地址空间、抢占、sleep/wait/join 和优先级尚无支持。详见
[实现与验收](../development/kernel-threads.md)。
