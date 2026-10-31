# 输入事件接口

当前内核内部 API 声明在 `scene/input.h` 中。

- `scene_input_init()`：重置队列状态和序列号；输入驱动注册前调用一次。
- `scene_input_push_key(event)`：校验并加入语义按键事件；参数无效或溢出时返回
  false。
- `scene_input_push_pointer(event)`：加入相对移动、滚轮或按键变化；空指针、无变化事件
  或队列溢出时返回 false。
- `scene_input_pop(event)`：移除最早事件；队列为空或目标为空时返回 false。
- `scene_input_wait(event)`：单一消费者的阻塞替代接口，只能在已初始化、IF 开启
  的工作线程中调用；原子尝试 pop 或等待输入 IRQ 唤醒。无效上下文/空目标返回
  false 且不修改输出，成功返回一个原始事件；无超时、取消或多消费者分发。
- `scene_input_pending()`：返回当前积压事件数。
- `scene_input_dropped()`：返回初始化以来因溢出丢弃的累计数量。

每个成功接收的事件都有单调递增 `sequence`、类型和对应 payload。按键 payload 包含
与设备无关的 `scene_key_code`、修饰键位、`pressed` 和 `repeat`。释放事件的
`pressed` 为 false；`repeat` 只对按下事件有意义。键码表示物理按键语义，不是
文本或 Unicode。

指针 payload 包含有符号 `delta_x`、`delta_y`、`wheel_delta`，以及当前
`buttons` 和 `changed_buttons` 位掩码。X 正值表示向右，Y 正值表示向下；消费
者结合当前值和变化位即可区分按下与释放。`wheel_delta` 的单位为设备滚轮步数，
正值向上、负值向下，不是像素或文本行数；三字节鼠标始终为零，IntelliMouse
ID 3 发布四字节包中的滚轮增量。缩放、加速度和滚动策略由消费者决定。

PS/2 相关头文件暴露初始化接口和诊断用接收计数。应用与未来驱动应消费输入
事件，而不是依赖 PS/2 扫描码或数据包。这些接口尚未稳定，可与仓库内所有调用
者一同演进。

`scene_ps2_mouse_init()` 仅用于 IF 关闭的启动阶段，重复初始化或已启用中断时
返回 false。它探测滚轮并选择实际包长；未知 ID 或通信失败不降级猜测。
`scene_ps2_mouse_has_wheel()` 在成功初始化后说明是否为 ID 3，初始化前为 false。
`scene_ps2_mouse_packets()` 统计完整且未溢出的包，包括未入队的无变化包。
`scene_ps2_device_command()` 和 `scene_ps2_device_read(device, value)` 同样要求
IF 关闭；错误设备、空读取目标或超时会失败，失败读取不修改输出。它们不是可在
运行期并发调用的事务 API。队列丢失时按钮快照仍更新，变化掩码不补发丢失事件。

## 按键到文本

`scene/text_input.h` 定义消费者侧的转换接口。调用方先取得原始输入事件，处理
快捷键/编辑动作，再按需要派生文本。PS/2 驱动和 `scene_input_event` 的类型及
队列容量均不变；文本由调用方持有，不回写原始队列，也不占用新的队列槽位。

- `scene_text_input_translate(layout, input, text)`：按显式布局将一个按下事件
  转换为至多一个文本事件，成功返回 `true`。当前仅支持
  `SCENE_KEYBOARD_LAYOUT_US`。没有可生成文本、参数无效或布局不支持时返回
  `false`，不修改输出。函数不消费队列、不改变输入、不保存修饰键状态。
- `scene_text_input_encode_utf8(codepoint, buffer, capacity)`：将一个 Unicode
  标量编码成最短的 1–4 字节 UTF-8，并返回字节数。无效标量、空缓冲区或容量
  不足返回零，整个缓冲区保持原样。不会自动添加 NUL 终止符。

`scene_text_event` 包含：

| 字段 | 含义 |
| --- | --- |
| `source_sequence` | 原始输入事件的 `sequence` 原样复制，用于关联，不是另一个事件序列 |
| `codepoint` | 一个 Unicode 标量；首版 US 布局只产生 ASCII 可打印字符 |
| `repeat` | 原始按下事件的重复标志，不另行产生重复计时 |

布局规则：字母的大小写由 Shift 与 Caps Lock 异或决定；数字、标点只受 Shift
影响，空格始终为空格。重复按下可生成文本；释放、修饰键、功能键、方向键、
Escape、Enter、Tab、Backspace 和 Delete 均保留为原始按键动作。含 Ctrl 或 Alt
的组合不生成文本，也不转换成控制字符；未知修饰键位同样不生成文本。
左右修饰键由驱动合并后的事件快照决定，转换不重新读取全局按键状态。

编码器按 [RFC 3629 §3](https://www.rfc-editor.org/rfc/rfc3629#section-3) 接受
U+0000–U+10FFFF，排除 U+D800–U+DFFF 代理码点。U+0000 会输出一个零字节；
调用方必须使用返回长度，不能把所有输出都视为 NUL 结尾字符串。编码器可处理
中文或补充平面标量，不表示已具备中文输入法、字体显示、UTF-8 解码或组合文本。

普通启动场景尚无文本消费者界面；自动测试消费并验证文本，但没有新增可交互
终端或文本框。布局选择、焦点、死键、AltGr、输入法和跨进程分发留给后续输入服务。
