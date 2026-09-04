# An ntdll-only process, created from Win32 and doing the kernel's work

Can a process whose only loaded module is `ntdll.dll` be created on this host,
and can it reach the executive services the kernel above it would lean on,
without anything dragging `kernel32` in behind its back? `native-child.c` is the
process under test, `host-probe.c` is the Win32 parent that creates it,
`measure.sh` builds both and writes `results-<date>.txt`, and the reading of
that transcript is under **The verdict** below. It ran on 2026-09-04:
`native-host-viable-afd-complete`. The AFD socket, left at its floor the first
time through, now carries a whole loopback connection; one class of question
about injection this host still cannot answer, and that limit is recorded as a
limit rather than a finding.

## Why it matters

Proposal 0011's `lk-host` is every Linux process, and section 3 fixes its shape:
a PE that imports only `ntdll`, never loads `kernel32`, never registers with
`csrss`. That shape is not an aesthetic choice. `fork` under the native
substrate is an executive clone of the address space, and a process that has
registered with `csrss` cannot be cloned, because the clone inherits a
registration the session manager never made for it and the two disagree about
who exists. So the kernel's host process has to be the kind of process `smss` or
`csrss` itself is, ntdll-only from birth.

Open question 3 is the doubt that shape raises. Endpoint products inject DLLs,
and an injected DLL imports `kernel32`; a shim engine or an AMSI provider does
the same. A process with no `kernel32` either loads it on demand, whereupon the
`csrss` registration the clone cannot survive happens anyway, or it refuses the
injection and something else breaks. The proposal records the whole design as
conditional on finding out which. This spike is that measurement, for the one
security configuration this machine actually runs.

## Method

Three probes, built with the mingw cross toolchain (`x86_64-w64-mingw32-gcc`
14.4.0) as configured, no MSVC needed. `host-probe.exe` is an ordinary Win32
process: it imports `kernel32`, and it is the stand-in for the supervisor that
would create real `lk-host` processes. It asks the executive to create the
child with `NtCreateUserProcess`, building the process parameters with
`RtlCreateProcessParametersEx` and pointing the image-name attribute at the NT
path.

The child is built twice from one source, differing only in the subsystem field
of the PE header: once `IMAGE_SUBSYSTEM_NATIVE`, once console. Both link no CRT
and import only `ntdll`; `objdump -p` confirms it before every run. The child
does the inside-the-process questions itself, because they can only be
answered from inside an ntdll-only process: it walks the PEB loader list (q6),
runs `RtlWaitOnAddress` against `RtlWakeAddressSingle` across two threads under
a timeout (q4), drives a whole AFD socket through its life (q3a-q3f), and
creates and connects an ALPC port (q5). The AFD part is the substantial one. It
opens two `\Device\Afd\Endpoint` handles through the `AfdOpenPacketXX` extended
attribute, binds one to `127.0.0.1:0` and reads the assigned port back, listens
on it, connects the second to it from a worker thread, accepts the connection
onto a third endpoint, sends eight bytes across and receives them, and reads
`IOCTL_AFD_POLL` at each edge for readiness. Then it re-walks the loader list
(q3f), so a module dragged in by the socket work would show. It reports through
two channels that need nothing but `ntdll`: a `key=value` file opened with
`NtCreateFile`, and an exit code whose low bits encode a summary the parent
reads even when the file does not survive.

The open packet's exact shape was read, not guessed. ReactOS's `msafd` and its
`afd` driver, System Informer's `phnt` header for the modern packet, and
`wepoll` for the poll IOCTL and its event bits were the reading guide; none was
copied. Two details this build insists on came out of that reading and a run of
one-hypothesis-at-a-time probes against the live driver: the endpoint is
created in TDI form with `\Device\Tcp` as the transport, and the connect carries
the modern `AFD_CONNECT_JOIN_INFO`, two endpoint handles ahead of the address.
The older layout `msafd` documents is refused here with
`STATUS_INVALID_PARAMETER`, which is what the first run mistook for a closed
door.

The parent then launches each image through `cmd.exe` with `CreateProcess`
(q2), and relaunches the native image twenty times to watch it survive the
machine's real security configuration (q7).

## The verdict

Yes, and the native subsystem is the shape that earns it. `results-2026-09-04.txt`,
taken on Windows 10.0.26200.9168 under Cygwin 3.6.10.

`NtCreateUserProcess` created the native-subsystem ntdll-only image and ran it
to a clean exit, `STATUS_SUCCESS` and an exit code with every capability bit
set. The loader list at steady state held two modules and no more: the image
itself and `ntdll.dll`. Nothing injected a module, so nothing dragged `kernel32`
in, and open question 3 comes back on the side the proposal needed. Under this
host's security configuration a process can be born ntdll-only and stay that
way.

