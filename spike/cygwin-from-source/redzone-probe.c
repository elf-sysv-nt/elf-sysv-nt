/* Spike 9: does the red zone survive a real Cygwin signal delivery?
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../LICENSE.

   Spike 7 asked this of a model built on spike 3's hijack.  This asks it of
   cygwin1.dll itself, built from source, with and without the two-line
   reservation in sigdelayed.  The shape of the measurement is deliberately
   spike 7's, because comparing the two transcripts is half the point.

   The thing that makes it a measurement rather than a hope is the covered
   round.  A round writes a sentinel below %rsp, waits, and reads it back; a
   round only says anything if a signal was delivered while the sentinel was
   live.  A first version of this probe counted every round and reported the
   red zone intact after five deliveries in twenty thousand rounds, none of
   which had landed in the window.  That is the inconclusive case wearing the
   answer's clothes, so covered rounds are now counted separately and a run
   with none of them refuses to give a verdict.

   Built without -mno-red-zone.  This program wants the red zone; that is the
   whole point of it.  */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

static volatile sig_atomic_t delivered;

static void
onsig (int sig)
{
  (void) sig;
  delivered++;
}

struct result
{
  unsigned long rounds;		/* windows opened */
  unsigned long covered;	/* windows a delivery landed inside */
  unsigned long broken;		/* covered windows that lost the sentinel */
  unsigned long first_bad;	/* what came back the first time it broke */
};

/* noinline, and nothing is called from the loop: the red zone is only the red
   zone in a leaf, and the moment this frame makes a call the callee owns
   those bytes legitimately.  The asm is what keeps the sentinel there and
   keeps the optimiser from proving the whole thing dead.  */
static __attribute__ ((noinline)) void
carry_in_red_zone (unsigned long want, unsigned long rounds, unsigned spin,
		   int control, struct result *out)
{
  memset (out, 0, sizeof *out);

  for (unsigned long n = 0; n < rounds; n++)
    {
      unsigned long seen;
      int before = delivered;

      /* The control arm exists because a probe that cannot see a clobber
	 reports every platform as clean.  A call writes its return address
	 at -8(%rsp), which is the same shape as the clobber being hunted, so
	 running with it on must report the red zone destroyed.  If it does
	 not, nothing this probe says about the real path means anything.  */
      if (control)
	__asm__ __volatile__ (
	  "movq %1, -8(%%rsp)\n\t"
	  "call 2f\n\t"
	  "jmp 3f\n"
	  "2:\n\t"
	  "ret\n"
	  "3:\n\t"
	  "movq -8(%%rsp), %0"
	  : "=&r" (seen)
	  : "r" (want)
	  : "memory");
      else
	__asm__ __volatile__ (
	"movq %1, -8(%%rsp)\n\t"
	"movq %2, %%rcx\n"
	"1:\n\t"
	"pause\n\t"
	"decq %%rcx\n\t"
	"jnz 1b\n\t"
	"movq -8(%%rsp), %0"
	: "=&r" (seen)
	: "r" (want), "r" ((unsigned long) spin)
	: "rcx", "memory");

      int after = delivered;
      out->rounds++;

      if (!control && after == before)
	continue;		/* nothing landed here; the round says nothing */

      out->covered++;
      if (seen != want)
	{
	  if (!out->broken)
	    out->first_bad = seen;
	  out->broken++;
	}
    }
}

int
main (int argc, char **argv)
{
  unsigned long rounds = (argc > 1) ? strtoul (argv[1], NULL, 0) : 400000;
  unsigned spin = (argc > 2) ? (unsigned) strtoul (argv[2], NULL, 0) : 4000;
  unsigned usec = (argc > 3) ? (unsigned) strtoul (argv[3], NULL, 0) : 200;
  int control = (argc > 4) && strcmp (argv[4], "control") == 0;
  const unsigned long sentinel = 0x5A5A5A5A5A5A5A5AUL;
  struct itimerval it;
  struct result r;

  if (signal (SIGALRM, onsig) == SIG_ERR)
    {
      fprintf (stderr, "cannot install a handler\n");
      return 2;
    }

  it.it_interval.tv_sec = 0;
  it.it_interval.tv_usec = usec;
  it.it_value = it.it_interval;
  if (setitimer (ITIMER_REAL, &it, NULL) != 0)
    {
      fprintf (stderr, "cannot arm the timer\n");
      return 2;
    }

  carry_in_red_zone (sentinel, rounds, spin, control, &r);

  memset (&it, 0, sizeof it);
  setitimer (ITIMER_REAL, &it, NULL);

  printf ("arm=%s\n", control ? "control" : "measure");
  printf ("rounds=%lu\n", r.rounds);
  printf ("deliveries=%d\n", (int) delivered);
  printf ("covered_rounds=%lu\n", r.covered);
  printf ("broken=%lu\n", r.broken);
  printf ("sentinel=0x%lx\n", sentinel);
  if (r.broken)
    printf ("first_clobber=0x%lx\n", r.first_bad);

  if (r.covered == 0)
    {
      printf ("verdict=inconclusive: no delivery landed in the window\n");
      return 3;
    }

  printf ("verdict=%s\n", r.broken ? "red-zone-destroyed" : "red-zone-intact");
  return r.broken ? 1 : 0;
}
