/* misc slice: the tsearch tree, the linear searchers, and the insque
   queue. The tree is grown from a fixed insert order so twalk's
   traversal is the same on both sides, tfind distinguishes present
   from absent without inserting, tdelete hands back the parent, and
   tdestroy tears it down through a counting free. lsearch appends
   where lfind refuses, and insque/remque edit a linear list observed
   by walking it. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static void visit(const void *node, VISIT v, int depth)
{
    if (v == postorder || v == leaf)
        printf("walk %d %d\n", **(int *const *)node, depth);
}

static int destroyed;
static void count_free(void *p)
{
    (void)p;
    destroyed++;
}

struct elem {
    struct elem *fwd;
    struct elem *bck;
    int v;
};

int main(void)
{
    static int keys[] = { 50, 30, 70, 20, 40, 60, 80 };
    void *root = 0;
    unsigned i;

    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        void *r = tsearch(&keys[i], &root, cmp_int);
        printf("insert %d %d\n", keys[i], r && *(int **)r == &keys[i]);
    }
    {
        int again = 50;
        void *r = tsearch(&again, &root, cmp_int);
        printf("dup %d\n", r && *(int **)r == &keys[0]);
    }
    {
        int want = 40, miss = 45;
        void *r = tfind(&want, &root, cmp_int);
        printf("find %d\n", r && **(int **)r == 40);
        r = tfind(&miss, &root, cmp_int);
        printf("miss %d\n", r == 0);
    }
    twalk(root, visit);
    {
        int gone = 20;
        void *r = tdelete(&gone, &root, cmp_int);
        printf("delete %d\n", r != 0);
        r = tfind(&gone, &root, cmp_int);
        printf("deleted %d\n", r == 0);
    }
    tdestroy(root, count_free);
    printf("destroyed %d\n", destroyed);

    {
        int tab[8] = { 3, 1, 4 };
        size_t n = 3;
        int key = 4, absent = 9;
        int *r = lfind(&key, tab, &n, sizeof tab[0], cmp_int);
        printf("lfind %d %zu\n", r && *r == 4, n);
        r = lfind(&absent, tab, &n, sizeof tab[0], cmp_int);
        printf("lfindmiss %d %zu\n", r == 0, n);
        r = lsearch(&absent, tab, &n, sizeof tab[0], cmp_int);
        printf("lsearch %d %zu %d\n", r && *r == 9, n, tab[3]);
    }

    {
        struct elem a = { 0, 0, 1 }, b = { 0, 0, 2 }, c = { 0, 0, 3 };
        struct elem *p;
        insque(&a, 0);
        insque(&b, &a);
        insque(&c, &b);
        for (p = &a; p; p = p->fwd)
            printf("queue %d\n", p->v);
        remque(&b);
        for (p = &a; p; p = p->fwd)
            printf("after %d\n", p->v);
        printf("back %d\n", c.bck == &a);
    }

    return 0;
}
