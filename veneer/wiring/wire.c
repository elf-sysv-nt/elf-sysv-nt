/* The one bind loop every slice shares (WP-56). See wire.h. */
#include "wire.h"

size_t __esn_wire_bind(struct esn_wire_ent *tab, size_t n,
                       esn_wire_resolver resolve, void *ctx)
{
    size_t missing = 0;
    for (size_t i = 0; i < n; i++) {
        void *fn = resolve(tab[i].export_name, ctx);
        tab[i].fn = fn;
        if (!fn)
            missing++;
    }
    return missing;
}
