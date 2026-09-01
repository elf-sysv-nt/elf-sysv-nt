/*
 * The dynamic specimen's body, called from its assembly entry. Its one
 * statement is the cross-object call the crossing is certified by: greet is
 * undefined here and satisfied by the runtime's DT_NEEDED, so the compiler
 * routes it through this image's PLT, and the value it returns becomes the
 * process exit status. Writing the body in C rather than in the entry stub
 * gives the image the unwind tables and read-only section a real program
 * carries, so its segment layout is an ordinary one rather than an assembly
 * corner case.
 */
extern int greet(void);

int dyn_body(void)
{
	return greet();
}
