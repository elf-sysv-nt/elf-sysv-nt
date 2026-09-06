elf-sysv-nt host probes, source only
====================================

This directory holds the C sources of a set of small measurement programs,
a script that compiles them, and a script that runs them. Nothing in it is
executable as delivered. You build the programs on this machine from the
sources here, run them, and send back the text files they write.

What they do
------------

Each probe asks Windows a few questions about one kernel facility (memory
placement, thread-local storage slots, I/O into uncommitted buffers, timer
resolution, exception dispatch, the Windows Hypervisor Platform) and prints
the answers as key=value lines. They read; they do not change the machine.
No registry writes, no services, no drivers, no installation, nothing
outside this directory and one scratch directory the file-system probe
creates and removes. They run as an ordinary user. Several of the WHP
probes will report that the hypervisor platform is absent or refused; that
is an answer, not a failure.

Three things the probes do that a security product might notice: they
allocate and release memory with placeholder flags, they create and delete
a virtual-machine partition through the documented WinHvPlatform API when
the platform is present, and one of them clones its own process with the
documented RtlCloneUserProcess call. The whp-corp-probe.ps1 script is a
read-only PowerShell reading of the hypervisor policy surface; it is safe to
show an IT reviewer first.

What you need
-------------

A mingw-w64 GCC for x86_64. A zip of winlibs (https://winlibs.com, the
UCRT or MSVCRT x86_64 build, "without LLVM" is fine) unpacked anywhere in
your profile is enough; no installer, no admin. Cygwin, MSYS2 or Visual
Studio are not needed, and MSVC's cl.exe will not build these.

How to run it
-------------

Open a command prompt in this directory and:

    set PATH=C:\path\to\mingw64\bin;%PATH%
    build.cmd
    run.cmd

build.cmd compiles every probe into bin\ and says which, if any, did not
build. run.cmd runs each one that built and writes out\<probe>.txt, then
runs the PowerShell policy probe into out\whp-corp.txt. The whole run takes
a few minutes; the WHP probes are the slow part, and they are skipped
quickly when the platform is not there.

The file-system probe needs a scratch directory on a local NTFS volume
(not a redirected or roaming profile). By default it uses %TEMP%; to use
another place:

    run.cmd D:\scratch\elfsysvnt

When it finishes, zip the out\ directory and send it back. The .err files
beside the .txt files hold anything a probe wrote to stderr; include them.

If something refuses to run
---------------------------

An application-control policy may refuse an unsigned executable outside
Program Files. Moving the directory to an allowed path is the usual answer.
An endpoint product objecting to a probe will show up as an .err file or a
missing .txt; send what you have, with a note of what happened. Nothing
here needs to be retried with elevation.
