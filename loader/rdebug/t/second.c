/* WP-39 e2e specimen: the second object. Its only purpose is to be a shared
 * library the root names in DT_NEEDED, so the graph walk has a second object to
 * find and the rendezvous has one to announce. */
int second(void)
{
	return 42;
}
