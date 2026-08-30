/* WP-38 end-to-end certification: a real plugin, ten thousand times.
 *
 * The done-when has two halves. The first is that a plugin loaded and unloaded
 * ten thousand times leaks nothing, which this checks as an accounting identity
 * rather than as a memory figure that could hide a slow leak: every resource a
 * load takes is counted at the seam it is taken through -- file images through
 * the loader's own host hook, table slots and scope slots by inspection -- and
 * after each cycle every count is back where it started. The second is that an
 * unwinder finds .eh_frame through dl_iterate_phdr for an object that arrived
 * after startup, which is checked by walking the phdrs the way libgcc's
 * unwinder walks them and reading PT_GNU_EH_FRAME out of a freshly dlopen'd
 * object.
 *
 * Usage: dl_e2e <path-to-libplug.so.0> [cycles]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../dl.h"

static int failures;
static int checks;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		printf("  FAIL %s\n", what);
	}
}

/* ---- the accounting host hook ------------------------------------------ */

/* The loader reads files through this, so every image it takes and every image
 * it gives back passes through the ledger. A leak of a file image shows up as
 * live != 0 with no arithmetic on the process's heap. */
static struct {
	long long live_bytes;
	long long live_blocks;
	long long reads;
	long long releases;
} ledger;

static int ledger_read(void *ctx, const char *path,
                       unsigned char **buf, size_t *size)
{
	FILE *f;
	long n;
	unsigned char *p;

	(void) ctx;
	f = fopen(path, "rb");
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
		fclose(f);
		return -1;
	}
	rewind(f);
	p = malloc((size_t) n);
	if (!p) {
		fclose(f);
		return -1;
	}
	if (fread(p, 1, (size_t) n, f) != (size_t) n) {
		free(p);
		fclose(f);
		return -1;
	}
	fclose(f);
	*buf = p;
	*size = (size_t) n;
	ledger.live_bytes += n;
	ledger.live_blocks++;
	ledger.reads++;
	return 0;
}

static void ledger_release(void *ctx, unsigned char *buf, size_t size)
{
	(void) ctx;
	free(buf);
	ledger.live_bytes -= (long long) size;
	ledger.live_blocks--;
	ledger.releases++;
}

/* ---- what the driver reads out of the plugin --------------------------- */

/* The plugin was compiled for the System V ABI; this program was not. Every
 * call into it goes through a pointer that carries the attribute. */
typedef int ELFSYSV_SYSV_ABI (*answer_fn)(void);
typedef int ELFSYSV_SYSV_ABI (*add_fn)(int, int);

/* ---- the phdr walk an unwinder does ------------------------------------ */

struct eh_search {
	const char *want_path;
	int         seen;
	uint64_t    eh_frame_hdr;
	uint64_t    load_bias;
	unsigned    objects;
};

static int eh_cb(struct dl_phdr_info *info, size_t size, void *data)
{
	struct eh_search *s = data;
	unsigned i;

	if (size < sizeof *info)
		return -1;
	s->objects++;
	if (!info->dlpi_name || strcmp(info->dlpi_name, s->want_path) != 0)
		return 0;
	s->seen = 1;
	s->load_bias = info->dlpi_addr;
	for (i = 0; i < info->dlpi_phnum; i++)
		if (info->dlpi_phdr[i].p_type == PT_GNU_EH_FRAME)
			s->eh_frame_hdr = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
	return 0;
}

/* ---- the cycle --------------------------------------------------------- */

