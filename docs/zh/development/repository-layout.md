# 仓库结构

```text
boot/                 Limine 配置和以后使用的启动资源
cmake/toolchains/     主机无关的交叉编译工具链描述
docs/zh, docs/en      同级维护的双语文档
kernel/include/scene  内核公开的模块间接口
kernel/src/arch       架构相关实现
kernel/src/boot       启动协议适配
kernel/src/core       内核基础设施
kernel/src/graphics   当前 framebuffer 验证层
kernel/src/tests      按自测构建选项启用的内核边界测试
kernel/linker         架构链接脚本
third_party/          固定来源和许可证的第三方内容
tools/                构建、镜像、诊断和 bootstrap 编排
```

`build/` 和 `.scene-sdk/` 不进入版本控制。NNOS 保持在相邻仓库中作为只读参考，
不得把其构建产物或整个源目录复制进 SceneOS。

