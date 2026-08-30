/* WP-42 end-to-end certification: the done-when, on a real fork.
 *
 * Three stages, each a claim the plan makes and none of them checkable by
 * reading the code.
 *
 * dlopen -- a fork from a thread that is inside dlopen produces a child that
 * runs rather than a child that deadlocks. A second thread loops through the
 * real dl_open and dl_close of a cross-linked plugin, holding the loader lock
 * across each and sleeping inside it so the window is wide rather than
 * theoretical; the forking thread calls elf_fork_prepare, which blocks until
 * that thread has left, forks, and the child calls dl_sym on the object and
 * calls into it across the ABI boundary. A child that deadlocked never reaches
 * the call and the watchdog kills it, which the parent reports as a signal
 * rather than a status, so the deadlock cannot be mistaken for a pass.
 *
 * tls -- the TLS block and every DTV cross. The fork is taken from a managed
 * thread, which owns its stack and therefore owns DR-0003's carrier word; the
 * child's initial thread is not that thread and its carrier holds whatever the
 * copy left, so the child re-establishes the pointer from the TCB it inherited
 * and the audit compares the whole DTV, slot by slot, against the parent's.
 *
 * rebase -- the failure mode that haunts Cygwin's fork is confirmed absent
 * rather than assumed absent. Every child reports back the address of the
 * loader's own code and the base of cygwin1.dll, and both must equal what the
 * parent recorded before the call. A rebase moves one or both, and it moves
 * them before anything else has a chance to go wrong, which is why the audit
 * reports it first and by name.
 *
 * Usage:
 *   fork_e2e <stage> <path-to-libplug.so.0> [count]
 *
 * Stages: dlopen, tls, rebase, all.
 * Exit: 0 the stage held, 1 it did not, 2 usage.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../fork.h"

#if defined(__CYGWIN__) || defined(_WIN32)
#include <windows.h>
#endif

typedef int ELFSYSV_SYSV_ABI (*add_fn)(int, int);

static int failures;

static void ok(int cond, const char *what)
{
	if (!cond) {
		failures++;
		printf("  FAIL %s\n", what);
	}
}

/* What the child reports back, in the low bits of its exit status. A status is
 * seven bits here for the same reason WP-41's specimen used seven: a child that
 * died has no chance to write anything, so the pass has to be a value only a
 * child that ran every check could produce. */
#define BIT_CROSSED   0x01   /* elf_fork_child returned 0 */
#define BIT_SYM       0x02   /* dl_sym found the symbol after the fork */
#define BIT_CALL      0x04   /* the loaded code ran and answered */
#define BIT_SELF      0x08   /* the loader's own code is where it was */
#define BIT_DLL       0x10   /* cygwin1.dll is where it was */
#define BIT_TP        0x20   /* the thread pointer reads back as the TCB's */
#define BIT_ALL       0x3f

static uint64_t cygwin_base(void)
{
#if defined(__CYGWIN__) || defined(_WIN32)
	return (uint64_t)(uintptr_t)GetModuleHandleA("cygwin1.dll");
#else
	return 0;
#endif
}

/* Recorded in the parent before every fork and read by the child out of the
 * copy, which is the only way the child can know what the parent saw. */
static uint64_t parent_cygwin_base;
static uint64_t parent_self_addr;

static void here(void) { }
static uint64_t self_addr(void)
{
	void (*volatile p)(void) = here;
	return (uint64_t)(uintptr_t)p;
}

/* ---- the loader lock, as the surface will hold it ---------------------- */

/* dl_open does not take a lock of its own; WP-38 delivered a single-threaded
 * surface and said the thread-local error carrier arrives with this package.
 * The bracket is therefore modelled here exactly as the surface will hold it:
 * every entry into the loader is between an acquire and a release, and
 * elf_fork_prepare takes the same lock. That is what makes the fork wait. */
static elf_fork_state fs;

static void loader_enter(void) { fs.lock.acquire(fs.lock.ctx); }
static void loader_leave(void) { fs.lock.release(fs.lock.ctx); }

/* ---- the plugin -------------------------------------------------------- */

static dl_state st;
static const char *plugin_path;
static void *plugin_handle;

static int load_plugin(void)
{
	loader_enter();
	plugin_handle = dl_open(&st, plugin_path, RTLD_NOW | RTLD_GLOBAL);
	loader_leave();
	if (plugin_handle == NULL) {
		const char *e = dl_error(&st);
		printf("  FAIL dlopen: %s\n", e ? e : "(no reason)");
		return -1;
	}
	return 0;
}

