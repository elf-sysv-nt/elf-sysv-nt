/* The static hello WP-14 has to link.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../../LICENSE.

   It prints nothing, because printing needs a libc and a libc needs the
   runtime, and WP-14 sits three packages before either.  What it does instead
   is touch one thing from each part of the image that a linker had to get
   right, and hand the result back as an exit status.

   That makes it a weaker hello than the traditional one and a stronger test.
   A program that prints has proved its write path; this one proves that .text
   was mapped executable, .rodata readable, .data initialised from the file,
   and .bss zeroed from nothing at all -- which is the set of claims WP-32
   makes about mapping and the set spike 2 measured against a hand-built
   image.  This is the same measurement against an image our own linker
   produced.  */

/* .rodata: initialised, and the segment holding it should be read-only.  */
static const unsigned long rodata_word = 0x5252525252525252UL;

/* .data: initialised, and writable.  */
static unsigned long data_word = 0x4444444444444444UL;

/* .bss: no bytes in the file, and every one of them has to arrive zero.
   Spike 2 found freshly committed pages arrive zeroed on this platform, so
   .bss is free; this is the assertion that it stayed free once a real linker
   was choosing the layout.

   volatile, and that is not decoration.  Without it the compiler proves the
   array is zero and never written, deletes the loop below, and deletes the
   array with it -- so the image ends up with no .bss at all and the test
   that checks for one passes against nothing.  */
static volatile unsigned long bss_words[512];

int
main (int argc, char **argv, char **envp)
{
  int status = 0;

  if (rodata_word != 0x5252525252525252UL)
    status |= 1;

  if (data_word != 0x4444444444444444UL)
    status |= 2;
  data_word = 0;
  if (data_word != 0)
    status |= 4;

  for (unsigned i = 0; i < sizeof bss_words / sizeof bss_words[0]; i++)
    if (bss_words[i] != 0)
      {
	status |= 8;
	break;
      }

  /* The stack the entry protocol built.  argc is at least one and argv[0] is
     addressable; a crt that read argc as a return address would fail here
     rather than at some later dereference.  */
  if (argc < 1 || argv == 0 || argv[0] == 0)
    status |= 16;

  /* envp has to be reachable and terminated.  crt1 computes it by scale-index
     past argv's terminator, which is the arithmetic most easily got wrong by
     eight.  */
  if (envp == 0)
    status |= 32;
  else
    {
      unsigned n = 0;
      while (envp[n] != 0 && n < 4096)
	n++;
      if (n >= 4096)
	status |= 64;
    }

  return status;
}
