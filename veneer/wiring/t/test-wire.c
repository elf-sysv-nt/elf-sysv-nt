/* Certify the bind loop and a generated table (WP-56).
 *
 * The generated wire-files.gen.c from the fixture forward map is compiled
 * beside this driver; a fake resolver stands in for GetProcAddress. The
 * expected picked rows, in fixture order:
 *   0 frob_open@GLIBC_2.2.5  -> frob_open
 *   1 frob_open@GLIBC_2.0    -> frob_open_old
 *   2 frob_alias             -> real_target
 *   3 frob_shim              -> frob_shim (shim, no target: keyed by symbol)
 *   4 frob_weak              -> frob_weak
 * frob_stub, frob_obj, other_fn and the scaffold row must not be picked.
 */
#include <assert.h>
#include <string.h>
#include "../wire.h"

extern struct esn_wire_ent __esn_wire_files[];
extern const unsigned long __esn_wire_files_n;

static int hit_open(void)   { return 11; }
static int hit_old(void)    { return 22; }
static int hit_target(void) { return 33; }
static int hit_weak(void)   { return 44; }

static void *fake_resolve(const char *name, void *ctx)
{
    (*(int *)ctx)++;
    if (!strcmp(name, "frob_open"))     return (void *)hit_open;
    if (!strcmp(name, "frob_open_old")) return (void *)hit_old;
    if (!strcmp(name, "real_target"))   return (void *)hit_target;
    if (!strcmp(name, "frob_weak"))     return (void *)hit_weak;
    return 0; /* frob_shim: unresolved, stays null */
}

int main(void)
{
    int calls = 0;
    assert(__esn_wire_files_n == 5);
    assert(!strcmp(__esn_wire_files[0].export_name, "frob_open"));
    assert(!strcmp(__esn_wire_files[1].export_name, "frob_open_old"));
    assert(!strcmp(__esn_wire_files[2].export_name, "real_target"));
    assert(!strcmp(__esn_wire_files[3].export_name, "frob_shim"));
    assert(!strcmp(__esn_wire_files[4].export_name, "frob_weak"));

    size_t missing = __esn_wire_bind(__esn_wire_files, __esn_wire_files_n,
                                     fake_resolve, &calls);
    assert(missing == 1);
    assert(calls == 5);
    assert(__esn_wire_files[3].fn == 0);

    int (*f)(void);
    f = (int (*)(void))__esn_wire_files[0].fn; assert(f() == 11);
    f = (int (*)(void))__esn_wire_files[1].fn; assert(f() == 22);
    f = (int (*)(void))__esn_wire_files[2].fn; assert(f() == 33);
    f = (int (*)(void))__esn_wire_files[4].fn; assert(f() == 44);

    /* Rebind is a plain re-run: slots refill, the missing one stays null. */
    missing = __esn_wire_bind(__esn_wire_files, __esn_wire_files_n,
                              fake_resolve, &calls);
    assert(missing == 1 && calls == 10);
    return 0;
}
