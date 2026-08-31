/* sysv-ipc slice: semaphores — a private set of three starts at the
   values SETALL plants, GETVAL and GETALL read them back, semop
   applies its whole array atomically, IPC_NOWAIT turns a would-block
   into EAGAIN without touching any value, GETPID names this process,
   and GETNCNT/GETZCNT are zero when nobody waits. */
#define _GNU_SOURCE
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    unsigned short init[3] = { 2, 0, 5 };
    unsigned short back[3];
    struct sembuf ops[2];
    int id;

    id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0600);
    printf("set created %d\n", id != -1);

    printf("setall %d\n", semctl(id, 0, SETALL, init) == 0);
    printf("getval 0 %d\n", semctl(id, 0, GETVAL));
    printf("getval 2 %d\n", semctl(id, 2, GETVAL));

    /* take one from sem 0 and give one to sem 1, atomically */
    ops[0].sem_num = 0; ops[0].sem_op = -1; ops[0].sem_flg = 0;
    ops[1].sem_num = 1; ops[1].sem_op = +1; ops[1].sem_flg = 0;
    printf("semop pair %d\n", semop(id, ops, 2) == 0);
    memset(back, 0, sizeof back);
    semctl(id, 0, GETALL, back);
    printf("after pair %d %d %d\n", back[0], back[1], back[2]);

    /* an array that cannot proceed leaves every value untouched */
    ops[0].sem_num = 1; ops[0].sem_op = -1; ops[0].sem_flg = 0;
    ops[1].sem_num = 0; ops[1].sem_op = -2; ops[1].sem_flg = IPC_NOWAIT;
    errno = 0;
    printf("nowait blocks %d %s\n",
           semop(id, ops, 2) == -1, strerror(errno));
    memset(back, 0, sizeof back);
    semctl(id, 0, GETALL, back);
    printf("untouched %d %d %d\n", back[0], back[1], back[2]);

    printf("getpid is ours %d\n",
           semctl(id, 0, GETPID) == (int)getpid());
    printf("waiters %d %d\n",
           semctl(id, 0, GETNCNT), semctl(id, 0, GETZCNT));

    printf("removed %d\n", semctl(id, 0, IPC_RMID) == 0);
    errno = 0;
    printf("getval after removal %d %s\n",
           semctl(id, 0, GETVAL) == -1, strerror(errno));
    return 0;
}
