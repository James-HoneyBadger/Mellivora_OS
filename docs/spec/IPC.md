# Interprocess communication (normative)

**Normative from:** M1 for sections, M5 for ports. **Governs:** how two
processes exchange data and how one process waits for another, with a security
model, a quota model and a way to see what is blocked.

Today a process can share memory through a named table, and can talk over a
socket to another process on loopback. Everything else — a request with a reply,
a completion notification, a wait on another process's exit — is built out of
signals and polling. This document replaces that.

## 1. The four primitives

| Primitive | Shape | Used for |
|---|---|---|
| **Port** | A queue of messages plus a reply relation | Requests and replies, completions, a worker pool's work queue |
| **Section** | Shared pages with a name ([VMM.md](VMM.md#6-sections)) | Bulk data, a window's pixels, a mapped file |
| **Named object** | An event, mutant or semaphore in the namespace ([OBJECTS.md](OBJECTS.md#3-types)) | Signalling and mutual exclusion between processes |
| **Pipe** | A byte stream, anonymous or named | A shell pipeline, a service's output |

All four are objects with handles, rights, ACLs and wait semantics. There is no
fifth mechanism and there is no kernel-internal shortcut that a driver uses and a
program cannot.

## 2. Ports

A port is a message queue with an owner:

```text
mv_port_message
  u32 header_size, struct_size, version
  u32 type              /* what the reader should do with it */
  u32 status            /* a completion carries the operation's result */
  u32 sender_pid
  u32 sender_tid
  u32 reply_port        /* 0 for a one-way message */
  u32 body_length
  u8  body[]            /* at most 64 KiB inline */
  u32 handle_count
  u32 handles[]         /* handles transferred with the message */
```

| Call | Semantics |
|---|---|
| `PortCreate(queue_limit, acl)` | Creates a port; the limit is messages, not bytes, and is configurable per port (default 64, maximum 4 096) |
| `PortConnect(name, timeout)` | Client: finds a named port, gets a handle with `WRITE_DATA | SYNCHRONIZE` |
| `PortReceive(port, buffer, timeout, alertable)` | Dequeues one message; blocks, or returns `MV_PENDING` with a completion |
| `PortReply(message, status, body)` | Answers a message that carries a reply port |
| `PortSend(port, message, timeout)` | Enqueues; blocks when the queue is full, never drops |

Rules that make it usable:

1. **A message is either delivered or the send fails.** There is no "dropped
   because the queue was full" path: the sender waits, times out, or is refused
   by quota ([S6](README.md#decisions-this-set-records)).
2. **A reply is matched to its request** by the reply port, so two threads can
   share one port without confusing their answers.
3. **Large payloads do not go through the queue.** A message over 64 KiB is
   carried as a *section* plus the message: the sender creates the section, maps
   it, and the message carries the handle. The receiver maps it and closes the
   handle. This is the same mechanism as a window's surface, which is why the
   desktop does not need a second one.
4. **Handles travel.** A message may carry up to 32 handles, each duplicated into
   the receiver with **no more** rights than the sender held
   ([OBJECTS.md](OBJECTS.md#4-handles)); this is how a client hands a file to a
   service.
5. **The receiver can identify the sender.** `sender_pid`/`sender_tid` are
   kernel-written and not settable. A receiver that needs the sender's identity
   for an access check asks the kernel for the sender's token, which is the
   only impersonation path ([SECURITY.md](SECURITY.md#22-tokens)).

### 2.1 Completion ports

A **completion port** is a port with a concurrency policy: a number of worker
threads may be associated with it, and the kernel hands each queued completion to
one of them, up to the concurrency limit, so a pool of workers scales with the
machine instead of with the queue.

- Used by: overlapped I/O ([DRIVERS.md](DRIVERS.md#5-io-requests)), job
  completion ([SCHED.md](SCHED.md#7-jobs)), and any program that wants a worker
  pool.
- A packet carries the operation's status, the bytes transferred and the
  caller's context word, so the worker does not need to look anything up.

## 3. Sections

Sections are specified in [VMM.md](VMM.md#6-sections). What matters here:

| Rule | Contract |
|---|---|
| Naming | `\Sessions\<n>\BaseNamedObjects\<name>` for a user's sections, `\Sessions\<n>\BaseNamedObjects\Global\<name>` with the permission to create there |
| Lifetime | A section lives while a handle is open; an unnamed section lives while it is mapped |
| Protection | A view's protection is the view's, not the section's; a read-only view of a writable section is normal and expected |
| Size | Up to 2^47 bytes; a section larger than physical RAM is committed lazily and backed by the pagefile |
| Cross-session | Refused without `SE_SESSION_CROSS`, which is how a session cannot read another session's window pixels |

## 4. Named objects

Events, mutants and semaphores live in the namespace and are opened by name;
`port`, `section`, `event` and `mutant` are the whole vocabulary a program needs
to build a producer/consumer, a rendezvous or a lock across processes.

| Object | Cross-process guarantee |
|---|---|
| Event | Manual-reset or auto-reset; a wait on an auto-reset event consumes one signal ([OBJECTS.md](OBJECTS.md#6-waiting)) |
| Mutant | Owned by a thread, recursive, abandoned on thread death; **the next waiter is told the previous owner died** and the object is left unowned |
| Semaphore | Counted; a wait decrements, a release increments, a release beyond the maximum is refused |

## 5. Pipes

| Kind | Creation | Semantics |
|---|---|---|
| Anonymous | `PipeCreate` returns two handles, inheritable | Byte stream, no name, closed when both handles close |
| Named | A name under `\Sessions\<n>\BaseNamedObjects\Pipes` | Server creates the first instance; clients open; a message-mode instance preserves record boundaries |

A pipe is a port with a stream API, not a second implementation: the same queue
limits, the same ACLs, the same wait rules. A shell pipeline is anonymous pipes
between the shell's children ([SHELL.md](SHELL.md#2-the-shell-namespace)).

## 6. Deadlock and fairness

| Rule | Why |
|---|---|
| A server may not hold a mutex while waiting for a reply it has not answered | The classic four-way deadlock |
| A reply is never required for a one-way message | A completion has no reply port |
| A port's queue is FIFO; a receive that has waited longest wins | So a busy client cannot starve a quiet one |
| A blocking send that would exceed the queue waits with a timeout, and a timeout is a status the caller handles | Silent loss is not an option ([S6](README.md#decisions-this-set-records)) |
| Two ports may be waited on together with `WaitAny` ([OBJECTS.md](OBJECTS.md#6-waiting)), so a server does not need a thread per client | Thread count follows load, not connections |
| Every wait is **alertable-capable**, so a server thread can be cancelled without killing it | Shutdown ordering ([SERVICES.md](SERVICES.md#12-lifecycle)) |

## 7. Quotas and backpressure

| Limit | Default | Enforced |
|---|---|---|
| Queued messages per port | 64 (max 4 096) | `PortSend` waits, then times out |
| Inline message size | 64 KiB | Refused above it; use a section |
| Handles per message | 32 | Refused |
| Ports per process | 4 096 | Create fails with `MV_NO_MEMORY` |
| Sections per process | 65 536 | Create fails |

A port charges its queue's memory to its owner, so a client cannot make a server
run out of memory by sending messages the server has not read
([SCHED.md](SCHED.md#7-jobs)).

## 8. Observability

`ipc` lists every port: owner, queue depth, peak depth, messages sent and
received, senders waiting. `ipc <port>` lists the waiting senders and the oldest
unanswered message. A machine with a stuck service can therefore be diagnosed
from the outside, which is the difference between a support ticket and a fix.

## 9. Conformance

M1 (sections) and M5 (ports) do not close until:

1. `ipc_test` sends 100 000 messages through one port from 8 threads to 8
   threads, with no loss, no duplication and FIFO order per sender.
2. A queue at its limit blocks the sender, honours the timeout, and never drops.
3. A 1 MiB message travels as a section, both sides map it, and both see the same
   bytes after a write.
4. A handle sent with a message arrives with no more rights than the sender held,
   and a duplicate of it in the receiver cannot be widened.
5. A mutant abandoned by a killed thread hands ownership to the next waiter with
   the abandoned status.
6. A server waiting on two ports with `WaitAny` serves both, and a cancelled
   receive leaves the port consistent.
7. Cross-session access without `SE_SESSION_CROSS` is refused, naming the session.

## 10. As built (the gap)

| Contract | Usable today |
|---|---|
| Ports and completions | None; sockets on loopback are the only request/reply |
| Sections by name | A shared-memory table with a name, no ACL, no session scope |
| Named events/mutants/semaphores | Message queues and semaphores exist as syscalls without a namespace |
| Pipes | Anonymous pipes exist |
| Wait-many, alertable waits | `poll` on sockets only |
| Quotas and backpressure | A queue limit per object, not per owner |

The socket, pipe, message-queue and shared-memory mechanisms are real and tested;
they become types in this model rather than separate mechanisms.
