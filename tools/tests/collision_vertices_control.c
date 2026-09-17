#include "kernel/xk_collision_vertices.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned pending;
void xv_object_math_report_check(void){if(pending)abort();}
static void dies(void (*action)(void))
{
    pid_t p=fork();assert(p>=0);
    if(!p){action();_exit(0);}
    int status;assert(waitpid(p,&status,0)==p);
    assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
static void override(void){xv_collision_vertices_override(1);}
static void take(void){(void)xv_collision_vertices_calls();}
static void busy(void){pending=1;override();}
static void *foreign_override(void *p){(void)p;assert(!xv_collision_vertices_control_ready());override();return 0;}
static void foreign(void){pthread_t t;assert(!pthread_create(&t,0,foreign_override,0));pthread_join(t,0);}
static void *helper(void *p)
{
    (void)p;
    for(unsigned i=0;i<10000;i++){
        unsigned scope __attribute__((cleanup(xv_collision_vertices_end)))=xv_collision_vertices_begin();
        assert(scope);
    }
    return 0;
}
static void cleanup_return(void)
{unsigned scope __attribute__((cleanup(xv_collision_vertices_end)))=xv_collision_vertices_begin();assert(scope);return;}
int main(void)
{
    struct rlimit r={0,0};setrlimit(RLIMIT_CORE,&r);
    assert(!xv_collision_vertices_available()&&!xv_collision_vertices_enabled());
    assert(!xv_collision_vertices_control_ready());
    assert(!xv_collision_vertices_begin());dies(override);
    xv_collision_vertices_init();assert(xv_collision_vertices_available());
    assert(xv_collision_vertices_control_ready());
    assert(!xv_collision_vertices_enabled());xv_collision_vertices_init();
    dies(busy);dies(foreign);
    xv_collision_vertices_override(1);
    unsigned token=xv_collision_vertices_begin();assert(token&&xv_collision_vertices_enabled());
    /* Model Present on the same native owner while another guest fiber retains
     * a native scope: the controller must defer instead of calling override. */
    assert(!xv_collision_vertices_control_ready());
    assert(xv_collision_vertices_enabled());
    dies(override);dies(take);xv_collision_vertices_end(&token);assert(!token);
    assert(xv_collision_vertices_control_ready());
    cleanup_return();assert(xv_collision_vertices_calls()==2);
    pthread_t a,b;assert(!pthread_create(&a,0,helper,0));assert(!pthread_create(&b,0,helper,0));
    pthread_join(a,0);pthread_join(b,0);assert(xv_collision_vertices_calls()==20000);
    xv_collision_vertices_override(-1);assert(!xv_collision_vertices_enabled());
    assert(!xv_collision_vertices_begin()&&!xv_collision_vertices_calls());
    puts("PASS default OFF, drained/bound owner, nonfatal suspended-scope deferral, held-scope rejection, cleanup, two native callers, exact take/reset and negative restore");
}
