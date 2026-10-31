# API 文档

API 文档面向驱动、系统服务和应用开发者。当前阶段只有启动和早期内核契约；
系统调用、驱动模型、GUI 协议和 libc 接口将在实现时加入。

- [启动协议契约](boot-contract.md)
- [中断与时间接口](interrupt-and-time.md)
- [输入事件接口](input-events.md)
- [内核线程接口](kernel-threads.md)
- [AI 操作协议草案](ai-operation-protocol.md)
- [运行库与动态依赖策略](runtime-linking.md)

稳定版之前 API 允许变化；调用方和双语文档必须随实现同步。
