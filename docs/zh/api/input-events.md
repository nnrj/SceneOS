# 输入事件接口

当前内核内部 API 声明在 `scene/input.h` 中。

- `scene_input_init()`：重置队列状态和序列号；输入驱动注册前调用一次。
- `scene_input_push_key(event)`：校验并加入语义按键事件；参数无效或溢出时返回
  false。
- `scene_input_pop(event)`：移除最早事件；队列为空或目标为空时返回 false。
- `scene_input_pending()`：返回当前积压事件数。
- `scene_input_dropped()`：返回初始化以来因溢出丢弃的累计数量。

每个成功接收的事件都有单调递增 `sequence`、类型和按键 payload。payload 包含
与设备无关的 `scene_key_code`、修饰键位、`pressed` 和 `repeat`。释放事件的
`pressed` 为 false；`repeat` 只对按下事件有意义。键码表示物理按键语义，不是
文本或 Unicode。

`scene/ps2_keyboard.h` 暴露初始化接口和诊断用已接收字节计数。应用与未来驱动
应消费输入事件，而不是依赖 PS/2 扫描码。这些接口尚未稳定，可与仓库内所有
调用者一同演进。
