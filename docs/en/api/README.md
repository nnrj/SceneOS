# API documentation

This documentation targets driver, system-service, and application developers.
Only boot and early kernel contracts exist today. System calls, the driver
model, GUI protocol, and libc interfaces will be added with their
implementations.

- [Boot contract](boot-contract.md)
- [Interrupt and time interfaces](interrupt-and-time.md)
- [Input event interfaces](input-events.md)
- [Kernel thread interfaces](kernel-threads.md)
- [AI operation protocol draft](ai-operation-protocol.md)
- [Runtime linking and dependency policy](runtime-linking.md)

Before the stable release, APIs may change, but callers and both documentation
languages must change with the implementation.
