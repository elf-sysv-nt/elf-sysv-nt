/* memory slice: the mapping family -- mmap anonymous and file-backed,
   mprotect, msync, munmap, and the 64 twin over the same page. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int main(void)
{
    long page = sysconf(_SC_PAGESIZE);
    size_t len = (size_t)page;
    char *m;
    char tmpl[] = "/tmp/esn-map-XXXXXX";
    int fd, rc;
    char buf[16];

    /* Anonymous, private, read-write: write and read back. */
    m = mmap(NULL, len, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("anon %d\n", m != MAP_FAILED);
    m[0] = 'a'; m[len - 1] = 'z';
    printf("rw %d\n", m[0] == 'a' && m[len - 1] == 'z');

    /* Drop the write bit; the page stays readable. */
    rc = mprotect(m, len, PROT_READ);
    printf("mprotect %d %d\n", rc == 0, m[0] == 'a');
    printf("munmap %d\n", munmap(m, len) == 0);

    /* Zero length is refused with EINVAL before any mapping exists. */
    errno = 0;
    m = mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("zerolen %d %d\n", m == MAP_FAILED, errno == EINVAL);

    /* File-backed shared mapping: store through the map, sync, and read
       the bytes back through the descriptor. */
    fd = mkstemp(tmpl);
    printf("tmp %d\n", fd >= 0);
    rc = ftruncate(fd, (off_t)len);
    printf("size %d\n", rc == 0);
    m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    printf("filemap %d\n", m != MAP_FAILED);
    memcpy(m, "through the map", 15);
    printf("msync %d\n", msync(m, len, MS_SYNC) == 0);
    printf("munmap2 %d\n", munmap(m, len) == 0);
    printf("readback %d %d\n",
           (int)pread(fd, buf, 15, 0), memcmp(buf, "through the map", 15) == 0);

    /* The 64 twin maps the same file the same way. */
    m = mmap64(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    printf("mmap64 %d %d\n", m != MAP_FAILED,
           m != MAP_FAILED && memcmp(m, "through the map", 15) == 0);
    if (m != MAP_FAILED)
        munmap(m, len);

    close(fd);
    unlink(tmpl);
    return 0;
}
