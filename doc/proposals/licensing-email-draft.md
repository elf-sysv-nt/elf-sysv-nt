# Draft: linking-exception question for the Cygwin list

Drafted 2026-08-30 against open item 1 of `licensing-issue.md`. Send to
cygwin@cygwin.com; not yet sent.

---

To: cygwin@cygwin.com
Subject: Linking exception for a modified Cygwin library — can it be carried forward?

Hello,

I maintain elf-sysv-nt (https://github.com/elf-sysv-nt/elf-sysv-nt), a project
that rebuilds the Cygwin API library with a different export face: ELF and
System V outward, NT inward, so that a Linux userland can run on the Cygwin
runtime largely unchanged. Nothing is asked of you to review there; the
question is about licensing.

The repository is LGPLv3-or-later, on the reasoning that a re-faced rebuild of
winsup is a modified version of the Cygwin library and inherits its licence. I
believe that part is settled.

The open question is the linking exception. As published at
https://cygwin.com/licensing.html, it permits linking libcygwin.a, crt0.o and
gcrt0.o with independent modules, and defines an independent module as one not
itself based on the Cygwin library. A modified version of the library is
therefore not a beneficiary of the exception, and my reading is that receiving
code under terms that carry an additional permission (GPLv3 section 7) does not
by itself confer the right to re-grant that permission to my own users.

That matters here because the exception is what lets GPLv2-only programs link
Cygwin at all, and running a GPLv2-only userland is the point of the project.
Without an equivalent exception on the derived library, those programs could
link cygwin1.dll but not this one.

Three questions:

1. Is my reading correct that the exception, as written, neither covers a
   modified Cygwin library nor authorizes it to extend an equivalent exception
   to its own users?

2. Has Red Hat, as copyright holder, ever granted permission for a derived
   work to carry the exception forward, or is there a process for requesting
   it?

3. If this is a question for Red Hat legal rather than this list, whom should
   I contact?

Until this is answered the repository grants no exception of its own and says
so, so that nobody relies on wording an engineer invented.

Thanks,
Philip Dye
