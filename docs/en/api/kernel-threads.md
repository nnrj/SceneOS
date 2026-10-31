# Kernel thread interfaces

`scene/thread.h` provides pre-stable kernel-internal APIs for cooperative BSP
threads sharing one kernel address space. Initialization, create, yield, wait, exit,
and reap require ordinary context with IF set. Invalid calls return false;
invalid exit calls panic. NMI/exception handlers must not call these operations.

| Interface | Contract |
| --- | --- |
| `scene_thread_init()` | Call once after owning page tables and enabling interrupts; bootstrap receives ID 1 with no worker stack allocation; repeat calls fail |
| `scene_thread_create(entry, context, id)` | Create a READY worker, up to eight; null entry/output, capacity, or allocation failure return false without changing the output ID; context may be null and remains caller-owned |
| `scene_thread_yield()` | Select the next READY thread; resume at the original call when scheduled again; no other candidate still returns true; before init or with IF clear returns false |
| `scene_thread_exit()` | Never returns; workers only; returning from entry exits automatically; mark EXITED while retaining stack and slot |
| `scene_thread_reap(id)` | Release another EXITED worker's stack pages and slot; bootstrap, running/ready/blocked threads, invalid or already reaped IDs fail |
| `scene_thread_get_info(id, info)` | Copy a snapshot; invalid ID, null output, or missing init fail without changing output; preserve caller IF |
| `scene_thread_current()` | Current thread ID, or 0 before initialization |
| `scene_thread_count()` | Allocated records, including bootstrap and unreaped exits; 0 before initialization |
| `scene_thread_wait(queue, condition, context)` | Workers only; check/enqueue atomically with IF clear and return true only when the condition succeeds; recheck after waking and block again if false |
| `scene_wait_queue_wake_one(queue)` | Unblock the FIFO head as READY; return true on success, false for empty/invalid queues or missing init; maskable IRQ calls are allowed and never switch inline |
| `scene_wait_queue_wake_all(queue)` | Unblock all current waiters and return the count, or 0 for invalid/empty queues; same context rules as wake-one |
| `scene_wait_queue_pending(queue)` | Count BLOCKED waiters, or 0 before init/for invalid queues; already READY waiters are excluded |

`scene_thread_id` is a monotonically increasing 64-bit ID; 0 is invalid. Slot
reuse never reuses an old ID; exhaustion rejects further creation.
`scene_thread_info` contains `id`, READY/RUNNING/BLOCKED/EXITED `state`, `stack_bottom`,
and exclusive `stack_top`. Worker stacks are 32 KiB with unmapped guards on
both sides. Both bounds are 0 for bootstrap, whose stack is externally owned.
A snapshot does not extend lifetime. Callers must not use its addresses to
modify another thread's stack or treat it as a stable user-space ABI.

Queries and wake operations never schedule, may run with IF clear, and preserve
IF. Other mutations require IF set. Initialize `scene_wait_queue` with `{0}`;
never modify its private fields or copy/reset a live queue. Keep the queue,
condition function, and context valid until every wait call returns, even when
pending reaches 0. Conditions run with interrupts disabled, must not block or
enable interrupts, and may atomically acquire a resource on true. Update state
before waking; IRQ-shared conditions need the same interrupt exclusion.
Bootstrap cannot wait and must keep running/yielding to let READY workers run.
Timeouts and cancellation are not provided. Entry code and context contents must
remain valid until the thread finishes. Shared data relies on the cooperative
BSP model; yield is not an SMP memory barrier. Floating-point/SIMD, TLS,
user address-space switching, preemption, sleep/join, and priorities are
unsupported. See [implementation and validation](../development/kernel-threads.md).
