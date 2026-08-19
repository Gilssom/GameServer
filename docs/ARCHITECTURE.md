# Standalone Server Architecture

## Request path

```text
DummyClient
  -> TCP packet header(size, id)
  -> blocking accept on main thread
  -> accepted socket associated with IOCP
  -> overlapped WSARecv
  -> GetQueuedCompletionStatus worker
  -> per-session stream accumulation
  -> complete packet dispatch
  -> MemoryAuthStore or OdbcAuthStore
  -> overlapped WSASend
```

## IOCP ownership

`IocpServer` owns the completion port, listen socket, worker threads and active
session map. Every overlapped operation owns an `IoContext`. A completion takes
ownership of that context and either destroys it or reissues a partial send.

The completion key is the socket value. A worker resolves it through the active
session map, so a cancelled completion arriving after session removal cannot
dereference a released Session pointer.

## TCP stream framing

The four-byte little-endian header contains `uint16 size` and `uint16 packet_id`.
A Session maintains a pending byte vector because one receive may contain part of
a packet or several packets. Dispatch occurs only after `pending >= declared_size`.

Before payload deserialization the server rejects:

- declared size smaller than the header;
- declared size greater than 4096;
- an unknown packet id;
- malformed length-prefixed strings.

Only the offending Session is closed. IOCP workers and other sessions remain
active, which the final E2E login after six fault cases verifies.

## Authentication

The dependency-free test mode hashes an ephemeral environment-provided password
with PBKDF2-HMAC-SHA256 and verifies it using a constant-time byte comparison.
The ODBC mode prepares one parameterized query and binds AccountName separately;
the connection string is accepted only through `GAMESERVER_ODBC_CONNECTION`.

The public ODBC example serializes access to one connection. It intentionally
does not claim the connection-pool behavior measured in the historical Gate 3
and Gate 4 reports.

## Known next steps

- replace blocking `accept()` with AcceptEx;
- give ODBC work a dedicated queue so PBKDF2 and DB latency do not occupy IOCP workers;
- add a bounded send queue per Session for backpressure;
- run long-duration soak and multi-host tests.
