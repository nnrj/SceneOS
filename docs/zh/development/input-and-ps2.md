# 输入核心与 PS/2 设备

输入核心把设备原始字节与使用者分离。`input.c` 维护一个含 64 个槽位的
`scene_input_event` 环形队列。键盘 IRQ1 与鼠标 IRQ12 在 BSP 上形成一个串行化
的中断生产上下文，启动期代码或专用工作线程是唯一消费者。为区分满与空会保留一个槽位，
因此最多积压 63 个事件。溢出时丢弃新事件并增加可查询的计数器。

首版假定可屏蔽中断 handler 不嵌套、单消费者、单 BSP 和 x86 有序内存模型。
虽然两个设备都会发布事件，中断门会把其生产过程串行化。它不是可直接用于 SMP
的通用队列；加入其他 CPU、生产者或用户态消费者之前，必须补充明确的原子内存
序，以及事件扇出或焦点路由策略。

i8042 驱动在配置期间禁用第一端口、清理陈旧输出，启用第一端口中断与翻译、
开启键盘扫描，然后通过核心 IRQ 注册表附着 IRQ1。所有轮询都有上限，失败时
退出初始化而不会永久卡死。中断 handler 每次最多读取 16 个键盘字节，跳过辅助
设备数据，解码常用 Set 1 与扩展键，维护左右修饰键和 Caps Lock，并发布按下、
释放与重复事件。

驱动刻意不生成字符。`core/text_input.c` 在普通消费者上下文按显式 US 布局把
语义按下事件转换成 `scene_text_event`，复制来源序列号和重复标志。字母使用
Shift/Caps 异或规则，数字及标点只受 Shift 影响；Ctrl/Alt 组合、释放和编辑/
导航键不生成文本。转换只读事件自带的修饰键快照，不维护另一份按键状态，
不在 IRQ 中运行，也不向原始环形队列增加生产者。文本结果归消费者持有。

当前布局覆盖 26 个字母、10 个数字、US 标点和空格，输出为 Unicode 标量中的
ASCII 可打印子集。独立的 UTF-8 编码器支持 1–4 字节标量编码，拒绝代理码点、
越界值和容量不足且不部分写入。它遵循
[RFC 3629](https://www.rfc-editor.org/rfc/rfc3629)，不进行规范化、UTF-8 解码或
输入法处理。Unicode 组合、死键、AltGr、更多布局和输入法仍属于后续文本服务。
PS/2 当前仍不支持 Pause/Print Screen 特殊序列、LED、热插拔恢复或多键盘。

共享控制器层启用 i8042 双端口，并校验命令响应是否来自指定设备。命令及数据
轮询只用于 IF 关闭的启动初始化；IF 开启时调用会失败，避免与 IRQ 抢读响应。
这不是完整运行期事务接口，调用方不能仅通过手动关闭 IF 就把它用于运行期。
启动期仍会丢弃来自另一端口的陈旧输出，不提供并发事务或热插拔恢复。

鼠标先恢复默认配置并停流，再按 200、100、80 的采样率序列探测 IntelliMouse，
最后查询设备 ID：0 使用三字节，3 使用四字节。流程及字段依据
[Microsoft 的 PS/2 鼠标协议说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/keyboard-and-mouse-hid-client-drivers#activating-buttons-4-5-and-wheel-on-ps2-mice)。
每个命令和参数都要求 ACK；控制器保留最多三次 RESEND 尝试。未知 ID、超时或
命令失败会使初始化失败，不猜测包长。只在协议确定、IRQ12 注册后开启流模式。

独立协议解码器跨 IRQ 保存部分包，只在包头检查同步位，丢弃溢出包并保留之前的
按键状态。X/Y 按包头符号位和数据字节组成的九位值解析，Y 转为向下为正。
第四字节为完整八位有符号滚轮值，转换后 `wheel_delta` 正值向上、负值向下，
单位为设备滚轮步数。左、右、中键当前值和变化掩码保持原契约。无变化包计入
接收包数但不入队；队列满仍更新设备当前按钮状态，消费者应结合丢失计数恢复。
五键 Explorer ID 4、水平滚轮、绝对坐标和丢字节后的完整重同步仍未实现。

执行 `python tools/scenectl.py smoke --keyboard --timeout 10` 会构建自测内核，
并通过 QEMU Monitor 注入 `A`。测试通过表示模拟设备、IRQ1 路由、解码器和队列
这一整条链路可用。

执行 `python tools/scenectl.py smoke --mouse --timeout 10` 可注入相对移动，验证
控制器第二端口、IRQ12 路由、数据包解码和指针事件整条链路。

滚轮和三字节兼容验收：

```powershell
python tools/scenectl.py smoke --mouse-wheel --timeout 15
python tools/scenectl.py smoke --mouse-wheel --pic --timeout 15
python tools/scenectl.py smoke --mouse-basic --timeout 15
python tools/scenectl.py smoke --mouse-basic --pic --timeout 15
python tools/scenectl.py smoke --preset release --mouse-wheel --timeout 15
```

两种自测都验证协商成功/失败、ID 0 回退、未知 ID、全部九位 X/Y 和八位滚轮值、
按键转换、部分包及溢出包。随后按就绪标记注入双向移动、三键按下/释放；滚轮
模式再注入双向滚动，分别要求 7 / 9 个事件、正确字段和零丢失。HMP `mouse_move`
的 dz 正值表示向上，与 PS/2 线上的滚轮字节方向相反，参见
[QEMU HMP 实现](https://github.com/qemu/qemu/blob/master/ui/ui-hmp-cmds.c)。
`--mouse-basic` 跳过协商且要求查询结果为 ID 0，用于全新 QEMU 的兼容路径，
不会把已经处于四字节模式的设备强行当作三字节。无滚轮设备拒绝切换的回退由
脚本化传输测试覆盖；这不等于真机验证。普通构建默认探测滚轮，关闭所有自测。

文本路径验收：

```powershell
python tools/scenectl.py smoke --text-input --timeout 15
python tools/scenectl.py smoke --text-input --pic --timeout 15
python tools/scenectl.py smoke --text-input --tsc-clock --timeout 15
python tools/scenectl.py smoke --preset release --text-input --timeout 15
```

独立自测编译单元先检查全部字母与修饰键组合、符号/Caps、重复、非文本按键及
无效参数不修改输出，再检查 UTF-8 的长度边界、中文/补充平面值、代理码点和
短缓冲区。随后通过 [QEMU Monitor](https://www.qemu.org/docs/master/system/monitor.html)
依次注入 15 组按键，包括左右 Shift、Caps Lock 开关、Ctrl/Alt 组合、空格、
回车、退格和扩展方向键。每组必须完成全部按下/释放，再发布下一组等待标记；
脚本按完整串口行匹配标记后发送下一条命令，避免步骤 1 与 10 误匹配。

测试要求最终文本为 `aAAa!b Z`，恰好八个文本事件、来源序列正确、原始事件
有序且无丢失。IRQ/设备驱动仍只发布原始事件；测试作为消费者调用转换器。
未达到标记、内核故障、进程提前退出或超时均失败，并清理本次启动的 QEMU。
普通构建不启用该测试，也尚未提供文本框或终端的可见输入反馈。

消费者也可在工作线程中使用 `scene_input_wait()`。它通过等待队列原子检查队列
或阻塞，成功的输入 IRQ 入队将等待者标记 READY；中断本身不切栈。原有 pop 及
单消费者限制保留。`smoke --thread-wait --timeout 15` 验证线程先 BLOCKED 再
由键盘/鼠标 IRQ 唤醒，详见[协作式内核线程](kernel-threads.md)。
