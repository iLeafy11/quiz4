#include <stdio.h>
#include <pthread.h>
// remember to set compilation option -pthread

void *busy(void *ptr) {
// ptr will point to "Hi"
    puts("Hello World");
    printf("%s\n", (char *)ptr);
    return NULL;
}

int main() {
    pthread_t id;
    pthread_create(&id, NULL, busy, "Hi");
    void *result;
    pthread_join(id, &result);
    printf("%s\n", *(char **)&result);
    while (1) {} // Loop forever
}
