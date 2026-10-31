# Cooperative kernel threads

The first M3 stage provides cooperative BSP threads. The boot context becomes
a permanent thread alongside eight fixed worker slots, all sharing the current
kernel page tables. Normal boot initializes only the bootstrap thread; it adds
no background tasks or changes to the framebuffer scene or input consumers.
Switches occur only on explicit yield or exit. Hardware timer interrupts keep
running but do not preempt threads.

## Scheduling and context

`core/thread.c` manages READY, RUNNING, BLOCKED, and EXITED states and selects the next
READY slot in circular order. Yield succeeds immediately if none is available.
Scheduling mutations and stack switches run with local interrupts disabled.
A resumed yield restores its saved IF; a new thread restores the IF captured
at creation before calling its entry. Create, yield, exit, and reap require
ordinary thread context with IF set, rejecting scheduling in maskable IRQs or
interrupt-disabled critical sections. This is not an SMP lock; NMI/exception
handlers must not call these operations.

The continuation interface in `scene/arch.h` forms the architecture boundary.
Core code does not interpret its storage. The x86-64 implementation lives in
`arch/x86_64/context.c/.asm`. A switch at a C call boundary saves RSP, RBX, RBP,
R12–R15, and the return address on the stack; the compiler handles caller-saved
registers normally. RET enters the new C trampoline with `RSP + 8` aligned to
16 bytes, following the
[x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex).
FPU/SIMD, TLS, CR3, and user-mode state are not switched. Threads must not use
floating-point or SIMD operations.

## Stacks and lifetime

The stack region is `[0xffffc10000000000, 0xffffc10000050000)`. Each slot reserves
40 KiB: an unmapped guard page on each side of a 32 KiB kernel stack, backed by
fresh zeroed, writable, NX pages. This range is exclusive by convention until
the VM layer implements reservations; other modules must not map it. The
bootstrap thread keeps its existing stack and owns none of these pages.

Creation publishes an ID only after all stack mappings and the initial
continuation succeed. Failure unmaps and frees this attempt's completed stack
pages and preserves the output ID. IDs start at 1 and increase monotonically;
0 is invalid. Exhaustion rejects creation rather than recycling old IDs.
Slots can be reused without letting stale IDs refer to new threads.

Returning from the entry is equivalent to explicit exit. Exit marks the thread
EXITED and switches to another READY thread; it cannot free the stack it is
using. Another thread must reap it to unmap/free stack pages and release the
slot. Unreaped exits still consume capacity. The caller owns the context
argument and must retain it until the entry finishes. Bootstrap cannot exit
or be reaped, so a runnable return context always exists.

The paging layer still retains empty intermediate tables. First use may consume
table pages; subsequent create/reap cycles must stop reducing the physical free
count. Stack data pages are returned each time. Automatic reaping, blocking
join, sleep, priorities, and forced termination remain future work.
The input queue still permits one consumer; threads do not make it or the heap
SMP-safe.

## Wait queues and input wakeup

Zero-initialize `scene_wait_queue` and retain it and the condition context until
all waiters finish. Never copy or reset a live queue. Workers call
`scene_thread_wait(queue, condition, context)`. The condition runs with IF clear
and must be brief, nonblocking, and must not enable interrupts. Checking it and
linking the waiter share one interrupt-disabled interval, preventing an IRQ
from being lost between those operations. An already satisfied condition
returns immediately; otherwise the thread becomes BLOCKED and switches out.

`wake_one` unblocks the FIFO head; `wake_all` unblocks all current waiters. Both
may run in ordinary or maskable IRQ context, preserve IF, and mark READY without
switching stacks in the handler. Execution still follows scheduler slot order;
batch wakeup does not promise execution order. A resumed waiter rechecks its
condition and rejoins the tail if false. Notification does not imply resource
availability. Update the condition before waking; state also modified by IRQs
needs the same interrupt exclusion. NMI and SMP use are outside the contract.

Bootstrap cannot block and must keep yielding, leaving a runnable context even
when all workers wait. Timeouts, cancellation, sleep, and a dedicated idle
thread are not provided; normal boot still creates no background consumer.
`scene_input_wait(event)` is an optional blocking API for the sole input
consumer. Its condition pops with IF clear, immediately consuming queued data
or waiting until a successful IRQ1/IRQ12 enqueue wakes it. Nonblocking pop,
decoders, raw event format, and capacity remain unchanged. Text translation
stays in the consumer after wait returns, outside IRQs. This is not event
fan-out to multiple consumers.

## Validation

```powershell
python tools/scenectl.py smoke --threads --timeout 15
python tools/scenectl.py smoke --threads --pic --timeout 15
python tools/scenectl.py smoke --threads --tsc-clock --timeout 15
python tools/scenectl.py smoke --preset release --threads --timeout 15
python tools/scenectl.py smoke --thread-guard --timeout 15
python tools/scenectl.py smoke --preset release --thread-guard --timeout 15
python tools/scenectl.py smoke --thread-wait --timeout 15
python tools/scenectl.py smoke --thread-wait --pic --timeout 15
python tools/scenectl.py smoke --preset release --thread-wait --timeout 15
```

The thread test runs three rounds of eight workers. It checks circular order,
distinct register/stack canaries, ABI alignment, PIT progress during execution,
rejection with IF clear, exhausted capacity, explicit/automatic exit, unreaped
slots, stale IDs, and zeroing on reuse. Occupying the fourth stack page forces
creation to roll back the first three mappings. After reap, every data page
must be unmapped, and later rounds must match the first round's warmed physical
free count. The assembly test lives in the architecture directory and is linked
only for focused builds.

`--thread-guard` writes the lower guard from a real worker, requiring vector 14,
CR2 `0xffffc10000000000`, and a supervisor write to a non-present page. This case
has no `Bootstrap complete` marker and does not validate every stack-overflow
or double-fault path. Normal smoke rejects unexpected exceptions. All
focused modes are mutually exclusive and disabled in ordinary builds.

`--thread-wait` checks FIFO order, spurious wake rechecks, an already satisfied
condition, wake-all, IF preservation, invalid queues, and bootstrap rejection.
A real input worker then consumes one buffered synthetic event. Bootstrap
emits each Monitor injection marker only after observing the worker BLOCKED.
Five hardware groups cover key press/release, motion, scrolling, and buttons,
for seven ordered events including the buffered one, with no loss. Bootstrap
never wakes that consumer directly; delivery must pass through input IRQs.

Next add wait timeouts/cancellation, an idle loop, and explicit non-preemptible regions before integrating
timer preemption. Ring 3 also needs full interrupt-state handling, TSS RSP0
switching, address spaces, and system calls. This stage supplies software stack
switching and wait/wakeup foundations.