/* One load, one look, one unload. Returns 0 on success. */
static int cycle(dl_state *st, const char *path, int deep)
{
	void *h;
	int *ready, *count;
	answer_fn answer;
	add_fn add;

	h = dl_open(st, path, RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		const char *e = dl_error(st);
		printf("  FAIL dlopen: %s\n", e ? e : "(no error reported)");
		failures++;
		return -1;
	}

	if (deep) {
		struct eh_search s;
		dl_info di;

		ready = dl_sym(st, h, "plug_ready");
		count = dl_sym(st, h, "plug_init_count");
		answer = (answer_fn) dl_sym(st, h, "plug_answer");
		add = (add_fn) dl_sym(st, h, "plug_add");

		ok(ready != NULL, "dlsym found the exported datum");
		ok(answer != NULL, "dlsym found the exported function");
		ok(ready && *ready == 1,
		   "the constructor ran before dlopen returned");
		ok(count && *count == 1,
		   "a freshly loaded copy has run its constructor exactly once");
		ok(answer && answer() == 42,
		   "the loaded code runs and its relocated pointer is right");
		ok(add && add(1, 2) == 45, "and takes arguments across the ABI");

		/* dladdr, over an address the driver only knows as a plain pointer. */
		memset(&di, 0, sizeof di);
		ok(dl_addr(st, (void *) answer, &di) != 0,
		   "dladdr finds the object an address is in");
		ok(di.dli_sname && strcmp(di.dli_sname, "plug_answer") == 0,
		   "and names the function");
		ok(di.dli_fname && strcmp(di.dli_fname, path) == 0,
		   "and the file it came from");

		/* The unwinder's walk. */
		memset(&s, 0, sizeof s);
		s.want_path = path;
		dl_iterate_phdr(st, eh_cb, &s);
		ok(s.seen, "dl_iterate_phdr reports the object that arrived after "
		           "startup");
		ok(s.eh_frame_hdr != 0,
		   "and PT_GNU_EH_FRAME is reachable through its phdrs");
		ok(s.eh_frame_hdr > s.load_bias,
		   "at a runtime address inside the mapping");
		/* The first four bytes of .eh_frame_hdr are its version (1) and three
		 * encoding bytes; an unwinder rejects anything else, so reading them
		 * back is what proves the address is the section and not a number. */
		if (s.eh_frame_hdr) {
			const unsigned char *p =
			    (const unsigned char *)(uintptr_t) s.eh_frame_hdr;
			ok(p[0] == 1, "the .eh_frame_hdr version byte reads back as 1");
		}
	}

	if (dl_close(st, h) != 0) {
		printf("  FAIL dlclose: %s\n", dl_error(st));
		failures++;
		return -1;
	}
	return 0;
}

static unsigned live_objects(const dl_state *st)
{
	unsigned i, n = 0;
	for (i = 0; i < DL_MAX_OBJECTS; i++)
		if (st->obj[i].in_use)
			n++;
	return n;
}

int main(int argc, char **argv)
{
	static dl_state st;
	dl_host host;
	const char *path;
	long cycles = 10000;
	long i;
	long long base_bytes;
	unsigned base_objects, base_scope;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <plugin.so> [cycles]\n", argv[0]);
		return 2;
	}
	path = argv[1];
	if (argc > 2)
		cycles = strtol(argv[2], NULL, 10);

	printf("WP-38 end to end: %s, %ld cycles\n", path, cycles);

	host.read = ledger_read;
	host.release = ledger_release;
	host.ctx = NULL;
	dl_state_init(&st, &host, argc, argv, NULL);

	base_bytes = ledger.live_bytes;
	base_objects = live_objects(&st);
	base_scope = st.reloc.count;

	/* The first cycle is looked at closely; the rest are for the ledger. */
	if (cycle(&st, path, 1) != 0)
		goto done;

	ok(ledger.live_bytes == base_bytes,
	   "the file image was given back on unload");
	ok(live_objects(&st) == base_objects,
	   "the table slot was given back on unload");
	ok(st.reloc.count == base_scope,
	   "the relocation scope slot was given back on unload");

	for (i = 1; i < cycles; i++) {
		if (cycle(&st, path, 0) != 0) {
			printf("  FAIL cycle %ld did not complete\n", i);
			goto done;
		}
		/* Check the ledger every cycle, not only at the end: a leak of one
		 * mapping in ten thousand is the failure worth catching, and a final
		 * total would let an early leak hide behind a late release. */
		if (ledger.live_bytes != base_bytes ||
		    live_objects(&st) != base_objects ||
		    st.reloc.count != base_scope) {
			printf("  FAIL cycle %ld leaked: %lld bytes, %u objects, "
			       "%u scope slots\n", i, ledger.live_bytes - base_bytes,
			       live_objects(&st) - base_objects,
			       st.reloc.count - base_scope);
			failures++;
			goto done;
		}
	}

	ok(ledger.reads == cycles && ledger.releases == cycles,
	   "every image read was released, once");
	ok(ledger.live_blocks == 0, "no image is still held");
	ok(st.adds == (unsigned long long) cycles &&
	   st.subs == (unsigned long long) cycles,
	   "the add and remove counters an unwinder caches on agree");

	/* The last cycle proves the loader still works, not merely that it did
	 * not grow: a table that leaked slots would have failed above, but a
	 * loader that quietly stopped loading would not. */
	if (cycle(&st, path, 1) == 0)
		printf("the ten-thousandth-and-first load behaves like the first\n");

done:
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
