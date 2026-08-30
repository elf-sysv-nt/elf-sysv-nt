/* WP-42 unit certification: the decisions, with no host fork in the loop.
 *
 * What a written ordering makes predictable: prepare handlers in the reverse of
 * registration and parent and child handlers in registration order, the loader
 * lock taken after the last prepare handler and released before the first
 * parent one, the child reinitializing rather than unlocking, the region table
 * sorted and disjoint, a manifest that survives a round trip and refuses every
 * corruption named in the header, and an audit that reports the first field
 * that moved and reports a rebase before anything the rebase caused.
 */
#include <stdio.h>
#include <string.h>

#include "../fork.h"

static int fails;
static void ck(int cond, const char *what)
{
	if (!cond) {
		printf("FAIL %s\n", what);
		fails++;
	}
}

/* ---- a recording lock, so the bracket is observable --------------------- */

static char lock_log[64];
static size_t lock_n;
static int lock_depth;

static void log_ch(char c)
{
	if (lock_n + 1 < sizeof lock_log)
		lock_log[lock_n++] = c;
}

static void t_acquire(void *ctx) { (void)ctx; lock_depth++; log_ch('A'); }
static void t_release(void *ctx) { (void)ctx; lock_depth--; log_ch('R'); }
static void t_reinit(void *ctx)  { (void)ctx; lock_depth = 0; log_ch('I'); }

static const elf_fork_lock test_lock = { t_acquire, t_release, t_reinit, NULL };

/* ---- a memory host that records rather than allocates ------------------- */

static uint64_t mem_seen[ELF_FORK_REGION_MAX];
static uint32_t mem_n;
static int mem_refuse_at = -1;

static int t_reserve(void *ctx, uint64_t base, uint64_t size)
{
	(void)ctx; (void)size;
	if (mem_refuse_at >= 0 && (uint32_t)mem_refuse_at == mem_n)
		return -1;
	if (mem_n < ELF_FORK_REGION_MAX)
		mem_seen[mem_n++] = base;
	return 0;
}

static int t_commit(void *ctx, uint64_t b, uint64_t s, uint32_t p)
{
	(void)ctx; (void)b; (void)s; (void)p;
	return 0;
}

static int t_free(void *ctx, uint64_t b, uint64_t s)
{
	(void)ctx; (void)b; (void)s;
	return 0;
}

static const elf_fork_mem test_mem = { t_reserve, t_commit, t_free, NULL };

/* ---- handler ordering --------------------------------------------------- */

static char order[64];
static size_t order_n;

static void note(char c)
{
	if (order_n + 1 < sizeof order)
		order[order_n++] = c;
}

static void p1(void) { note('1'); }
static void p2(void) { note('2'); }
static void p3(void) { note('3'); }
static void c1(void) { note('a'); }
static void c2(void) { note('b'); }
static void c3(void) { note('c'); }

static void test_ordering(void)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, &test_mem, &test_lock);

	elf_fork_atfork(&fs, p1, c1, c1);
	elf_fork_atfork(&fs, p2, c2, c2);
	elf_fork_atfork(&fs, p3, c3, c3);

	order_n = 0; lock_n = 0; lock_depth = 0;
	ck(elf_fork_prepare(&fs, elf_fork_flavor_fork, NULL) == 0, "prepare returns 0");
	order[order_n] = '\0';
	ck(strcmp(order, "321") == 0, "prepare handlers run in reverse order");
	ck(lock_depth == 1, "the loader lock is held across the fork");
	ck(lock_n == 1 && lock_log[0] == 'A', "the lock is taken after the handlers");

	ck(elf_fork_prepare(&fs, elf_fork_flavor_fork, NULL) == -1,
	   "a second prepare while armed is refused");

	order_n = 0;
	elf_fork_parent(&fs);
	order[order_n] = '\0';
	ck(strcmp(order, "abc") == 0, "parent handlers run in registration order");
	ck(lock_depth == 0, "the parent releases the lock");

	/* The child side of the same fork: prepare again, then take the child
	 * path, which must reinitialize rather than unlock. */
	order_n = 0; lock_n = 0;
	elf_fork_prepare(&fs, elf_fork_flavor_vfork, NULL);
	order_n = 0;
	ck(elf_fork_child(&fs, NULL, 0) == 0, "the child crosses with no manifest");
	order[order_n] = '\0';
	ck(strcmp(order, "abc") == 0, "child handlers run in registration order");
	lock_log[lock_n] = '\0';
	ck(strchr(lock_log, 'I') != NULL, "the child reinitializes the lock");
	ck(strchr(lock_log + 1, 'R') == NULL, "the child does not unlock");
}

