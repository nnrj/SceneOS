# Cooperative kernel threads

The first M3 stage provides cooperative BSP threads. The boot context becomes
a permanent thread alongside eight fixed worker slots, all sharing the current
kernel page tables. Normal boot initializes only the bootstrap thread; it adds
no background tasks or changes to the framebuffer scene or input consumers.
Switches occur only on explicit yield or exit. Hardware timer interrupts keep
running but do not preempt threads.

## Scheduling and context

`core/thread.c` manages READY, RUNNING, and EXITED states and selects the next
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
join, wait queues, sleep, priorities, and forced termination remain future work.
The input queue still permits one consumer; threads do not make it or the heap
SMP-safe.

## Validation

```powershell
python tools/scenectl.py smoke --threads --timeout 15
python tools/scenectl.py smoke --threads --pic --timeout 15
python tools/scenectl.py smoke --threads --tsc-clock --timeout 15
python tools/scenectl.py smoke --preset release --threads --timeout 15
python tools/scenectl.py smoke --thread-guard --timeout 15
python tools/scenectl.py smoke --preset release --thread-guard --timeout 15
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
or double-fault path. Normal smoke rejects unexpected exceptions. The two
focused modes are mutually exclusive and disabled in ordinary builds.

Next add waiting/wakeup and explicit non-preemptible regions before integrating
timer preemption. Ring 3 also needs full interrupt-state handling, TSS RSP0
switching, address spaces, and system calls. This stage supplies software stack
switching only.
