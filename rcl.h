#ifndef RCL_H
#define RCL_H

#include <stdlib.h>

#define RCL_USED(x) (void)(x)

typedef int rcl_cpu_t;
typedef void *(rcl_client_t)();
typedef void (rcl_callback_t)(void *ctx);

struct rcl_cpu_config {
    rcl_cpu_t srv_cpu;
    rcl_cpu_t *cnt_cpus;
    size_t cnt_cpus_sz;
};

struct rcl_server;

struct rcl_lock {
    struct rcl_server *srv;
    int locked;
};

int rcl_init(struct rcl_cpu_config *cpu_cfg);
int rcl_client_run(int id, rcl_client_t *f);

void rcl_lock_init(struct rcl_lock *lck);
int rcl_request(struct rcl_lock *lck, rcl_callback_t* cb, void *arg);

#endif // !RCL_H
