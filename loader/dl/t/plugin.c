/* WP-38 e2e specimen: a real ELF shared object, cross-linked for the triple.
 *
 * It carries what a plugin carries and what the done-when needs to see: a
 * constructor in DT_INIT_ARRAY, a destructor in DT_FINI_ARRAY, an exported
 * function and an exported datum, a relocation that has to be applied before
 * either works, and unwind tables, so the linker emits PT_GNU_EH_FRAME and an
 * unwinder walking dl_iterate_phdr has something to find.
 *
 * It is freestanding: no libc, nothing to call. Everything it does, it does to
 * its own memory.
 */

/* Set by the constructor, cleared by the destructor. The driver reads it
 * through dlsym to see that initialization actually ran, and that it ran
 * before dlopen returned. */
int plug_ready = 0;

/* Incremented by every constructor run, never cleared: after ten thousand
 * load/unload cycles a freshly loaded copy must still read 1, because each
 * copy is a fresh mapping of the file and not a survivor of the last one. */
int plug_init_count = 0;

/* A pointer into the object itself, which only a relocation can fill in. If
 * relocation did not happen, this is null and the driver sees it. */
static int plug_answer_value = 42;
int *plug_answer_ptr = &plug_answer_value;

int plug_answer(void)
{
	return *plug_answer_ptr;
}

int plug_add(int a, int b)
{
	return a + b + plug_answer_value;
}

__attribute__((constructor))
static void plug_ctor(void)
{
	plug_ready = 1;
	plug_init_count++;
}

__attribute__((destructor))
static void plug_dtor(void)
{
	plug_ready = 0;
}
