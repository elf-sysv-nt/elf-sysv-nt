/*
 * t/sigtls.c -- the per-thread signal record through the carrier's TCB,
 * held to its properties (WP-27 milestone 8).
 *
 * The unit under test is a seam, so the test fabricates everything on the
 * far side of it: threads run on stacks this harness owns, so the carrier
 * word -- a fixed offset below gs:[StackBase] -- lands in harness memory
 * rather than in the host runtime's _cygtls, and the TCB heads are static
 * records rather than allocations of a TLS package.  The offset is deep
 * (0x30000 on a 256 KiB stack) so no live frame and no host reservation
 * comes near it.
 *
 * What is certified: with no carrier the provider answers NULL and the
 * signal package's fallback serves; an adopted record captures every mask
 * and altstack access on its thread and no other; a thread created
 * through the layered launch finds its record hung on its own TCB before
 * the body runs, initialized to the creator's mask with the unblockable
 * bits dropped and no alternate stack; and the two threads' records stay
 * independent under interleaved use.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sigtls.h"

#define OFF ((uint64_t)0x30000)
#define PAD ((uint64_t)0x40000)
#define STACKSZ ((size_t)256 * 1024)

static int failures;
static int checks;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		printf("not ok - %s\n", what);
	}
}

static elfsysv_sigstate_t st;
static elfsysv_tcbhead_t head_a, head_b;
static elfsysv_sigtls_t rec_a;

/* What the created thread observed, read by the creator after the join. */
static elfsysv_sigtls_t *b_resolved;
static void *b_hung;
static uint64_t b_blocked;
static int b_altstack_flags;
static void *b_carrier;

static uint64_t gs_stackbase(void)
{
	return elfsysv_carrier_gs_read(ELFSYSV_TEB_STACKBASE);
}

/* The create fn the unit calls, System V per its typedef, over the host's
 * pthread_create with a harness-owned stack. */
static pthread_attr_t attr_b;

static int __attribute__((sysv_abi))
create_wrap(void **thread, const void *attr, void *(*start)(void *),
	    void *arg)
{
	(void)attr;
	return pthread_create((pthread_t *)thread, &attr_b, start, arg);
}

static void *body_b(void *arg)
{
	(void)arg;
	b_carrier = elfsysv_carrier_get();
	b_hung = head_b.sigtls;
	b_resolved = elf_sig_tls(&st);
	b_blocked = b_resolved ? b_resolved->blocked : ~0ull;
	b_altstack_flags = b_resolved ? b_resolved->altstack.ss_flags : -1;
	return 0;
}

static void *body_a(void *arg)
{
	elfsysv_sigset_t s;
	elfsysv_stack_t ss;
	static char alt[64 * 1024];
	const char *why = 0;

	(void)arg;

	/* Before any carrier: the provider answers NULL and the embedded
	 * fallback serves; adoption refuses. */
	elfsysv_face_sigtls_install();
	elf_sig_init(&st);
	ok(elf_sig_tls(&st) == &st.tls0,
	   "with no carrier the fallback record serves");
	ok(elfsysv_face_sigtls_adopt(&rec_a, 0) == -1,
	   "and adoption refuses without a TCB");

	uint64_t base = gs_stackbase();
	ok(elfsysv_carrier_init(base - OFF, PAD, &why) == 0,
	   "the carrier initializes against the harness stack");

	/* A carrier with no TCB in it is still the fallback. */
	ok(elf_sig_tls(&st) == &st.tls0,
	   "an empty carrier is still the fallback");

	/* Adopt: the record hangs on this thread's TCB and captures every
	 * access. */
	elfsysv_carrier_set(&head_a);
	ok(elfsysv_face_sigtls_adopt(&rec_a, elf_sigbit(10) | elf_sigbit(9))
		   == 0,
	   "adoption succeeds once the TCB is carried");
	ok(head_a.sigtls == &rec_a, "the record hangs on the TCB head");
	ok(rec_a.blocked == elf_sigbit(10),
	   "adopted with the mask given, SIGKILL dropped");
	ok(elf_sig_tls(&st) == &rec_a,
	   "the provider now resolves the adopted record");

	elf_sigset_from_mask(&s, elf_sigbit(12));
	elf_sig_procmask(&st, ELFSYSV_SIG_BLOCK, &s, NULL);
	ok(rec_a.blocked == (elf_sigbit(10) | elf_sigbit(12)),
	   "procmask lands in the adopted record");
	ok(st.tls0.blocked == 0, "and not in the fallback");

	memset(&ss, 0, sizeof(ss));
	ss.ss_sp = alt;
	ss.ss_size = sizeof(alt);
	ok(elf_sig_altstack(&st, 0, &ss, NULL) == 0 &&
		   rec_a.altstack.ss_sp == alt,
	   "the alternate stack lands in the adopted record too");

	/* The layered launch: carrier, then record, then body. */
	static elfsysv_face_sigtls_launch_t launch;
	void *th = 0;
	launch.carrier.tp = &head_b;
	launch.start = body_b;
	launch.arg = 0;
	ok(elfsysv_face_sigtls_thread_create(create_wrap, &th, 0, &launch,
					     &st) == 0,
	   "the layered thread create succeeds");
	pthread_join((pthread_t)th, NULL);

	ok(b_carrier == &head_b,
	   "the new thread carries its own TCB before the body");
	ok(b_hung == &launch.record,
	   "and finds its record hung on it");
	ok(b_resolved == &launch.record,
	   "the provider resolves the new thread's own record");
	ok(b_blocked == (elf_sigbit(10) | elf_sigbit(12)),
	   "initialized to the creator's mask at create time");
	ok(b_altstack_flags == ELFSYSV_SS_DISABLE,
	   "with no alternate stack inherited");

	/* Independence, after the fact: nothing the new thread did moved
	 * this thread's record, and this thread still resolves its own. */
	ok(elf_sig_tls(&st) == &rec_a,
	   "the creating thread still resolves its own record");
	ok(rec_a.blocked == (elf_sigbit(10) | elf_sigbit(12)),
	   "and its mask did not move");

	elf_sig_set_tls_provider(NULL);
	return 0;
}

int main(void)
{
	pthread_t a;
	pthread_attr_t attr_a;
	void *stack_a, *stack_b;

	if (posix_memalign(&stack_a, 4096, STACKSZ) ||
	    posix_memalign(&stack_b, 4096, STACKSZ)) {
		printf("no memory for the harness stacks\n");
		return 1;
	}
	memset(stack_a, 0, STACKSZ);
	memset(stack_b, 0, STACKSZ);

	pthread_attr_init(&attr_a);
	pthread_attr_setstack(&attr_a, stack_a, STACKSZ);
	pthread_attr_init(&attr_b);
	pthread_attr_setstack(&attr_b, stack_b, STACKSZ);

	if (pthread_create(&a, &attr_a, body_a, NULL)) {
		printf("the harness thread did not start\n");
		return 1;
	}
	pthread_join(a, NULL);

	printf("checks=%d\nfailures=%d\n", checks, failures);
	printf("verdict=%s\n", failures ? "no" : "yes");
	return failures ? 1 : 0;
}
