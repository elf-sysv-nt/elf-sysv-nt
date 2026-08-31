/* misc slice: the hsearch hash tables, global and reentrant. ENTER
   inserts and hands back the new row, FIND answers from what ENTER
   stored, a missing key is NULL with ESRCH, and a second ENTER of the
   same key keeps the first row's data. The _r family says the same
   through its int-and-out-parameter protocol over a caller's table,
   with the two tables independent. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <search.h>

int main(void)
{
    ENTRY e, *r;

    printf("hcreate %d\n", hcreate(30) != 0);

    e.key = (char *)"alpha";
    e.data = (char *)"one";
    r = hsearch(e, ENTER);
    printf("enter %d\n", r && strcmp(r->data, "one") == 0);

    e.key = (char *)"beta";
    e.data = (char *)"two";
    r = hsearch(e, ENTER);
    printf("enter2 %d\n", r != 0);

    e.key = (char *)"alpha";
    e.data = 0;
    r = hsearch(e, FIND);
    printf("find %d\n", r && strcmp(r->data, "one") == 0);

    errno = 0;
    e.key = (char *)"gamma";
    r = hsearch(e, FIND);
    printf("miss %d %d\n", r == 0, errno == ESRCH);

    e.key = (char *)"alpha";
    e.data = (char *)"clobber";
    r = hsearch(e, ENTER);
    printf("dup %d\n", r && strcmp(r->data, "one") == 0);

    hdestroy();

    {
        struct hsearch_data ha, hb;
        ENTRY *rp;
        int rc;

        memset(&ha, 0, sizeof ha);
        memset(&hb, 0, sizeof hb);
        printf("rcreate %d %d\n",
               hcreate_r(20, &ha) != 0, hcreate_r(20, &hb) != 0);

        e.key = (char *)"only-in-a";
        e.data = (char *)"here";
        rc = hsearch_r(e, ENTER, &rp, &ha);
        printf("renter %d %d\n", rc != 0, rp != 0);

        e.data = 0;
        rc = hsearch_r(e, FIND, &rp, &ha);
        printf("rfind %d %d\n", rc != 0,
               rc && strcmp(rp->data, "here") == 0);

        errno = 0;
        rc = hsearch_r(e, FIND, &rp, &hb);
        printf("rsplit %d %d\n", rc == 0, errno == ESRCH);

        hdestroy_r(&ha);
        hdestroy_r(&hb);
        printf("rdone 1\n");
    }

    return 0;
}