static void test_atfork_bound(void)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, &test_mem, &test_lock);
	for (int i = 0; i < ELF_FORK_ATFORK_MAX; i++)
		ck(elf_fork_atfork(&fs, p1, NULL, NULL) == 0, "atfork accepts");
	ck(elf_fork_atfork(&fs, p1, NULL, NULL) == -1,
	   "atfork refuses past its bound");
	ck(fs.why[0] != '\0', "the refusal says why");
}

/* ---- the region table --------------------------------------------------- */

static void test_regions(void)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, &test_mem, &test_lock);

	ck(elf_fork_region_add(&fs, 0x30000, 0x10000, elf_fork_region_reserve,
	                       0, "third") == 0, "add third");
	ck(elf_fork_region_add(&fs, 0x10000, 0x10000, elf_fork_region_reserve,
	                       0, "first") == 0, "add first");
	ck(elf_fork_region_add(&fs, 0x20000, 0x10000, elf_fork_region_commit,
	                       6, "second") == 0, "add second");
	ck(fs.region_count == 3, "three regions");
	ck(fs.region[0].base == 0x10000 && fs.region[1].base == 0x20000 &&
	   fs.region[2].base == 0x30000, "the table is sorted by base");

	ck(elf_fork_region_add(&fs, 0x18000, 0x10000, elf_fork_region_reserve,
	                       0, "overlap") == -1, "an overlap is refused");
	ck(elf_fork_region_add(&fs, 0x50000, 0, elf_fork_region_reserve,
	                       0, "empty") == -1, "a zero size is refused");
	ck(elf_fork_region_add(&fs, UINT64_MAX - 4, 16, elf_fork_region_reserve,
	                       0, "wrap") == -1, "a wrapping range is refused");

	ck(elf_fork_region_drop(&fs, 0x20000) == 0, "drop the middle");
	ck(fs.region_count == 2 && fs.region[1].base == 0x30000,
	   "the table closes over the hole");
	ck(elf_fork_region_drop(&fs, 0x20000) == -1, "dropping twice is refused");

	/* A name longer than the field is truncated, not overrun. */
	ck(elf_fork_region_add(&fs, 0x90000, 0x1000, elf_fork_region_reserve, 0,
	                       "a name far longer than the field can hold") == 0,
	   "a long name is accepted");
	ck(strlen(fs.region[2].what) == ELF_FORK_WHAT_MAX - 1,
	   "a long name is truncated to the field");
}

/* ---- the manifest ------------------------------------------------------- */

static void fill(elf_fork_state *fs, int n)
{
	for (int i = 0; i < n; i++)
		elf_fork_region_add(fs, 0x100000 + (uint64_t)i * 0x10000, 0x8000,
		                    (i & 1) ? elf_fork_region_commit
		                            : elf_fork_region_reserve,
		                    (uint32_t)(i & 7), "region");
}

