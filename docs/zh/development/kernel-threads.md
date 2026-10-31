# 协作式内核线程

M3 首阶段提供 BSP 上的协作式线程。启动上下文成为常驻线程，另有 8 个固定
工作槽位，所有线程共享当前内核页表。普通启动只初始化启动线程，不创建后台
任务，现有 framebuffer 场景和输入消费者行为不变。线程只有主动调用 yield 或
退出时才切换；硬件时钟中断继续运行，但尚未触发抢占。

## 调度与上下文

`core/thread.c` 管理 READY、RUNNING、EXITED 状态，按槽位循环挑选下一个 READY
线程。yield 没有候选者时直接成功返回。调度状态修改及切栈期间关闭本地中断；
恢复旧线程时恢复该次 yield 保存的 IF，新线程先恢复创建时的 IF，再调用入口。
创建、yield、退出及回收要求普通线程上下文且 IF 开启，拒绝从可屏蔽 IRQ 或已
关闭中断的临界区调度。这不是 SMP 锁，也不允许从 NMI/异常处理器调用。

架构边界由 `scene/arch.h` 的 continuation 接口提供。核心不解释保存区；x86-64
实现放在 `arch/x86_64/context.c/.asm`。切换位于 C 调用边界，保存 RSP、RBX、
RBP、R12–R15 与栈上的返回地址；调用者保存寄存器依照编译器的普通调用规则。
新栈经 RET 进入 C trampoline，入口满足 `RSP + 8` 的 16 字节对齐，依据
[x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex)。
当前不切换 FPU/SIMD、TLS、CR3 或用户态状态，线程禁止使用浮点/SIMD。

## 栈与生命周期

线程栈虚拟区为 `[0xffffc10000000000, 0xffffc10000050000)`。每个槽位保留 40 KiB：
前后各一页不映射的保护页，中间 32 KiB 为按需分配、清零、可写且 NX 的内核栈。
本阶段按约定独占该范围，正式虚拟地址保留区管理尚未实现；其他模块不得映射它。
启动线程继续使用既有栈，不拥有这类栈页。

创建只在全部栈页映射和初始 continuation 完成后发布 ID；失败会撤销本次已完成
的栈页映射并归还物理页，输出 ID 保持原样。ID 从 1 开始单调分配，0 无效，
耗尽后拒绝创建，不循环重用旧 ID。槽位可以复用，旧 ID 不能命中新线程。

入口函数返回等价于显式退出。退出标为 EXITED 并切到其他 READY 线程，不能在
正在使用的栈上释放自己。其他线程调用 reap 才撤销栈映射、归还物理页和释放槽位；
已退出未回收的线程仍占容量。上下文参数由调用方持有，至少保持到入口结束。
启动线程不能退出或回收，因此总有一个可运行的返回上下文。

当前分页层仍保留空中间页表：首次使用栈区域可能消耗中间表页，后续反复创建/
回收应保持物理页数量不再减少。这与每次正确归还栈数据页并不冲突。没有自动
回收器、阻塞 join、等待队列、sleep、优先级或强制终止；这些属于后续调度工作。
输入队列仍只允许一个消费者，创建线程不意味着队列或堆已经成为 SMP 安全接口。

## 验证

```powershell
python tools/scenectl.py smoke --threads --timeout 15
python tools/scenectl.py smoke --threads --pic --timeout 15
python tools/scenectl.py smoke --threads --tsc-clock --timeout 15
python tools/scenectl.py smoke --preset release --threads --timeout 15
python tools/scenectl.py smoke --thread-guard --timeout 15
python tools/scenectl.py smoke --preset release --thread-guard --timeout 15
```

线程自测分三轮运行，每轮填满 8 个工作槽位。它检查固定轮转顺序、每线程不同
的寄存器与栈哨兵、ABI 对齐、运行中 PIT 前进、IF 关闭时拒绝调度、容量耗尽、
显式/自动退出、未回收槽位、失效 ID 和复用后的清零。故意占用第四个栈页验证
前三页分配的失败回滚；回收后验证所有数据页不再映射，后两轮物理空闲页数量
与首轮预热后的基准相同。汇编验收位于架构目录，仅在专项构建中启用。

`--thread-guard` 在真实工作线程中写入下保护页，要求向量 14、CR2 为
`0xffffc10000000000`，且原因是 supervisor 对 non-present 页的写入。该用例
不会出现 `Bootstrap complete`，不能等同于所有栈溢出或双重故障路径都已验证。
普通 smoke 会拒绝意外异常。两个专项选项互斥，普通构建均关闭。

后续先补等待/唤醒与明确的不可抢占区，再接入时钟抢占；进入 Ring 3 时还必须
增加完整中断现场、TSS RSP0 切换、地址空间和系统调用。当前仅完成软件切栈基础。
