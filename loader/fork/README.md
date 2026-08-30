# fork, vfork and posix_spawn (WP-42)

Work in progress.

Cygwin's `fork` copies a process by creating a suspended child of the same
image and writing the parent's memory into it. Everything the loader keeps in
its own writable data crosses on that copy. Everything the loader mapped for
itself does not, because the copier only replays what it recorded. This package
is the record, the lock that makes the record consistent at the moment it is
taken, and the child-side replay.

Contents arrive with the commits that follow.