static void test_manifest_roundtrip(void)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, &test_mem, &test_lock);
	fill(&fs, 5);

	unsigned char buf[4096];
	size_t used = 0;
	ck(elf_fork_manifest_pack(&fs, buf, sizeof buf, &used) == 0, "pack");
	ck(used == elf_fork_manifest_size(5), "the packed size is the stated size");

	elf_fork_region out[ELF_FORK_REGION_MAX];
	char why[160];
	int n = elf_fork_manifest_unpack(buf, used, out, why, sizeof why);
	ck(n == 5, "unpack returns the count");
	for (int i = 0; i < 5 && i < n; i++) {
		ck(out[i].base == fs.region[i].base, "base survives");
		ck(out[i].size == fs.region[i].size, "size survives");
		ck(out[i].kind == fs.region[i].kind, "kind survives");
		ck(out[i].prot == fs.region[i].prot, "prot survives");
		ck(strcmp(out[i].what, fs.region[i].what) == 0, "name survives");
	}

	/* An empty manifest is a header and nothing else, and round trips. */
	elf_fork_state e;
	elf_fork_state_init(&e, NULL, NULL, &test_mem, &test_lock);
	ck(elf_fork_manifest_pack(&e, buf, sizeof buf, &used) == 0, "pack empty");
	ck(elf_fork_manifest_unpack(buf, used, out, why, sizeof why) == 0,
	   "an empty manifest unpacks to zero regions");

	/* A buffer too small is refused rather than partly written. */
	ck(elf_fork_manifest_pack(&fs, buf, 8, &used) == -1,
	   "a short buffer is refused");
}

static void test_manifest_refusals(void)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, &test_mem, &test_lock);
	fill(&fs, 3);

	unsigned char good[4096], buf[4096];
	size_t used = 0;
	elf_fork_manifest_pack(&fs, good, sizeof good, &used);

	elf_fork_region out[ELF_FORK_REGION_MAX];
	char why[160];

	struct { const char *what; size_t at; unsigned char v; size_t len; } bad[] = {
		{ "bad magic",        0, 0xff, 0 },
		{ "bad version",      4, 0x09, 0 },
		{ "count past bound", 8, 0xff, 0 },
	};

	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		memcpy(buf, good, used);
		buf[bad[i].at] = bad[i].v;
		size_t len = bad[i].len ? bad[i].len : used;
		why[0] = '\0';
		ck(elf_fork_manifest_unpack(buf, len, out, why, sizeof why) == -1,
		   bad[i].what);
		ck(why[0] != '\0', "the refusal says why");
	}

	/* A length that does not match the count, both ways. */
	ck(elf_fork_manifest_unpack(good, used - 1, out, why, sizeof why) == -1,
	   "a truncated manifest is refused");
	memcpy(buf, good, used);
	buf[used] = 0;
	ck(elf_fork_manifest_unpack(buf, used + 1, out, why, sizeof why) == -1,
	   "a manifest with trailing bytes is refused");

	/* Every prefix of a valid manifest is refused rather than half-read. */
	for (size_t len = 0; len < used; len++)
		ck(elf_fork_manifest_unpack(good, len, out, why, sizeof why) == -1,
		   "every short prefix is refused");

	/* A zero size in a record. */
	memcpy(buf, good, used);
	memset(buf + 12 + 8, 0, 8);
	ck(elf_fork_manifest_unpack(buf, used, out, why, sizeof why) == -1,
	   "a zero-size region is refused");

	/* A record whose base + size wraps. */
	memcpy(buf, good, used);
	memset(buf + 12, 0xff, 8);
	memset(buf + 12 + 8, 0xff, 8);
	ck(elf_fork_manifest_unpack(buf, used, out, why, sizeof why) == -1,
	   "a wrapping region is refused");

	/* An unterminated name: fill the whole field. */
	memcpy(buf, good, used);
	memset(buf + 12 + 24, 'x', ELF_FORK_WHAT_MAX);
	ck(elf_fork_manifest_unpack(buf, used, out, why, sizeof why) == -1,
	   "an unterminated region name is refused");

	/* An unknown kind. */
	memcpy(buf, good, used);
	buf[12 + 16] = 0x7f;
	ck(elf_fork_manifest_unpack(buf, used, out, why, sizeof why) == -1,
	   "an unknown region kind is refused");

	/* Regions out of order, which in a well-formed table cannot happen and in
	 * a rewritten one would have the child reserve a range twice. */
	memcpy(buf, good, used);
	size_t rec = 8 + 8 + 4 + 4 + ELF_FORK_WHAT_MAX;
	unsigned char tmp[64];
	memcpy(tmp, buf + 12, rec);
	memcpy(buf + 12, buf + 12 + rec, rec);
	memcpy(buf + 12 + rec, tmp, rec);
	ck(elf_fork_manifest_unpack(buf, used, out, why, sizeof why) == -1,
	   "unsorted regions are refused");
}

