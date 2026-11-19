#if defined(__linux__)
#include "core/RtGuard.h"

#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <cstddef>

extern "C" {
void* __libc_malloc(size_t);
void __libc_free(void*);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
}

namespace {
thread_local int hookDepth = 0;

void note(const char* why) {
    if (hookDepth || !ghovlz::rtGuardActive())
        return;
    ++hookDepth;
    ghovlz::rtGuardHit(why);
    --hookDepth;
}
} // namespace

extern "C" void* malloc(size_t n) {
    note("malloc");
    return __libc_malloc(n ? n : 1);
}
extern "C" void free(void* p) {
    note("free");
    __libc_free(p);
}
extern "C" void* calloc(size_t n, size_t size) {
    note("calloc");
    return __libc_calloc(n, size);
}
extern "C" void* realloc(void* p, size_t n) {
    note("realloc");
    return __libc_realloc(p, n);
}
extern "C" int nanosleep(const struct timespec* req, struct timespec* rem) {
    note("nanosleep");
    using Fn = int (*)(const struct timespec*, struct timespec*);
    static Fn real = nullptr;
    if (!real) {
        ++hookDepth;
        real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "nanosleep"));
        --hookDepth;
    }
    return real(req, rem);
}
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg) {
    note("pthread_create");
    using Fn = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    static Fn real = nullptr;
    if (!real) {
        ++hookDepth;
        real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "pthread_create"));
        --hookDepth;
    }
    return real(thread, attr, start, arg);
}
#endif
