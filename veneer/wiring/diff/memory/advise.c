/* memory slice: paging advice and page locking -- madvise, posix_madvise,
   mlock, munlock -- over one anonymous page. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void)
{
    long page = sysconf(_SC_PAGESIZE);
    size_t len = (size_t)page;
    char *m;
    int rc;

    m = mmap(NULL, len, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("anon %d\n", m != MAP_FAILED);
    m[0] = 'x';

    printf("madv-normal %d\n", madvise(m, len, MADV_NORMAL) == 0);
    printf("madv-seq %d\n", madvise(m, len, MADV_SEQUENTIAL) == 0);
    printf("madv-willneed %d\n", madvise(m, len, MADV_WILLNEED) == 0);
    errno = 0;
    rc = madvise(m, len, -1);
    printf("madv-bad %d %d\n", rc == -1, errno == EINVAL);

    /* posix_madvise returns the error, never sets errno. */
    printf("pmadv %d\n", posix_madvise(m, len, POSIX_MADV_WILLNEED) == 0);
    printf("pmadv-bad %d\n", posix_madvise(m, len, -1) == EINVAL);

    /* One page locks within any sane RLIMIT_MEMLOCK; unlock undoes it. */
    rc = mlock(m, len);
    printf("mlock %d\n", rc == 0);
    printf("munlock %d\n", munlock(m, len) == 0);
    printf("still %d\n", m[0] == 'x');

    printf("munmap %d\n", munmap(m, len) == 0);
    return 0;
}
