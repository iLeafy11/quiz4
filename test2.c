#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "list.h"

struct pool {
    int val;
};

struct pool *func(void *arg)
{
    return container_of((int *)arg, struct pool, val);
}

int main(int argc, char *argv[])
{
    struct pool *p = malloc(sizeof(struct pool));
    p->val = 9;
    printf("%p, %p\n", p, func(&p->val));
    return 0;
}