The subsystem field is what decides it, not the import table, and that is the
distinction the spike was built to catch. The console-subsystem image carries
the identical ntdll-only import table, and it was created and ran just the same;
but its loader list came back with four modules, `KERNEL32.DLL` and
`KERNELBASE.dll` among them. The subsystem process initialization maps them
whatever the import table says. An ntdll-only import table is necessary and not
sufficient. `lk-host` must be `IMAGE_SUBSYSTEM_NATIVE`, and this is the evidence
for the requirement rather than the assertion of it.

`cmd.exe` cannot start the native image at all: it reports it as not a valid
Win32 application and exits non-zero. It runs the console image, which then maps
`kernel32` as above. Neither is a problem the design has, because the supervisor
creates `lk-host` with `NtCreateUserProcess` and no shell is ever in the path;
the measurement records the boundary rather than tripping over it.

The executive services the host reaches inside the ntdll-only process answer.
`RtlWaitOnAddress` and `RtlWakeAddressSingle` carried a wake from one thread to
another under a five-second guard that turns a hang into a failure rather than a
hang. An ALPC port was created and connected.

AFD is the one that grew. The endpoint no longer stops at the device open: two
were created for TCP over IPv4 (q3a), one bound to `127.0.0.1:0` with the kernel
handing back an ephemeral port the child reads through `IOCTL_AFD_GET_SOCK_NAME`
(q3b), and a loopback connection ran end to end inside the single process (q3c).
The listener started listening, a worker thread connected the second endpoint,
`IOCTL_AFD_WAIT_FOR_LISTEN` returned the pending connection, `IOCTL_AFD_ACCEPT`
grafted it onto a third endpoint, and eight bytes sent from the client came back
byte-for-byte on the accepted side. This is the claim proposal 0011 rested on an
unmeasured driver: that a socket can be built and driven from a process holding
nothing but `ntdll`.

`IOCTL_AFD_POLL` reports the readiness the design's `epoll` needs (q3d). The
freshly accepted endpoint polls not-ready for read before its peer sends; the
connected client polls ready for write; once the peer's bytes arrive the server
side polls ready for read. Poll issued repeatedly across that change is
consistent with level-triggered reporting (q3e): the read-ready poll returns the
same signalled bit each time while the eight bytes sit unread, and returns
not-ready again once the receive drains them. Nothing in a single poll marks the
transition itself, so a caller wanting an edge holds its own last-state and
compares; open question 8 asks exactly whether that is enough, and this spike
reports what the driver does without settling it. That decision is the
operator's.

The whole round trip happened without a module joining the loader list. The
re-walk after the socket work (q3f) found the same two modules the start-of-run
walk found, `kernel32` among neither. The socket path is `ntdll` down to the
syscall, and it stays that way.

Twenty launches, twenty clean exits, nothing crashed and nothing quarantined.

## What this does not reach

Recorded now rather than after the fact, so the limits are not mistaken for
findings.

The injection question is answered for Windows Defender and for nothing else.
This machine carries no third-party EDR, and that absence is itself part of the
finding: it means the clean two-module loader list is Defender leaving the
process alone, not a general proof that every endpoint product would. A box with
a real EDR agent is a different measurement, and the proposal's deployment note
already says an injected DLL is a reality to expect there. Rerun this spike on
such a box before trusting the result on it.

AFD past a loopback TCP stream. The socket that runs here is `AF_INET`,
`SOCK_STREAM`, `IPPROTO_TCP`, connected over `127.0.0.1` inside one process. No
IPv6, no UDP, no traffic that leaves the loopback interface, no second process
on the other end, and no scale past a single connection polled one handle at a
time. The connection is created in the driver's TDI form; the mswsock-style TLI
create with no transport name faulted here and is not the path this takes. And
the readiness measured is one poll at a time on a blocking handle, not the
`IOCP`-driven poll a real `epoll` emulation would run; `wepoll`'s pattern of one
endpoint-less `\Device\Afd` handle bound to a completion port is read about but
not built. Whether the level-triggered poll this spike records is the right
base for `EPOLLET` is open question 8, and it stays open.

A full ALPC round trip. The port was created and connected; the spike did not
run a message across it with the accept handshake, so `create` and `connect`
are proven and a request-reply exchange is not.

`NtCreateProcessEx` cloning, which is `fork`'s primitive and the subject of open
question 2. That is spike (a), `nt-clone-fork`, and nothing here measures it. And
one Windows build only: the answer belongs to the running NT kernel, and the
transcript records the build number for exactly that reason.
