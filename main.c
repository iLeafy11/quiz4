#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "thread.h"
#include "dthread.h"

#define PRECISION 100 /* upper bound in BPP sum */
/* Use Bailey–Borwein–Plouffe formula to approximate PI */
static void *bbp(void *arg) // mod
{
    int k = *(int *) arg;
    double sum = (4.0 / (8 * k + 1)) - (2.0 / (8 * k + 4)) -
                 (1.0 / (8 * k + 5)) - (1.0 / (8 * k + 6));
    double *product = malloc(sizeof(double));
    if (product)
        *product = 1 / pow(16, k) * sum;
    return (void *) product;
}

/*
static void *test(void *arg)
{
    sleep(10);
    int i = *(int *) arg;
    double *product = malloc(sizeof(double));
    if (product) {
        *product = (double) i;
    }
    return (void *) product;
}
*/

#include "leibniz.h"

int main()
{
    int bbp_args[PRECISION + 1]; // mod
    double bbp_sum = 0; // mod
    tpool_t pool = tpool_create(4);
    tpool_future_t futures[PRECISION + 1];

    for (int i = 0; i <= PRECISION; i++) {
        bbp_args[i] = i; // mod
        futures[i] = tpool_apply(pool, bbp, (void *) &bbp_args[i]); // bbp
        // futures[i] = tpool_apply(pool, test, (void *) &bbp_args[i]); // test
    }

    for (int i = PRECISION; i >= 0; i--) {
        double *result = tpool_future_get(futures[i], 1 /* blocking wait */);
        if (!result) {
            printf("%d\n", i);
            continue;
        }
        bbp_sum += *result; // mod
        // printf ("%.15f\n", *result); // test
        tpool_future_destroy(futures[i]);
        free(result);
        
    }

    tpool_join(pool);
    printf("PI calculated with %d terms: %.15f\n", PRECISION + 1, bbp_sum); // mod

    return 0;
}
