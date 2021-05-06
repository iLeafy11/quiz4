#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "dthread.h"

enum __future_flags {
    __FUTURE_RUNNING = 01,
    __FUTURE_FINISHED = 02,
    __FUTURE_TIMEOUT = 04,
    __FUTURE_CANCELLED = 010,
    __FUTURE_DESTROYED = 020,
};

typedef struct __threadtask {
    void *(*func)(void *);
    void *arg;
    struct __tpool_future *future;
    struct list_head list; /* a circular doubly linked list structure */
} threadtask_t;

typedef struct __jobqueue {
    struct list_head head;
    size_t size;
    pthread_cond_t cond_nonempty;
    pthread_mutex_t rwlock;
} jobqueue_t;

struct __tpool_future {
    int flag;
    void *result;
    pthread_mutex_t mutex;
    pthread_cond_t cond_finished;
};

struct __threadpool {
    size_t count;
    pthread_t *workers;
    jobqueue_t *jobqueue;
};

static struct __tpool_future *tpool_future_create(void)
{
    struct __tpool_future *future = malloc(sizeof(struct __tpool_future));
    if (future) {
        future->flag = 0;
        future->result = NULL;
        pthread_mutex_init(&future->mutex, NULL);
        // pthread_cond_init(&future->cond_finished, NULL);
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_cond_init(&future->cond_finished, &attr);
        pthread_condattr_destroy(&attr);
    }
    return future;
}

int tpool_future_destroy(struct __tpool_future *future)
{
    if (future) {
        pthread_mutex_lock(&future->mutex);
        if (future->flag & (__FUTURE_FINISHED | __FUTURE_CANCELLED | __FUTURE_TIMEOUT)) {
            printf("future finished, cancelled, timeout should be destroyed.\n");
            pthread_mutex_unlock(&future->mutex);
            pthread_mutex_destroy(&future->mutex);
            pthread_cond_destroy(&future->cond_finished);
            free(future);
        } else {
            future->flag |= __FUTURE_DESTROYED;
            printf("future label destroyed, if future is not finished, cancelled, timeout.\n");
            pthread_mutex_unlock(&future->mutex); // will free it when fetching the jobs.
        }
    }
    return 0;
}

void *tpool_future_get(struct __tpool_future *future, unsigned int seconds)
{
    pthread_mutex_lock(&future->mutex);
    /* turn off the timeout bit set previously */
    future->flag &= ~__FUTURE_TIMEOUT;
    while ((future->flag & __FUTURE_FINISHED) == 0) {
        if (seconds) {
            struct timespec expire_time;
            clock_gettime(CLOCK_MONOTONIC, &expire_time);
            expire_time.tv_sec += seconds;
            int status = pthread_cond_timedwait(&future->cond_finished,
                                                &future->mutex, &expire_time);
            if (status == ETIMEDOUT) {
                future->flag |= __FUTURE_TIMEOUT;
                pthread_mutex_unlock(&future->mutex);
                return NULL;
            }
        } else {
            pthread_cond_wait(&future->cond_finished, &future->mutex);
            printf("wait for getting the future.\n");
        }
    }
    printf("return the result after getting the future.\n");
    pthread_mutex_unlock(&future->mutex); // allow tpool_future_destroy())
    return future->result;
}


static jobqueue_t *jobqueue_create(void)
{
    jobqueue_t *jobqueue = malloc(sizeof(jobqueue_t));
    if (jobqueue) {
        INIT_LIST_HEAD(&jobqueue->head);
        jobqueue->size = 0;
        pthread_cond_init(&jobqueue->cond_nonempty, NULL);
        pthread_mutex_init(&jobqueue->rwlock, NULL);
    }
    return jobqueue;
}

static void jobqueue_destroy(jobqueue_t *jobqueue) // questionable code
{
    threadtask_t *tmp;
    struct list_head *curr, *next;
    list_for_each_safe(curr, next, &jobqueue->head) {
        list_remove(curr);
        tmp = list_entry(curr, threadtask_t, list);
        pthread_mutex_lock(&tmp->future->mutex);
        if (tmp->future->flag & __FUTURE_DESTROYED) {
            printf("future destroyed. in jobqueue_destroy().\n");
            pthread_mutex_unlock(&tmp->future->mutex);
            pthread_mutex_destroy(&tmp->future->mutex);
            pthread_cond_destroy(&tmp->future->cond_finished);
            free(tmp->future);
            continue;
        } else {
            printf("%p ", &tmp->list);
            tmp->future->flag |= __FUTURE_CANCELLED;
            printf("future label cancelled, if the future is not destroyed.\n");
            pthread_mutex_unlock(&tmp->future->mutex);
        }
        free(tmp);
    }
    pthread_mutex_destroy(&jobqueue->rwlock);
    pthread_cond_destroy(&jobqueue->cond_nonempty);
    free(jobqueue);
}

static void __jobqueue_fetch_cleanup(void *arg) /* TODO: is it need to be locked */
{
    pthread_mutex_t *mutex = (pthread_mutex_t *) arg;
    pthread_mutex_unlock(mutex);
}

static void *jobqueue_pop(void *queue)
{
    jobqueue_t *jobqueue = (jobqueue_t *) queue;
    struct list_head *target = jobqueue->head.next;
    list_remove(target);
    jobqueue->size--;
    return (void *) target;
}

