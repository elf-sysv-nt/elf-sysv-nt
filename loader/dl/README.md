WP-38 — the dl surface (work in progress)

This package delivers the `<dlfcn.h>` surface an ELF program expects from its
loader — `dlopen`, `dlsym`, `dlvsym`, `dlclose`, `dlerror`, `dladdr`,
`dladdr1`, `dlinfo`, `dl_iterate_phdr` — over the packages already delivered:
WP-31's parser, WP-32's mapper, WP-33's object graph, WP-34's relocation,
WP-35's symbol lookup, WP-36's version matcher, WP-37's TLS, and WP-39's
rendezvous.

It also owns initialization order: `DT_PREINIT_ARRAY` for the root, then
dependencies before dependents through `DT_INIT` and `DT_INIT_ARRAY`, and the
exact reverse on the way out.

This file is a placeholder; the account of the design lands with the code.
