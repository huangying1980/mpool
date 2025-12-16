#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "mpool.h"

#define N 10
struct node {
    struct node* next;
};

struct node* head = NULL;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void* thread_func(void *arg)
{
    int i;
    struct node* node;
    printf("%s is call\n", __func__);
    while (!head) {
        usleep(50);
        continue;
    }

    while (head)  {
        node = (struct node *)head;
        head = head->next;
        printf("call mpool_free_mt node %p\n", node);
        mpool_free_mt(node);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(void)
{
    struct node* node;
    int   i;
    pthread_t tid;

    for (i = 0; i < N; i++) {
        node = mpool_alloc_mt(200 << 10);
        if (!node) {
            fprintf(stderr, "mpool_alloc failed\n");
            return -1;
        }
        fprintf(stderr, "node %p\n", node);
        node->next = head;
        head = node;
    }
    pthread_create(&tid, NULL, thread_func, NULL);
    pthread_join(tid, NULL);
    node = mpool_alloc_mt(49 << 10);
    if (node) {
        fprintf(stderr, "node %p", node);
        mpool_free_mt(node);
    } 
    mpool_cleanup_mt();

    return 0;
}
