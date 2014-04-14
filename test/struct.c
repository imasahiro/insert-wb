#include <stdio.h>
#include <stdint.h>

typedef uintptr_t VALUE;
struct RBasic {
    VALUE flag;
    const VALUE klass;
};

struct y {
    struct RBasic base;
    VALUE v;
};

struct a {
    struct RBasic base;
    VALUE *v;
    int len;
};

void __write_barrier() {}

int n = 0;
void *b = NULL;

void g(struct a *a) {
    while(n < a->len) {
        int v = n + 1;
        a->v[n++] = (VALUE) b; // 1
        n += 10;
    }
}
