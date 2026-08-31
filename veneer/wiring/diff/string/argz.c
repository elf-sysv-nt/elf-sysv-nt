/* string slice: the argz/envz vector family, argz.h and envz.h.
 *
 * Waited on <linux/errno.h> (argz.h includes errno.h for the error_t
 * returns); the sysroot carries the el8 kernel headers now.
 */
#define _GNU_SOURCE
#include <argz.h>
#include <envz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *az = NULL, *ez = NULL, *entry;
    size_t az_len = 0, ez_len = 0;

    argz_create_sep("a:bb::ccc", ':', &az, &az_len);
    printf("count %zu\n", argz_count(az, az_len));
    for (entry = NULL; (entry = argz_next(az, az_len, entry)); )
        printf("entry %s\n", entry);

    argz_add(&az, &az_len, "dd");
    argz_insert(&az, &az_len, az, "zz");
    argz_delete(&az, &az_len, "bb");
    printf("count %zu\n", argz_count(az, az_len));
    argz_stringify(az, az_len, ',');
    printf("joined %s\n", az);
    free(az);

    envz_add(&ez, &ez_len, "HOME", "/tmp");
    envz_add(&ez, &ez_len, "EMPTY", NULL);
    envz_add(&ez, &ez_len, "TERM", "dumb");
    printf("get %s\n", envz_get(ez, ez_len, "HOME"));
    printf("get-null %d\n", envz_get(ez, ez_len, "EMPTY") == NULL);
    printf("entry %s\n", envz_entry(ez, ez_len, "TERM"));
    envz_remove(&ez, &ez_len, "HOME");
    printf("removed %d\n", envz_get(ez, ez_len, "HOME") == NULL);
    free(ez);
    return 0;
}
