/* sysv-ipc slice: shared memory end to end — a private segment is
   created, attached, stamped, and seen through a second attachment;
   IPC_STAT reports the size and attach count; detaching drops the
   count; removal makes new attachments fail with EINVAL while the
   existing mapping survives until its own detach. */
#define _GNU_SOURCE
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct shmid_ds ds;
    int id;
    char *p, *q;

    id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    printf("segment created %d\n", id != -1);

    p = shmat(id, 0, 0);
    printf("attached %d\n", p != (void *)-1);
    strcpy(p, "stamped through the first attachment");

    q = shmat(id, 0, SHM_RDONLY);
    printf("second attachment sees %s\n", q);

    memset(&ds, 0, sizeof ds);
    shmctl(id, IPC_STAT, &ds);
    printf("size %d attached %d\n",
           (int)ds.shm_segsz, (int)ds.shm_nattch);

    shmdt(q);
    memset(&ds, 0, sizeof ds);
    shmctl(id, IPC_STAT, &ds);
    printf("after detach attached %d\n", (int)ds.shm_nattch);

    printf("removed %d\n", shmctl(id, IPC_RMID, 0) == 0);
    errno = 0;
    q = shmat(id, 0, 0);
    printf("attach after removal %d %s\n",
           q == (void *)-1, strerror(errno));
    printf("mapping survives removal %.7s\n", p);
    printf("final detach %d\n", shmdt(p) == 0);
    return 0;
}
