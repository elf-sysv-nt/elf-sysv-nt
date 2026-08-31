/* sysv-ipc slice: message queues -- msgget with IPC_PRIVATE creates a
   private queue, msgsnd/msgrcv round-trip a typed message including a
   zero-length body, MSG_NOERROR truncates an oversized request instead
   of failing, a type-selective receive (a negative type ceiling and an
   exact positive type) picks the right message out of several queued
   at once, IPC_NOWAIT turns a would-block into ENOMSG, IPC_STAT reports
   the queue depth, and removal makes a further send fail with EIDRM. */
#define _GNU_SOURCE
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct qmsg {
    long mtype;
    char mtext[32];
};

int main(void)
{
    struct msqid_ds ds;
    struct qmsg out, in;
    int id;

    id = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    printf("queue created %d\n", id != -1);

    out.mtype = 1;
    memset(out.mtext, 0, sizeof out.mtext);
    printf("send zero length %d\n",
           msgsnd(id, &out, 0, 0) == 0);
    memset(&in, 0, sizeof in);
    printf("recv zero length %d\n",
           msgrcv(id, &in, sizeof in.mtext, 0, 0) == 0);

    out.mtype = 5;
    strcpy(out.mtext, "hello queue");
    printf("send roundtrip %d\n",
           msgsnd(id, &out, strlen(out.mtext) + 1, 0) == 0);
    memset(&in, 0, sizeof in);
    printf("recv roundtrip %d %ld %s\n",
           msgrcv(id, &in, sizeof in.mtext, 5, 0) > 0,
           in.mtype, in.mtext);

    {
        struct { long mtype; char mtext[64]; } big;
        big.mtype = 7;
        strcpy(big.mtext, "this line is much too long for a four byte body");
        msgsnd(id, &big, strlen(big.mtext) + 1, 0);
    }
    memset(&in, 0, sizeof in);
    errno = 0;
    printf("truncate without noerror %d %s\n",
           msgrcv(id, &in, 4, 7, 0) == -1, strerror(errno));
    memset(&in, 0, sizeof in);
    printf("truncate with noerror %d\n",
           msgrcv(id, &in, 4, 7, MSG_NOERROR) == 4);

    out.mtype = 2; strcpy(out.mtext, "type two");
    msgsnd(id, &out, strlen(out.mtext) + 1, 0);
    out.mtype = 3; strcpy(out.mtext, "type three");
    msgsnd(id, &out, strlen(out.mtext) + 1, 0);
    memset(&in, 0, sizeof in);
    printf("negative type picks lowest %d %ld\n",
           msgrcv(id, &in, sizeof in.mtext, -3, 0) > 0, in.mtype);
    memset(&in, 0, sizeof in);
    printf("exact type remaining %d %ld %s\n",
           msgrcv(id, &in, sizeof in.mtext, 3, 0) > 0, in.mtype, in.mtext);

    errno = 0;
    printf("nowait on empty queue %d %s\n",
           msgrcv(id, &in, sizeof in.mtext, 0, IPC_NOWAIT) == -1,
           strerror(errno));

    out.mtype = 9; strcpy(out.mtext, "still here");
    msgsnd(id, &out, strlen(out.mtext) + 1, 0);
    memset(&ds, 0, sizeof ds);
    msgctl(id, IPC_STAT, &ds);
    printf("qnum before drain %d\n", (int)ds.msg_qnum);
    memset(&in, 0, sizeof in);
    msgrcv(id, &in, sizeof in.mtext, 0, 0);
    memset(&ds, 0, sizeof ds);
    msgctl(id, IPC_STAT, &ds);
    printf("qnum after drain %d\n", (int)ds.msg_qnum);

    printf("removed %d\n", msgctl(id, IPC_RMID, 0) == 0);
    errno = 0;
    out.mtype = 1;
    printf("send after removal %d %s\n",
           msgsnd(id, &out, 1, 0) == -1, strerror(errno));
    return 0;
}
