#include "kernel/xk_polygon_edge.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int busy;
void xv_object_math_report_check(void) { if (busy) abort(); }

static void *worker(void *unused)
{
    (void)unused;
    for (unsigned i=0; i<50000; ++i) {
        assert(xv_polygon_edge_begin());
        xv_polygon_edge_end();
    }
    return NULL;
}

static void *wrong_owner(void *which)
{
    switch ((unsigned long)which) {
    case 0: xv_native_polygon_edge_override(0); break;
    case 1: xv_native_polygon_edge_init(); break;
    default: (void)xv_math_polygon_edge_calls(); break;
    }
    return NULL;
}

static void rejects(unsigned test)
{
    pid_t pid=fork(); assert(pid>=0);
    if (!pid) {
        if (test<3) {
            pthread_t thread;
            assert(!pthread_create(&thread,NULL,wrong_owner,(void*)(unsigned long)test));
            assert(!pthread_join(thread,NULL));
        } else if (test==3 || test==4) {
            assert(xv_polygon_edge_begin());
            if (test==3) xv_native_polygon_edge_override(0);
            else (void)xv_math_polygon_edge_calls();
        } else if (test==5) {
            busy=1; xv_native_polygon_edge_override(0);
        } else {
            xv_polygon_edge_end();
        }
        _exit(0);
    }
    int status=0; assert(waitpid(pid,&status,0)==pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
}

int main(void)
{
    struct rlimit no_core={0,0}; assert(!setrlimit(RLIMIT_CORE,&no_core));
    assert(!xv_native_polygon_edge_available());
    assert(!xv_polygon_edge_begin());
    xv_native_polygon_edge_init();
    assert(xv_native_polygon_edge_available());
    assert(!xv_polygon_edge_begin());
    xv_native_polygon_edge_override(1);
    for (unsigned test=0; test<7; ++test) rejects(test);
    pthread_t threads[4];
    for (unsigned i=0; i<4; ++i) assert(!pthread_create(&threads[i],NULL,worker,NULL));
    for (unsigned i=0; i<4; ++i) assert(!pthread_join(threads[i],NULL));
    assert(xv_math_polygon_edge_calls()==200000);
    assert(xv_math_polygon_edge_calls()==0);
    xv_native_polygon_edge_override(0);
    assert(!xv_polygon_edge_begin());
    assert(xv_math_polygon_edge_calls()==0);
    xv_native_polygon_edge_override(1);
    assert(xv_polygon_edge_begin());
    xv_polygon_edge_end();
    xv_native_polygon_edge_override(-1);
    assert(!xv_polygon_edge_begin());
    assert(xv_math_polygon_edge_calls()==1);
    return 0;
}
