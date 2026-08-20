# Spike 3: the ABI crossing

Unrun. Placeholder; the method belongs here once someone writes it.

**Question.** Can one runtime entry point present a System V face over an
MS-ABI core, survive a signal delivered mid-call, and leave the red zone
intact?

**Gates.** The ABI boundary, which is to say `elfsysv1.dll` itself. A no sends
the program to the veneer-thunk fallback named in the breakdown, which is a
decision rather than a task.

Per `AGENTS.md`, this directory holds the script that measures it, its sources,
and the transcript the script produced, named by the date of the run.
