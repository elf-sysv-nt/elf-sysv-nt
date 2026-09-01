/*
 * The ELF runtime the dynamic-crossing certification links a specimen against.
 *
 * It stands where libc.so.6 stands for a real program: an ET_DYN the main
 * image names in a DT_NEEDED and calls across through a relocated PLT. One
 * exported function is enough to prove the crossing — greet() returns a
 * sentinel the specimen carries out as its exit status, so a status of that
 * value is reached only when the main image's GOT and PLT were resolved
 * against this object. Freestanding: the crossing under test is the loader's,
 * not a libc's, so the runtime brings no libc of its own.
 */
int greet(void)
{
	return 42;
}
