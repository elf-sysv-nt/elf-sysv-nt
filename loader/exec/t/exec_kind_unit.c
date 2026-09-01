/*
 * WP-56: the exec-kind classifier as a pure decision over fixtures.
 *
 * Every field the classifier reads -- e_type, has_interp -- is set here by
 * hand rather than parsed, so each case is exactly one point in the input
 * space and the table below is the specification the code is held to. The
 * shapes are the real ones: bzip2 is a fixed-base ET_EXEC that names an
 * interpreter, a PIE is an ET_DYN that names one, a shared object is an
 * ET_DYN that does not, and a static-pie is an ET_EXEC with a dynamic
 * section but no interpreter, which still needs no loader.
 */
#include "../exec_kind.h"
#include "../../elf/elf_types.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *what, exec_kind got, exec_kind want)
{
if (got != want) {
printf("FAIL %-24s got %s, want %s\n",
       what, exec_kind_name(got), exec_kind_name(want));
failures++;
}
}

static elf_parsed mk(uint16_t e_type, int has_interp, int has_dynamic)
{
elf_parsed p;
memset(&p, 0, sizeof p);
p.e_type = e_type;
p.has_interp = has_interp;
p.has_dynamic = has_dynamic;
return p;
}

int main(void)
{
/* what                         e_type    interp dyn   expected */
elf_parsed static_exec  = mk(ET_EXEC,  0, 0);
elf_parsed static_pie   = mk(ET_EXEC,  0, 1);
elf_parsed bzip2_shape  = mk(ET_EXEC,  1, 1);
elf_parsed pie          = mk(ET_DYN,   1, 1);
elf_parsed shared_obj   = mk(ET_DYN,   0, 1);
elf_parsed reloc        = mk(ET_REL,   0, 0);
elf_parsed core         = mk(ET_CORE,  0, 0);
elf_parsed none         = mk(ET_NONE,  0, 0);

check("static exec",  exec_kind_of(&static_exec), EXEC_KIND_STATIC);
check("static pie",   exec_kind_of(&static_pie),  EXEC_KIND_STATIC);
check("bzip2 shape",  exec_kind_of(&bzip2_shape), EXEC_KIND_DYNAMIC);
check("pie",          exec_kind_of(&pie),         EXEC_KIND_DYNAMIC);
check("shared object",exec_kind_of(&shared_obj),  EXEC_KIND_UNSUPPORTED);
check("relocatable",  exec_kind_of(&reloc),       EXEC_KIND_UNSUPPORTED);
check("core",         exec_kind_of(&core),        EXEC_KIND_UNSUPPORTED);
check("et_none",      exec_kind_of(&none),        EXEC_KIND_UNSUPPORTED);

/* Names are stable and total, including a value outside the enum. */
if (strcmp(exec_kind_name(EXEC_KIND_STATIC),  "static")  ||
    strcmp(exec_kind_name(EXEC_KIND_DYNAMIC), "dynamic") ||
    strcmp(exec_kind_name(EXEC_KIND_UNSUPPORTED), "unsupported") ||
    strcmp(exec_kind_name((exec_kind) 99), "unknown")) {
printf("FAIL names\n");
failures++;
}

if (failures) {
printf("exec_kind_unit: %d failure(s)\n", failures);
return 1;
}
printf("exec_kind_unit: all cases pass\n");
return 0;
}
