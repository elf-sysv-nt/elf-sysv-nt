/*
 * WP-56 acceptance -- read a built package's image shape with the loader's own
 * classifier, not with a second reading of our own.
 *
 * accept.sh already reports a package's libc surface: the undefined symbols it
 * needs and whether each forwards, crosses a certified shim, or waits. That
 * says the runtime can resolve what the package calls. It does not say the
 * loader will take the image at all -- and the classifier that decides which
 * crossing an image is owed (exec_kind_of, WP-56/DR-0058) is the gate the
 * dynamic driver stands behind. A package that reads "ready" on its symbols
 * but that the classifier does not call dynamic is not the shape the driver
 * runs, and the harness should say so before it credits the package.
 *
 * So this helper runs the loader's real path over the built ELF: elf_parse()
 * (WP-31) validates the image, exec_kind_of() (WP-56) classifies it, and the
 * parsed view names the interpreter and the sonames the image needs. It prints
 * three lines the harness reads back:
 *
 *   kind=<static|dynamic|unsupported>
 *   interp=<path or ->
 *   needed=<comma-separated sonames, or ->
 *
 * bzip2's shape -- an ET_EXEC that names /lib64/ld-linux-x86-64.so.2 in a
 * PT_INTERP and lists libc.so.6 in its DT_NEEDED -- reads kind=dynamic, which
 * is exactly the image the dynamic crossing driver is written to run. A static
 * image reads kind=static (the WP-41 path, entered at e_entry with no loader)
 * and a shared object or relocatable reads kind=unsupported.
 *
 * Exit: 0 when the image parsed and was classified; 1 on a usage error; 2 when
 * elf_parse() rejected the image, with the field and message on stderr. The
 * helper reads the file whole into memory and never writes it.
 */
#include "../../loader/elf/elf_parse.h"
#include "../../loader/exec/exec_kind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *slurp(const char *path, size_t *size)
{
FILE *f = fopen(path, "rb");
long n;
unsigned char *buf;

if (!f)
return NULL;
if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
fclose(f);
return NULL;
}
rewind(f);
buf = malloc((size_t)n);
if (buf && n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
free(buf);
buf = NULL;
}
fclose(f);
if (buf)
*size = (size_t)n;
return buf;
}

/* A validated strtab-relative offset, printed as the C string it points at.
 * elf_parse() has proven strtab_off lies within the image; this walks to the
 * terminator without leaving the image, and prints "?" for a name whose string
 * runs to the end without one. */
static void put_str(const unsigned char *img, size_t size,
                    const elf_parsed *p, uint64_t rel)
{
uint64_t at = p->strtab_off + rel;
size_t i;

if (!p->has_strtab || at >= size) {
fputs("?", stdout);
return;
}
for (i = at; i < size && img[i]; i++)
putchar(img[i]);
if (i >= size)
putchar('?');
}

int main(int argc, char **argv)
{
const char *path;
unsigned char *img;
size_t size = 0;
elf_parsed p;
elf_diag diag;
elf_err e;
exec_kind k;
unsigned i;

if (argc != 2) {
fprintf(stderr, "usage: img_shape ELF\n");
return 1;
}
path = argv[1];

img = slurp(path, &size);
if (!img) {
fprintf(stderr, "img_shape: cannot read %s\n", path);
return 1;
}

e = elf_parse(img, size, &p, &diag);
if (e != elf_ok) {
fprintf(stderr, "img_shape: %s rejected: %s (%s)\n",
        path, elf_err_name(e), diag.msg);
free(img);
return 2;
}

k = exec_kind_of(&p);
printf("kind=%s\n", exec_kind_name(k));

printf("interp=");
if (p.has_interp && p.interp_off < size) {
size_t j;
for (j = p.interp_off; j < size && img[j]; j++)
putchar(img[j]);
} else {
putchar('-');
}
putchar('\n');

printf("needed=");
if (p.needed_count == 0) {
putchar('-');
} else {
for (i = 0; i < p.needed_count; i++) {
if (i)
putchar(',');
put_str(img, size, &p, p.needed[i]);
}
}
putchar('\n');

free(img);
return 0;
}
