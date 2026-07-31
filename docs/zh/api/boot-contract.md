# 启动协议契约

SceneOS 当前通过 Limine protocol base revision 6 启动。内核请求：

- bootloader 名称和版本；
- HHDM 偏移；
- ACPI RSDP；
- 内存映射；
- 首个 framebuffer。

必需条件只有受支持的 Limine base revision。framebuffer 和 RSDP 是可选资源：
framebuffer 缺失时内核记录日志并继续 headless；RSDP 缺失或校验失败时不能
启用 APIC 路径，将保留 8259A 兼容路径。内存映射中的 `USABLE` 区域由物理页
分配器按区域接管。

链接入口为 `kmain`，内核链接到 `0xffffffff80000000`。Limine 请求放在独立
只读 PT_LOAD 段；代码、只读数据和可写数据使用不同权限段。

启动适配器必须把第三方协议结构转换为 SceneOS 自有结构。除启动模块外，
内核代码不得直接依赖 `limine.h`，以便以后升级协议或增加其他启动方式。
