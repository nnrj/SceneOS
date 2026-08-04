# 输入事件接口

当前内核内部 API 声明在 `scene/input.h` 中。

- `scene_input_init()`：重置队列状态和序列号；输入驱动注册前调用一次。
- `scene_input_push_key(event)`：校验并加入语义按键事件；参数无效或溢出时返回
  false。
- `scene_input_push_pointer(event)`：加入相对移动或按键变化；空指针、无变化事件
  或队列溢出时返回 false。
- `scene_input_pop(event)`：移除最早事件；队列为空或目标为空时返回 false。
- `scene_input_pending()`：返回当前积压事件数。
- `scene_input_dropped()`：返回初始化以来因溢出丢弃的累计数量。

每个成功接收的事件都有单调递增 `sequence`、类型和按键 payload。payload 包含
与设备无关的 `scene_key_code`、修饰键位、`pressed` 和 `repeat`。释放事件的
`pressed` 为 false；`repeat` 只对按下事件有意义。键码表示物理按键语义，不是
文本或 Unicode。

指针 payload 包含有符号 `delta_x`、`delta_y`、`wheel_delta`，以及当前
`buttons` 和 `changed_buttons` 位掩码。X 正值表示向右，Y 正值表示向下；消费
者结合当前值和变化位即可区分按下与释放。滚轮增量是预留接口，对当前三字节
PS/2 鼠标始终为零。

PS/2 相关头文件暴露初始化接口和诊断用接收计数。应用与未来驱动应消费输入
事件，而不是依赖 PS/2 扫描码或数据包。这些接口尚未稳定，可与仓库内所有调用
者一同演进。
