/* WP-39 e2e specimen: the root program. It names the second object in
 * DT_NEEDED by calling into it, and carries a DT_RUNPATH/DT_RPATH so the graph
 * walk resolves that name to a file. It is never run here -- the graph reads its
 * dynamic section statically -- so a bare entry point and a reference to the
 * library are all it needs. */
extern int second(void);

int root_value;

void entry(void)
{
	root_value = second();
}
