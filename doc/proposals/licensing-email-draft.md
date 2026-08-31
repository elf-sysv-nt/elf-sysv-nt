# Draft: linking-exception question for the Cygwin list

Retired unsent, 2026-08-30. DR-0037 answered the question this asked by
adopting the accepted-practice reading — MSYS2 and Git for Windows already
distribute on it — so there is nothing left to ask upstream. Kept because the
two textual doubts it raises are the sharpest statement of the
counter-reading, and whoever briefs counsel should start from them.

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
believe that part is settled. For the full picture: the tree also vendors
glibc 2.28's installed headers byte-identical (LGPL-2.1-or-later, notices
intact) and carries export and import inventories cut from Cygwin 3.6.10,
though neither bears on the question below.

The open question is the linking exception. As published at
https://cygwin.com/licensing.html, it permits linking libcygwin.a, crt0.o and
gcrt0.o with independent modules and conveying the resulting executable under
terms of your choice, an independent module being one not itself based on the
Cygwin library.

My understanding going in was that a modified version of the library remains a
beneficiary: under GPLv3 section 7 an additional permission travels with the
work unless a conveyor removes it, and nothing in the exception's text
terminates it on modification. The project only makes sense on that reading.
It has since been brought into question on two textual points. First, the
grant names libcygwin.a, crt0.o and gcrt0.o, and a re-faced rebuild ships
artifacts under other names; whether those words mean the Cygwin library in
whatever form it takes downstream, or those artifacts alone, changes
everything. Second, section 7 lets a conveyor remove additional permissions
but add them only on material of the conveyor's own copyright, so if the
original grant does not stretch to the renamed artifacts, I cannot repair the
gap with wording of my own.

That matters here because the exception is what lets GPLv2-only programs link
Cygwin at all, and running a GPLv2-only userland is the point of the project.
Without an equivalent exception on the derived library, those programs could
link cygwin1.dll but not this one.

Three questions:

1. Does the exception, as the copyright holder intends it, follow a modified
   version of the library — so that executables linking a re-faced rebuild
   are conveyed under it the way executables linking cygwin1.dll are?

2. If it does not follow automatically, has Red Hat ever granted a derived
   work permission to carry the exception forward, or is there a process for
   requesting it?

3. If this is a question for Red Hat legal rather than this list, whom should
   I contact?

Until this is answered the repository grants no exception of its own and says
so, so that nobody relies on wording an engineer invented.

Thanks,

Philip Dye
