# AI 操作协议草案

AI 服务 `aid` 是用户态系统服务，不属于内核。协议使用带版本的 JSON 消息，
通过 SceneOS IPC 传输。AI 不应默认提交任意 shell 字符串，而应调用结构化
action。

```json
{
  "protocol": "scene.ai/1",
  "request_id": "example-1",
  "action": "fs.copy",
  "arguments": {
    "source": "/home/user/a.txt",
    "destination": "/home/user/archive/a.txt"
  },
  "options": {
    "overwrite": false,
    "dry_run": true
  }
}
```

每个 action 必须定义：

- JSON Schema、版本和稳定性；
- 所需能力和资源范围；
- 参数、结果及机器可读错误；
- 幂等性、取消和超时语义；
- 是否需要用户确认；
- 审计记录中必须保留的字段。

建议的初始命名空间为 `fs.*`、`process.*`、`window.*`、`system.*` 和
`network.*`。执行网关独立于模型，模型不能批准自己的高风险操作。

