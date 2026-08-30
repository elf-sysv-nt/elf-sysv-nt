/* WP-T1 unit test: run the parser over the committed corpus and check that
 * each fixture produces the verdict its manifest records. An accept fixture
 * must parse; a reject fixture must fail with the recorded code and with a
 * diagnostic whose field names the recorded member. Every image is loaded
 * behind a guard page, so a parser that reads past the end of even an
 * accepted fixture crashes the test rather than passing it quietly.
 *
 * Usage: unit [corpus-dir]   (default: corpus)
 */
#include "../elf_parse.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long sz;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0) { fclose(f); return NULL; }
	buf = (unsigned char *)malloc((size_t)sz ? (size_t)sz : 1);
	if (buf && sz)
		if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); buf = NULL; }
	fclose(f);
	*n = (size_t)sz;
	return buf;
}

int main(int argc, char **argv)
{
	const char *dir = (argc > 1) ? argv[1] : "corpus";
	char mpath[1024];
	FILE *mf;
	char line[512];
	int pass = 0, fail = 0;

	snprintf(mpath, sizeof mpath, "%s/manifest.tsv", dir);
	mf = fopen(mpath, "r");
	if (!mf) { fprintf(stderr, "unit: cannot open %s\n", mpath); return 2; }

	while (fgets(line, sizeof line, mf)) {
		char name[256], verdict[16], code[16], field[128];
		char path[1024];
		unsigned char *raw;
		size_t n;
		guard_buf g;
		elf_parsed out;
		elf_diag diag;
		elf_err e;
		int ok = 1;

		field[0] = 0;
		/* NAME \t verdict \t code \t field(optional) */
		int got = sscanf(line, "%255s %15s %15s %127[^\n\t]",
		                 name, verdict, code, field);
		if (got < 3) continue;

		snprintf(path, sizeof path, "%s/%s", dir, name);
		raw = read_file(path, &n);
		if (!raw) { fprintf(stderr, "unit: cannot read %s\n", path); fail++; continue; }
		if (guard_load(raw, n, &g) != 0) {
			fprintf(stderr, "unit: guard map failed for %s\n", path);
			free(raw); fail++; continue;
		}
		free(raw);

		e = elf_parse(g.base, g.len, &out, &diag);

		if (strcmp(verdict, "accept") == 0) {
			if (e != elf_ok) {
				printf("FAIL %-32s expected accept, got %s (%s: %s)\n",
				       name, elf_err_name(e), diag.field ? diag.field : "?",
				       diag.msg);
				ok = 0;
			}
		} else {
			if (e == elf_ok) {
				printf("FAIL %-32s expected reject %s, but it parsed\n",
				       name, code);
				ok = 0;
			} else {
				if (strcmp(elf_err_name(e), code) != 0) {
					printf("FAIL %-32s expected code %s, got %s\n",
					       name, code, elf_err_name(e));
					ok = 0;
				}
				if (field[0] &&
				    (!diag.field || !strstr(diag.field, field))) {
					printf("FAIL %-32s expected field ~%s, got %s\n",
					       name, field, diag.field ? diag.field : "(none)");
					ok = 0;
				}
				if (!diag.field || diag.msg[0] == 0) {
					printf("FAIL %-32s rejection carried no diagnostic\n", name);
					ok = 0;
				}
			}
		}
		guard_free(&g);
		if (ok) { pass++; }
		else    { fail++; }
	}
	fclose(mf);

	printf("\nunit: %d passed, %d failed\n", pass, fail);
	return fail ? 1 : 0;
}
