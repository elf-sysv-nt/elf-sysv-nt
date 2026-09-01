/*
 * The init-crossing specimen's body and its two initializers. It certifies the
 * loader runs a dynamic image's DT_INIT chain, in ABI order, before entry.
 *
 * A single global g is written by the initializers in a way that only the right
 * order can reach: DT_INIT (my_dt_init, named by -Wl,-init) requires g==0 and
 * leaves g==2; the DT_INIT_ARRAY entry (ctor, a constructor attribute) requires
 * g==2 and leaves g==42. Any other order or a skipped stage poisons g to a
 * value that is not 42, so a process exit of 42 is reached only when DT_INIT
 * ran first, then DT_INIT_ARRAY, both before the entry read g.
 *
 * my_dt_init also checks argc>=1, the floor the (argc,argv,envp) convention
 * always meets (argv[0] is the program), so a call made with the wrong vector
 * poisons rather than passing.
 *
 * The DT_INIT_ARRAY entry also calls greet(), imported from the runtime, so
 * the image genuinely carries a DT_NEEDED and a PLT the crossing must resolve --
 * this specimen is dynamic in shape and in fact, not an executable dressed as
 * one -- and reaching 42 also means the crossing relocated that call, since a
 * constructor runs after the link that precedes the entry.
 *
 * Freestanding: the crossing under test is the loader's, so the specimen brings
 * no libc; g lives in the image and the initializers only touch it and greet.
 */

/* Imported from the runtime (libgreet.so); returns 42. Its call in ctor forces
 * a real DT_NEEDED and PLT, and certifies the crossing resolved it. */
extern int greet(void);

/* Not static, and not const-foldable: the linker must place real function
 * addresses in .init and .init_array, and the entry must read a real global. */
volatile int g = 0;

/* DT_INIT: set by -Wl,-init,my_dt_init on the link line. Runs first. */
void my_dt_init(int argc, char **argv, char **envp)
{
	(void) argv;
	(void) envp;
	if (g == 0 && argc >= 1)
		g = 2;
	else
		g = 100;
}

/* DT_INIT_ARRAY: a constructor attribute lands its address in .init_array,
 * which the linker records as DT_INIT_ARRAY. Runs after DT_INIT. */
__attribute__((constructor))
static void ctor(int argc, char **argv, char **envp)
{
	(void) argc;
	(void) argv;
	(void) envp;
	if (g == 2 && greet() == 42)
		g += 40;   /* -> 42, the pass: DT_INIT ran first, the crossing resolved greet */
	else
		g = 200;   /* out of order, DT_INIT skipped, or the crossing did not resolve greet */
}

/* The entry reads g and carries it out as the exit status. Reached after the
 * initializers when the loader ran them; a status of 42 is the only pass. */
int init_body(void)
{
	return g;
}
