/* system slice: uname, sysinfo, and the get_*_pages / get_nprocs*
   family off sys/utsname.h and sys/sysinfo.h -- the six rows this
   slice wires.  Raw values (hostname, kernel release, exact memory
   and CPU counts) differ between the reference machine and wherever
   this runs, so the case prints invariants rather than numbers: the
   uname fields' shape, sysinfo and the get_* family agreeing with
   each other, and the monotonic relationships a real machine always
   satisfies. */
#define _GNU_SOURCE
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct utsname u;
    int ur = uname(&u);
    printf("uname rc %d\n", ur);
    printf("sysname nonempty %d\n", u.sysname[0] != '\0');
    printf("release nonempty %d\n", u.release[0] != '\0');
    printf("machine nonempty %d\n", u.machine[0] != '\0');
    printf("nodename nonempty %d\n", u.nodename[0] != '\0');

    struct sysinfo si;
    int sr = sysinfo(&si);
    printf("sysinfo rc %d\n", sr);
    printf("uptime nonneg %d\n", si.uptime >= 0);
    printf("totalram positive %d\n", si.totalram > 0);
    printf("freeram le total %d\n", si.freeram <= si.totalram);
    printf("procs positive %d\n", si.procs > 0);

    long nconf = get_nprocs_conf();
    long n = get_nprocs();
    printf("nprocs positive %d\n", n > 0);
    printf("nprocs le conf %d\n", n <= nconf);

    long phys = get_phys_pages();
    long avphys = get_avphys_pages();
    printf("phys positive %d\n", phys > 0);
    printf("avphys le phys %d\n", avphys <= phys);

    return 0;
}