/* ---- stage dlopen ------------------------------------------------------ */

static volatile int churn_stop;
static volatile int churn_cycles;

/* A thread that is inside the loader, with the lock held, for a wide window.
 * The sleep is inside the bracket on purpose: the race this stage is about is
 * not a narrow one that a lucky schedule misses, it is a fork arriving while
 * another thread genuinely is in the middle of a load. */
static void *churn(void *arg)
{
	(void)arg;
	while (!churn_stop) {
		loader_enter();
		void *h = dl_open(&st, plugin_path, RTLD_NOW | RTLD_LOCAL);
		usleep(2000);
		if (h != NULL)
			dl_close(&st, h);
		churn_cycles++;
		loader_leave();
		usleep(500);
	}
	return NULL;
}

/* One fork, with the child running the checks and reporting in its status. */
static int one_fork(int check_tp, elfsysv_tcb_t *tcb)
{
	unsigned char packed[4096];
	size_t packed_len = 0;

	parent_cygwin_base = cygwin_base();
	parent_self_addr = self_addr();

	if (elf_fork_prepare(&fs, elf_fork_flavor_fork, tcb) != 0) {
		printf("  FAIL prepare: %s\n", fs.why);
		return -1;
	}
	if (elf_fork_manifest_pack(&fs, packed, sizeof packed, &packed_len) != 0) {
		printf("  FAIL the manifest does not fit\n");
		elf_fork_parent(&fs);
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		elf_fork_parent(&fs);
		perror("  FAIL fork");
		return -1;
	}

	if (pid == 0) {
		/* A child that deadlocks is a child that never gets here again; the
		 * watchdog turns that into a signal the parent can tell apart from a
		 * status, so a hang cannot read as a slow pass. */
		alarm(30);

		int bits = 0;
		if (elf_fork_child(&fs, packed, packed_len) == 0)
			bits |= BIT_CROSSED;
		else
			fprintf(stderr, "  child: %s\n", fs.why);

		add_fn add = (add_fn)dl_sym(&st, plugin_handle, "plug_add");
		if (add != NULL)
			bits |= BIT_SYM;
		if (add != NULL && add(1, 2) == 45)
			bits |= BIT_CALL;

		if (self_addr() == parent_self_addr)
			bits |= BIT_SELF;
		if (cygwin_base() == parent_cygwin_base)
			bits |= BIT_DLL;

		if (!check_tp || (tcb != NULL && elfsysv_tp_get() == tcb->tp))
			bits |= BIT_TP;

		_exit(bits);
	}

	elf_fork_parent(&fs);

	int status = 0;
	if (waitpid(pid, &status, 0) != pid) {
		perror("  FAIL waitpid");
		return -1;
	}
	if (WIFSIGNALED(status)) {
		printf("  FAIL the child died on signal %d -- a deadlocked child is "
		       "killed by its watchdog\n", WTERMSIG(status));
		return -1;
	}
	if (!WIFEXITED(status)) {
		printf("  FAIL the child neither exited nor was signalled\n");
		return -1;
	}

	int bits = WEXITSTATUS(status);
	ok((bits & BIT_CROSSED) != 0, "the child's loader state audits equal");
	ok((bits & BIT_SYM) != 0, "dl_sym works in the child after the fork");
	ok((bits & BIT_CALL) != 0, "the loaded code runs in the child");
	ok((bits & BIT_SELF) != 0,
	   "the loader's own code is at the same address in the child");
	ok((bits & BIT_DLL) != 0, "cygwin1.dll did not rebase across the fork");
	ok((bits & BIT_TP) != 0, "the child's thread pointer is its TCB's");
	return bits == BIT_ALL ? 0 : -1;
}

static int stage_dlopen(int count)
{
	pthread_t th;
	churn_stop = 0;
	churn_cycles = 0;
	if (pthread_create(&th, NULL, churn, NULL) != 0) {
		printf("  FAIL cannot create the churn thread\n");
		return -1;
	}

	int rc = 0;
	for (int i = 0; i < count && rc == 0; i++)
		rc = one_fork(0, NULL);

	churn_stop = 1;
	pthread_join(th, NULL);

	ok(churn_cycles > 0, "the other thread really was inside dlopen");
	return rc;
}

