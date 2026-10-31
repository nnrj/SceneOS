# 输入核心与 PS/2 设备

输入核心把设备原始字节与使用者分离。`input.c` 维护一个含 64 个槽位的
`scene_input_event` 环形队列。键盘 IRQ1 与鼠标 IRQ12 在 BSP 上形成一个串行化
的中断生产上下文，启动期内核代码是消费者。为区分满与空会保留一个槽位，
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

共享控制器层启用 i8042 双端口，并校验命令响应是否来自指定设备。鼠标恢复默认
配置并开启流模式，在 IRQ12 中组装有同步标志的三字节包，丢弃溢出包，把 PS/2
向上为正的 Y 转换成屏幕坐标向下为正，并发布相对位移以及左、右、中键的当前与
变化状态。滚轮协商和四字节包尚未实现。

执行 `python tools/scenectl.py smoke --keyboard --timeout 10` 会构建自测内核，
并通过 QEMU Monitor 注入 `A`。测试通过表示模拟设备、IRQ1 路由、解码器和队列
这一整条链路可用。

执行 `python tools/scenectl.py smoke --mouse --timeout 10` 可注入相对移动，验证
控制器第二端口、IRQ12 路由、数据包解码和指针事件整条链路。

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
