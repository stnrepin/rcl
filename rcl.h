#ifndef RCL_H
#define RCL_H

#include <stdatomic.h>
#include <stdlib.h>
#include <immintrin.h>

#include <pthread.h>

#define RCL_USED(x) (void)(x)

typedef int rcl_cpu_t;
typedef pthread_t rcl_id_t;
typedef void *(rcl_client_t)(void *);
typedef void (rcl_callback_t)(void *);

struct rcl_cpu_config {
    rcl_cpu_t srv_cpu;
};

struct rcl_server;
struct rcl_request;

struct rcl_lock {
#ifndef RCL_DEBUG_USE_PTHREAD_LOCK
    struct rcl_server *srv;
    struct rcl_request *volatile req;
    atomic_int locked;
#else
    pthread_mutex_t inner;
#endif
};


int rcl_init(struct rcl_cpu_config *cpu_cfg);
int rcl_client_run(rcl_id_t *id, rcl_client_t *f, void *arg);
int rcl_client_join(rcl_id_t id);

void rcl_lock_init(struct rcl_lock *lck);
void rcl_request(struct rcl_lock *lck, rcl_callback_t* cb, void *arg);

static inline void rcl_pause() {
    _mm_pause();
}

#endif // !RCL_H
