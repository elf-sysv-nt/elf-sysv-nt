/* Wide-window variant: watch N bytes below %rsp and report where the nearest
   write landed, which is the shape spike 3 reported and therefore the shape
   that can be compared with it. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define WATCH 512

static volatile sig_atomic_t delivered;
static void onsig (int s) { (void) s; delivered++; }

static volatile unsigned long nearest = ~0UL, furthest;
static volatile unsigned long covered, dirty;

static __attribute__ ((noinline)) void
watch (unsigned long rounds, unsigned spin)
{
  const unsigned long pat = 0xC3C3C3C3C3C3C3C3UL;

  for (unsigned long n = 0; n < rounds; n++)
    {
      int before = delivered;
      unsigned long buf[WATCH / 8];

      /* Fill the window below %rsp, spin, then copy it back out. Both the
         fill and the copy are done in asm off %rsp so nothing the compiler
         does can move the region being watched. */
      __asm__ __volatile__ (
        "movq %2, %%rcx\n"          /* words */
        "movq %%rsp, %%rdi\n\t"
        "subq %3, %%rdi\n"          /* rdi = rsp - WATCH */
        "1:\n\t"
        "movq %1, (%%rdi)\n\t"
        "addq $8, %%rdi\n\t"
        "decq %%rcx\n\t"
        "jnz 1b\n\t"
        "movq %4, %%rcx\n"
        "2:\n\t"
        "pause\n\t"
        "decq %%rcx\n\t"
        "jnz 2b\n\t"
        "movq %2, %%rcx\n\t"
        "movq %%rsp, %%rsi\n\t"
        "subq %3, %%rsi\n\t"
        "movq %0, %%rdi\n"
        "3:\n\t"
        "movsq\n\t"
        "decq %%rcx\n\t"
        "jnz 3b"
        :
        : "r" (buf), "r" (pat), "r" ((unsigned long) (WATCH / 8)),
          "r" ((unsigned long) WATCH), "r" ((unsigned long) spin)
        : "rcx", "rdi", "rsi", "memory");

      if (delivered == before)
        continue;
      covered++;

      /* buf[WATCH/8 - 1] is the word nearest %rsp, at offset 8. */
      for (unsigned i = 0; i < WATCH / 8; i++)
        if (buf[i] != pat)
          {
            unsigned long off = WATCH - i * 8;
            if (off < nearest) nearest = off;
            if (off > furthest) furthest = off;
            dirty++;
          }
    }
}

int
main (int argc, char **argv)
{
  unsigned long rounds = (argc > 1) ? strtoul (argv[1], NULL, 0) : 200000;
  unsigned spin = (argc > 2) ? (unsigned) strtoul (argv[2], NULL, 0) : 4000;
  unsigned usec = (argc > 3) ? (unsigned) strtoul (argv[3], NULL, 0) : 200;
  struct itimerval it;

  signal (SIGALRM, onsig);
  memset (&it, 0, sizeof it);
  it.it_interval.tv_usec = usec; it.it_value.tv_usec = usec;
  setitimer (ITIMER_REAL, &it, NULL);

  watch (rounds, spin);

  memset (&it, 0, sizeof it);
  setitimer (ITIMER_REAL, &it, NULL);

  printf ("watched_bytes=%d\n", WATCH);
  printf ("deliveries=%d\n", (int) delivered);
  printf ("covered_rounds=%lu\n", covered);
  printf ("dirty_words=%lu\n", dirty);
  if (nearest == ~0UL) printf ("verdict=intact\n");
  else printf ("nearest=%lu furthest=%lu verdict=clobbered\n", nearest, furthest);
  return 0;
}
