/* misc slice: the getopt family over fixed argv arrays. Short options
   with and without arguments, the unknown-option and missing-argument
   protocols under opterr 0, glibc's permutation moving operands to the
   tail, and the long forms -- getopt_long with a flag-setting row and
   getopt_long_only's single-dash spelling. POSIXLY_CORRECT is cleared
   so both sides permute. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

int main(void)
{
    int c, i;

    unsetenv("POSIXLY_CORRECT");
    opterr = 0;

    {
        char *av[] = { "case", "-a", "-bval", "foo", 0 };
        optind = 0;
        while ((c = getopt(4, av, "ab:")) != -1)
            printf("short %c %s\n", c, optarg ? optarg : "-");
        printf("shortend %d %s\n", optind, av[optind]);
    }

    {
        char *av[] = { "case", "-x", "-b", 0 };
        optind = 0;
        c = getopt(3, av, "ab:");
        printf("unknown %d %d\n", c == '?', optopt == 'x');
        c = getopt(3, av, "ab:");
        printf("missing %d %d\n", c == '?', optopt == 'b');
    }

    {
        char *av[] = { "case", "-b", 0 };
        optind = 0;
        c = getopt(2, av, ":ab:");
        printf("colon %d %d\n", c == ':', optopt == 'b');
    }

    {
        char *av[] = { "case", "op1", "-a", "op2", "-bv", 0 };
        optind = 0;
        while ((c = getopt(5, av, "ab:")) != -1)
            printf("perm %c %s\n", c, optarg ? optarg : "-");
        printf("permind %d\n", optind);
        for (i = optind; i < 5; i++)
            printf("operand %s\n", av[i]);
    }

    {
        int flagval = 0;
        struct option lo[] = {
            { "alpha", no_argument,       0,        'A' },
            { "beta",  required_argument, 0,        'B' },
            { "gamma", optional_argument, 0,        'G' },
            { "flag",  no_argument,       &flagval, 42  },
            { 0, 0, 0, 0 }
        };
        int li = -1;
        char *av[] = { "case", "--alpha", "--beta=seven",
                       "--gamma", "--flag", 0 };
        optind = 0;
        while ((c = getopt_long(5, av, "", lo, &li)) != -1)
            printf("long %d %s %d\n", c, optarg ? optarg : "-", li);
        printf("flagrow %d\n", flagval);
    }

    {
        struct option lo[] = {
            { "file", required_argument, 0, 'f' },
            { 0, 0, 0, 0 }
        };
        char *av[] = { "case", "-file", "x.txt", 0 };
        optind = 0;
        c = getopt_long_only(3, av, "", lo, 0);
        printf("only %d %s\n", c == 'f', optarg ? optarg : "-");
    }

    return 0;
}