static void *jobqueue_fetch(void *queue)
{

    jobqueue_t *taskqueue = (jobqueue_t *) queue;
    threadtask_t *task;
    int old_state;
    pthread_cleanup_push(__jobqueue_fetch_cleanup, (void *) &taskqueue->rwlock);

    while (1) {
        pthread_mutex_lock(&taskqueue->rwlock);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
        pthread_testcancel();
        while (list_empty(&taskqueue->head)) { /* TODO: what if using jobqueue->size to replace isEmpty() ? */
            pthread_cond_wait(&taskqueue->cond_nonempty, &taskqueue->rwlock);
        }
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);

        /* pop queue */
        struct list_head *node = (struct list_head *) jobqueue_pop(taskqueue);
        task = list_entry(node, threadtask_t, list);
        
        pthread_mutex_unlock(&taskqueue->rwlock);

        if (task->func) { /* TODO: reduce branch(?) */
            pthread_mutex_lock(&task->future->mutex);
            if (task->future->flag & __FUTURE_CANCELLED) {
                printf("future cancelled, so delete the task.\n");
                pthread_mutex_unlock(&task->future->mutex);
                free(task);
                continue;
            } else {
                task->future->flag |= __FUTURE_RUNNING;
                printf("future running.\n");
                pthread_mutex_unlock(&task->future->mutex);
            }
            
            /* work on task */
            void *ret_value = task->func(task->arg);
            pthread_mutex_lock(&task->future->mutex);

            if (task->future->flag & __FUTURE_DESTROYED) {
                printf("future destroyed, so delete the future object of the task.\n");
                pthread_mutex_unlock(&task->future->mutex);
                pthread_mutex_destroy(&task->future->mutex);
                pthread_cond_destroy(&task->future->cond_finished);
                free(task->future);
            } else {
                task->future->flag |=  __FUTURE_FINISHED;
                task->future->result = ret_value;
                pthread_cond_broadcast(&task->future->cond_finished);
                printf("future finished, so the task is finished. broadcast cond_finished.\n");
                pthread_mutex_unlock(&task->future->mutex);
            }
            
            free(task);
        } else {
            pthread_mutex_destroy(&task->future->mutex);
            pthread_cond_destroy(&task->future->cond_finished);
            free(task->future);
            free(task);
            break;
        }

    }

    pthread_cleanup_pop(0);
    return NULL;
    // pthread_exit(NULL);
}

struct __threadpool *tpool_create(size_t count)
{
    jobqueue_t *jobqueue = jobqueue_create();
    struct __threadpool *pool = malloc(sizeof(struct __threadpool));
    printf("The address of the threadpool: %p\n", pool);
    if (!jobqueue || !pool) {
        if (jobqueue)
            jobqueue_destroy(jobqueue);
        free(pool);
        return NULL;
    }

    pool->count = count, pool->jobqueue = jobqueue;
    if ((pool->workers = malloc(count * sizeof(pthread_t)))) {
        for (int i = 0; i < count; i++) { // if one thread is failed, then destroy all
            if (pthread_create(&pool->workers[i], NULL, jobqueue_fetch,
                               (void *) jobqueue)) {
                for (int j = 0; j < i; j++)
                    pthread_cancel(pool->workers[j]);
                for (int j = 0; j < i; j++)
                    pthread_join(pool->workers[j], NULL);
                free(pool->workers);
                jobqueue_destroy(jobqueue);
                free(pool);
                return NULL;
            }
        }
        return pool;
    }

    jobqueue_destroy(jobqueue);
    free(pool);
    return NULL;
}

struct __tpool_future *tpool_apply(struct __threadpool *pool,
                                   void *(*func)(void *),
                                   void *arg)
{
    jobqueue_t *jobqueue = pool->jobqueue;
    threadtask_t *new_node = malloc(sizeof(threadtask_t));
    struct __tpool_future *future = tpool_future_create();
    /*
        if (func || arg) {
            future->flag = __FUTURE_CANCELLED;
        }
       */
    if (new_node && future) {
        new_node->func = func, new_node->arg = arg, new_node->future = future;
        pthread_mutex_lock(&jobqueue->rwlock);
        switch (jobqueue->size) {
            case 0:
                list_add_tail(&new_node->list, &jobqueue->head);
                pthread_cond_broadcast(&jobqueue->cond_nonempty);
                printf("After add things to tail, broadcast nonempty\n");
                break;
            default:
                list_add_tail(&new_node->list, &jobqueue->head);
                break;    
        }

        jobqueue->size++;
        pthread_mutex_unlock(&jobqueue->rwlock);
    } else if (new_node) {
        free(new_node);
        return NULL;
        /*
           new_node
           return new_node;
           */
    } else if (future) {
        free(future);
        // tpool_future_destroy(future);
        return NULL;
    }
    return future;
}

int tpool_join(struct __threadpool *pool)
{
    size_t num_threads = pool->count;
    for (int i = 0; i < num_threads; i++)
        tpool_apply(pool, NULL, NULL);
    // cancellation
    for (int i = 0; i < num_threads; i++) 
        pthread_cancel(pool->workers[i]);
    for (int i = 0; i < num_threads; i++)
        pthread_join(pool->workers[i], NULL);

    free(pool->workers);
    jobqueue_destroy(pool->jobqueue);
    free(pool);
    return 0;
}
 
