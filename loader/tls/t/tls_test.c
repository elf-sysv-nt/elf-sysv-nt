/* WP-37 certification: the loader's TLS against modules laid out in memory the
 * way an object carries a PT_TLS -- no cross build, the synthetic style WP-36's
 * version test and WP-35's lookup test use.
 *
 * It holds the package to the done-when bar. Two PT_TLS modules with init
 * images are laid out as the initial static set; the test asserts the static
 * offsets are correct and aligned, that an initial-exec/local-exec address
 * (tp + offset) reads the module's init value, and that __tls_get_addr returns
 * the same address for a static module as the offset names and a lazily
 * allocated correct block for a dynamic one. All four TLS models resolve
 * against that one layout. Then, with the live carrier, a managed thread is
 * created, a third module is registered after it (a dlopen), and the thread
 * resolves the new module's TLS -- the dlopen-into-a-prior-thread case -- after
 * which teardown frees the dynamic blocks and the DTV.
 */
#define _GNU_SOURCE
#include "../elf_tls.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
static void ck(const char *what, int ok)
{
	printf("    %-58s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

/* ---- the module init images -------------------------------------------- *
 * Module 1 is the executable's own TLS (local-exec). Module 2 is a shared
 * object present at startup (initial-exec, and reachable general/local-dynamic
 * too). Module 3 arrives later, a dlopen (general-dynamic, dynamic block). Each
 * image is a small run of sentinels a read can check exactly. Module 2 carries
 * eight .tbss bytes past its init image to exercise zeroing. */

static const uint32_t mod1_img[4] = {
	0xE0000000u, 0xE0000001u, 0xE0000002u, 0xE0000003u
};
static const uint64_t mod2_img[2] = {
	0xD000000000000001ull, 0xD000000000000002ull
};
static const uint64_t mod3_img[2] = {
	0xF000000000000001ull, 0xF000000000000002ull
};

static const elf_tls_module MOD1 = {
	.init_image = mod1_img, .init_size = 16, .mem_size = 16, .align = 8
};
static const elf_tls_module MOD2 = {
	.init_image = mod2_img, .init_size = 16, .mem_size = 24, .align = 16
};
static const elf_tls_module MOD3 = {
	.init_image = mod3_img, .init_size = 16, .mem_size = 16, .align = 16
};

/* ---- phase 1: static layout and the four models on a driven pointer ----
 *
 * This phase drives the resolver with the thread pointer explicitly
 * (elf_tls_get_addr_at), so it needs no live %gs carrier and runs anywhere the
 * host compiler does. It is the layout and four-model certification. */
static void phase1_layout_and_models(void)
{
	elf_tls_state st;
	elfsysv_tcb_t *tcb;
	void *tp;
	int64_t off1, off2;

	printf("  phase 1: static layout and the four models\n");

	elf_tls_state_init(&st, 0);
	uint64_t m1 = elf_tls_add_initial(&st, &MOD1);
	uint64_t m2 = elf_tls_add_initial(&st, &MOD2);
	elf_tls_finish_layout(&st);

	ck("module ids assigned one-based in load order", m1 == 1 && m2 == 2);

	/* variant II: mod1 nearest tp at -16, mod2 below it, rounded to 16 -> -48;
	 * the numbers WP-34's assign_static_tls produces for the same set. */
	off1 = elf_tls_static_tpoff(&st, 1);
	off2 = elf_tls_static_tpoff(&st, 2);
	ck("module 1 static offset is -16", off1 == -16);
	ck("module 2 static offset is -48", off2 == -48);
	ck("static size covers the modules and the surplus",
	   st.static_size >= st.static_used + st.surplus &&
	   st.static_used == 48);
	ck("surplus defaulted to glibc's 1664", st.surplus == 1664);

	tcb = elfsysv_tp_alloc(st.static_size);
	ck("TCB allocated for the static size", tcb != NULL);
	if (!tcb) return;
	tp = tcb->tp;

	ck("install stood up the static block and the DTV",
	   elf_tls_thread_install(&st, tcb) == 0 && tcb->head->dtv != NULL);

	/* each module's static address is aligned to the module's alignment */
	ck("module 1 address aligned to 8",
	   ((uintptr_t)((char *)tp + off1) & 7) == 0);
	ck("module 2 address aligned to 16",
	   ((uintptr_t)((char *)tp + off2) & 15) == 0);

	/* --- local-exec: module 1 is the executable's TLS, read at tp+offset - */
	{
		const uint32_t *le = (const uint32_t *)((char *)tp + off1);
		ck("local-exec: tp+off reads module 1's init image",
		   le[0] == 0xE0000000u && le[3] == 0xE0000003u);
	}
	/* --- initial-exec: module 2, a startup object, read at tp+offset ----- */
	{
		const uint64_t *ie = (const uint64_t *)((char *)tp + off2);
		ck("initial-exec: tp+off reads module 2's init image",
		   ie[0] == 0xD000000000000001ull && ie[1] == 0xD000000000000002ull);
		ck("initial-exec: module 2's .tbss tail is zero", ie[2] == 0);
	}
	/* --- general-dynamic: module 2 through __tls_get_addr, and it must land
	 *     on the very address the initial-exec offset names ---------------- */
	{
		elf_tls_index ti = { .ti_module = 2, .ti_offset = 0 };
		void *gd = elf_tls_get_addr_at(&st, tp, &ti);
		ck("general-dynamic: __tls_get_addr(module 2) == tp+off (static)",
		   gd == (void *)((char *)tp + off2));
		ck("general-dynamic: the resolved block reads the init value",
		   gd && *(const uint64_t *)gd == 0xD000000000000001ull);
	}
	/* --- local-dynamic: one call for module 2's base, then per-datum
	 *     offsets off it ------------------------------------------------- */
	{
		elf_tls_index base = { .ti_module = 2, .ti_offset = 0 };
		char *b = elf_tls_get_addr_at(&st, tp, &base);
		ck("local-dynamic: base + datum offsets read both of module 2's data",
		   b && *(uint64_t *)(b + 0) == 0xD000000000000001ull &&
		   *(uint64_t *)(b + 8) == 0xD000000000000002ull);
	}

	/* --- a dynamic module registered after this pointer was installed: it
	 *     resolves, lazily allocating a correct block off the DTV --------- */
	{
		uint64_t m3 = elf_tls_add_dynamic(&st, &MOD3);
		elf_tls_index ti0 = { .ti_module = 3, .ti_offset = 0 };
		elf_tls_index ti1 = { .ti_module = 3, .ti_offset = 8 };
		void *a0, *a1;
		ck("dynamic module gets the next id", m3 == 3);
		a0 = elf_tls_get_addr_at(&st, tp, &ti0);
		a1 = elf_tls_get_addr_at(&st, tp, &ti1);
		ck("dynamic: block lazily allocated off the DTV, not in the TCB",
		   a0 && (a0 < (void *)tcb->block ||
			  a0 >= (void *)((char *)tcb->block + tcb->block_size)));
		ck("dynamic: the lazily allocated block reads its init image",
		   a0 && a1 && *(uint64_t *)a0 == 0xF000000000000001ull &&
		   *(uint64_t *)a1 == 0xF000000000000002ull);
		ck("dynamic: a second call returns the same block (no realloc)",
		   elf_tls_get_addr_at(&st, tp, &ti0) == a0);
	}

	elf_tls_thread_teardown(tcb);
	ck("teardown cleared the DTV pointer", tcb->head->dtv == NULL);
	elfsysv_tp_free(tcb);
}

/* ---- phase 2: a dlopen into a thread created before it ------------------
 *
 * This phase uses the live carrier: a managed thread reads its own pointer
 * through %gs (carrier C3) and resolves TLS through the real __tls_get_addr.
 * The thread is created, then a third module is registered (the dlopen), then
 * the thread -- which existed before the module did -- resolves it. Barriers
 * sequence the two so "before" and "after" are exact. */

struct p2ctx {
	elf_tls_state    *st;
	pthread_barrier_t ready;   /* parent has installed the DTV        */
	pthread_barrier_t pre;     /* thread read static + GD, now waiting */
	pthread_barrier_t post;    /* parent has registered the dlopen    */
	pthread_barrier_t done;    /* thread finished; parent may tear down */
};

static void *p2_thread(void *arg)
{
	struct p2ctx *c = arg;
	void *tp;
	int64_t off2;
	elf_tls_index t2 = { .ti_module = 2, .ti_offset = 0 };
	elf_tls_index t3 = { .ti_module = 3, .ti_offset = 0 };
	elf_tls_index t3b = { .ti_module = 3, .ti_offset = 8 };
	void *gd, *a0, *a1;

	pthread_barrier_wait(&c->ready);
	tp = elfsysv_tp_get();                 /* the live carrier */
	off2 = elf_tls_static_tpoff(c->st, 2);

	ck("thread: initial-exec read through the live %gs carrier",
	   *(const uint64_t *)((char *)tp + off2) == 0xD000000000000001ull);
	gd = __tls_get_addr(&t2);
	ck("thread: __tls_get_addr(module 2) via carrier == tp+off",
	   gd == (void *)((char *)tp + off2));

	pthread_barrier_wait(&c->pre);
	pthread_barrier_wait(&c->post);        /* the dlopen has happened */

	a0 = __tls_get_addr(&t3);
	a1 = __tls_get_addr(&t3b);
	ck("thread: a module dlopen'd after this thread existed resolves",
	   a0 && a1 && *(uint64_t *)a0 == 0xF000000000000001ull &&
	   *(uint64_t *)a1 == 0xF000000000000002ull);

	pthread_barrier_wait(&c->done);
	return NULL;
}

static void phase2_dlopen_into_prior_thread(void)
{
	const char *why = NULL;
	elf_tls_state st2;
	elfsysv_tp_thread_t mt;
	struct p2ctx c;
	void *ret = NULL;
	int rc;

	printf("  phase 2: a dlopen into a thread created before it\n");

	if (elfsysv_tp_runtime_init(&why) != 0) {
		ck("carrier runtime init (needs the running Cygwin)", 0);
		printf("    (reason: %s)\n", why ? why : "unknown");
		return;
	}

	elf_tls_state_init(&st2, 0);
	elf_tls_add_initial(&st2, &MOD1);
	elf_tls_add_initial(&st2, &MOD2);
	elf_tls_finish_layout(&st2);
	c.st = &st2;
	pthread_barrier_init(&c.ready, NULL, 2);
	pthread_barrier_init(&c.pre, NULL, 2);
	pthread_barrier_init(&c.post, NULL, 2);
	pthread_barrier_init(&c.done, NULL, 2);

	rc = elfsysv_tp_thread_create(&mt, st2.static_size, p2_thread, &c);
	ck("managed thread created on a runtime-owned stack", rc == 0);
	if (rc != 0)
		return;

	/* The thread's pointer and TCB exist now; stand its TLS up before letting
	 * it run. The module set at this instant is {1,2} -- module 3 does not
	 * exist yet, which is the point. */
	ck("thread TLS installed for the startup module set",
	   elf_tls_thread_install(&st2, mt.tcb) == 0);
	pthread_barrier_wait(&c.ready);

	pthread_barrier_wait(&c.pre);
	/* the dlopen: a module with its own PT_TLS arrives after the thread. */
	ck("dlopen registered a third module after the thread existed",
	   elf_tls_add_dynamic(&st2, &MOD3) == 3);
	pthread_barrier_wait(&c.post);

	pthread_barrier_wait(&c.done);
	elf_tls_thread_teardown(mt.tcb);
	ck("teardown freed the dynamic block and the DTV",
	   mt.tcb->head->dtv == NULL);

	elfsysv_tp_thread_join(&mt, &ret);
	pthread_barrier_destroy(&c.ready);
	pthread_barrier_destroy(&c.pre);
	pthread_barrier_destroy(&c.post);
	pthread_barrier_destroy(&c.done);
}

int main(void)
{
	printf("WP-37: TLS in the loader\n");
	phase1_layout_and_models();
	phase2_dlopen_into_prior_thread();

	printf("\n%s: %d check(s) failed\n",
	       failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
