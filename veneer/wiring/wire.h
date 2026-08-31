/* The wiring bind table (WP-56).
 *
 * Each slice's generated table names, per wired symbol, the elfsysv1.dll
 * export its body reaches. The fn slot starts null and is filled once at
 * load by __esn_wire_bind through a resolver the runtime supplies; the
 * generated thunks tail-jump through the filled slot. Keeping the resolver
 * a callback keeps this file free of any Windows header, so the table and
 * the bind loop certify under a host compiler with a fake resolver.
 */
#ifndef ESN_WIRE_H
#define ESN_WIRE_H

#include <stddef.h>

/* The tables are DSO-internal; hidden visibility keeps a table access a
 * plain rip-relative load. PE compilers for the host-side certification
 * have no such attribute, so it compiles away there. */
#if defined(__ELF__)
#define ESN_WIRE_LOCAL __attribute__((visibility("hidden")))
#else
#define ESN_WIRE_LOCAL
#endif

struct esn_wire_ent {
    const char *export_name;   /* elfsysv1.dll export to reach */
    void *fn;                  /* filled by __esn_wire_bind; null until then */
};

/* Returns the export's address, or null if the name is not exported. */
typedef void *(*esn_wire_resolver)(const char *export_name, void *ctx);

/* Fill every fn slot; returns the count of names the resolver refused.
 * A refused slot stays null. Idempotent: an already-filled slot is
 * re-resolved, so a rebind after a runtime reload is a plain re-run. */
size_t __esn_wire_bind(struct esn_wire_ent *tab, size_t n,
                       esn_wire_resolver resolve, void *ctx);

#endif
