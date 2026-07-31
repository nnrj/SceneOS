# AI operation protocol draft

The `aid` AI service is a user-space system service, not part of the kernel.
The protocol uses versioned JSON messages over SceneOS IPC. Models should call
structured actions rather than submit arbitrary shell text by default.

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

Every action defines:

- JSON Schema, version, and stability;
- required capabilities and resource scope;
- arguments, results, and machine-readable errors;
- idempotency, cancellation, and timeout semantics;
- whether user confirmation is required;
- fields retained in the audit record.

Initial namespaces are expected to include `fs.*`, `process.*`, `window.*`,
`system.*`, and `network.*`. The execution gateway is independent of the
model, and a model cannot approve its own high-risk operation.