/* ---- the child replay --------------------------------------------------- */

static void test_child_replay(void)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, &test_mem, &test_lock);
	fill(&fs, 4);

	unsigned char buf[4096];
	size_t used = 0;
	elf_fork_prepare(&fs, elf_fork_flavor_fork, NULL);
	elf_fork_manifest_pack(&fs, buf, sizeof buf, &used);

	mem_n = 0; mem_refuse_at = -1;
	ck(elf_fork_child(&fs, buf, used) == 0, "the child replays the manifest");
	ck(mem_n == 4, "every region was reserved");
	ck(mem_seen[0] < mem_seen[1] && mem_seen[1] < mem_seen[2],
	   "the replay is in address order");

	/* A host that refuses one reservation fails the child by name, and the
	 * child handlers do not run against a loader that did not cross. */
	elf_fork_prepare(&fs, elf_fork_flavor_fork, NULL);
	mem_n = 0; mem_refuse_at = 2;
	order_n = 0;
	ck(elf_fork_child(&fs, buf, used) == -1, "a refused reservation fails");
	ck(strstr(fs.why, "region") != NULL, "the failure names the region");
	ck(order_n == 0, "no child handler runs after a failed replay");
	mem_refuse_at = -1;

	/* A manifest that will not parse fails before any host call. */
	elf_fork_prepare(&fs, elf_fork_flavor_fork, NULL);
	buf[0] = 0xff;
	mem_n = 0;
	ck(elf_fork_child(&fs, buf, used) == -1, "a bad manifest fails the child");
	ck(mem_n == 0, "nothing was reserved from a manifest that would not parse");
	ck(strstr(fs.why, "manifest") != NULL, "the failure names the manifest");
}

/* ---- the audit ---------------------------------------------------------- */

static void test_audit(void)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, &test_mem, &test_lock);
	fill(&fs, 2);

	elf_fork_audit a, b;
	char why[160];
	elf_fork_audit_take(&fs, &a);
	elf_fork_audit_take(&fs, &b);
	ck(elf_fork_audit_diff(&a, &b, why, sizeof why) == 0,
	   "an unchanged state audits equal");
	ck(why[0] == '\0', "an equal audit says nothing");

	/* A rebase is reported first and by name, even when everything else moved
	 * with it, because it is the difference that explains the others. */
	b.self_addr += 0x10000;
	b.obj_hash ^= 1;
	b.dtv_hash ^= 1;
	ck(elf_fork_audit_diff(&a, &b, why, sizeof why) == -1, "a rebase differs");
	ck(strstr(why, "rebased") != NULL, "a rebase is reported by name");

	b = a;
	b.region_hash ^= 1;
	ck(elf_fork_audit_diff(&a, &b, why, sizeof why) == -1, "a region moved");
	ck(strstr(why, "regions") != NULL, "the moved field is named");

	b = a;
	b.rdebug_map += 8;
	ck(elf_fork_audit_diff(&a, &b, why, sizeof why) == -1, "the map head moved");
	ck(strstr(why, "rdebug map head") != NULL, "the map head is named");

	/* The outermost difference is reported when several coincide. */
	b = a;
	b.obj_hash ^= 1;
	b.dtv_hash ^= 1;
	elf_fork_audit_diff(&a, &b, why, sizeof why);
	ck(strstr(why, "object table") != NULL,
	   "the object table is reported before the DTV");

	/* A record that is not an audit is refused rather than compared. */
	b = a;
	b.magic = 0;
	ck(elf_fork_audit_diff(&a, &b, why, sizeof why) == -1,
	   "a record without the magic is refused");
}

int main(void)
{
	test_ordering();
	test_atfork_bound();
	test_regions();
	test_manifest_roundtrip();
	test_manifest_refusals();
	test_child_replay();
	test_audit();

	if (fails == 0)
		printf("wp42 unit: the ordering, the manifest, the replay and the "
		       "audit all hold\n");
	else
		printf("wp42 unit: %d checks failed\n", fails);
	return fails != 0;
}