/* ---- stage tls --------------------------------------------------------- */

static elf_tls_state tls;
static int tls_rc;
static int tls_count;

/* The fork is taken from here, on a stack this runtime owns, so the carrier
 * word DR-0003 reads through %gs is one that exists and one the child's
 * initial thread does not have. */
static volatile int tls_go;
static elfsysv_tcb_t *volatile tls_tcb;

static void *tls_thread(void *arg)
{
	(void)arg;

	/* The runtime allocates this thread's TCB as part of creating it, so the
	 * TLS block cannot be installed into it until the thread exists. The body
	 * waits for the creator to install and bind rather than racing it. */
	while (!tls_go)
		usleep(1000);

	elfsysv_tcb_t *tcb = tls_tcb;
	fs.tls = &tls;

	tls_rc = 0;
	for (int i = 0; i < tls_count && tls_rc == 0; i++)
		tls_rc = one_fork(1, tcb);
	return NULL;
}

static int stage_tls(int count)
{
	const char *why = NULL;
	if (elfsysv_tp_runtime_init(&why) != 0) {
		printf("  FAIL the thread pointer will not come up: %s\n",
		       why ? why : "(no reason)");
		return -1;
	}

	elf_tls_state_init(&tls, 0);

	/* Two modules, so the DTV has slots to compare rather than a length. */
	static const unsigned char img_a[16] = { 1, 2, 3, 4 };
	static const unsigned char img_b[32] = { 9 };
	elf_tls_module a = { img_a, sizeof img_a, 64, 16, 1, 0 };
	elf_tls_module b = { img_b, sizeof img_b, 128, 32, 1, 0 };
	ok(elf_tls_add_initial(&tls, &a) == 1, "the first TLS module is id 1");
	ok(elf_tls_add_initial(&tls, &b) == 2, "the second TLS module is id 2");
	elf_tls_finish_layout(&tls);

	elfsysv_tp_thread_t mt;
	memset(&mt, 0, sizeof mt);
	tls_count = count;

	/* elfsysv_tp_thread_create establishes the pointer for the thread and runs
	 * the body with it live. */
	tls_go = 0;
	if (elfsysv_tp_thread_create(&mt, (size_t)tls.static_size,
	                             tls_thread, NULL) != 0) {
		printf("  FAIL cannot create the managed thread\n");
		return -1;
	}

	/* The TCB the runtime allocated for that thread is the one the fork path
	 * must re-establish. Install the block into it and bind it, then release
	 * the body. */
	if (elf_tls_thread_install(&tls, mt.tcb) != 0) {
		printf("  FAIL the TLS block would not install\n");
		tls_go = 1;
		elfsysv_tp_thread_join(&mt, NULL);
		return -1;
	}
	tls_tcb = mt.tcb;
	tls_go = 1;

	elfsysv_tp_thread_join(&mt, NULL);
	return tls_rc;
}

/* ---- stage rebase ------------------------------------------------------ */

static int stage_rebase(int count)
{
	for (int i = 0; i < count; i++)
		if (one_fork(0, NULL) != 0)
			return -1;
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fputs("Usage: fork_e2e <dlopen|tls|rebase|all> <plugin> [count]\n",
		      stderr);
		return 2;
	}
	const char *stage = argv[1];
	plugin_path = argv[2];
	int count = argc > 3 ? atoi(argv[3]) : 4;
	if (count < 1)
		count = 1;

	dl_state_init(&st, NULL, argc, argv, NULL);
	elf_fork_state_init(&fs, &st, NULL, NULL, NULL);

	/* Every stage forks with an object loaded, because a loader with nothing
	 * in it has no object table to cross and would pass by having nothing to
	 * lose. */
	if (load_plugin() != 0)
		return 1;

	int rc = 0;
	if (!strcmp(stage, "dlopen") || !strcmp(stage, "all"))
		rc |= stage_dlopen(count) != 0;
	if (!strcmp(stage, "tls") || !strcmp(stage, "all"))
		rc |= stage_tls(count) != 0;
	if (!strcmp(stage, "rebase") || !strcmp(stage, "all"))
		rc |= stage_rebase(count > 8 ? count : 8) != 0;

	if (rc == 0 && failures == 0) {
		printf("wp42 e2e (%s): every child crossed intact\n", stage);
		return 0;
	}
	printf("wp42 e2e (%s): %d checks failed\n", stage, failures);
	return 1;
}
