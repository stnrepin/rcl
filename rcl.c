#include "rcl.h"

#include <stdint.h>

#ifndef RCL_DEBUG_USE_PTHREAD_LOCK

struct rcl_request {
    struct rcl_lock *lock;
    void *ctx;
    rcl_callback_t *cb;
    void *volatile owner;
};

struct rcl_thread {
    struct rcl_server *srv;
    uint64_t timestamp;
    int servicing;
};

struct rcl_thread_ll_stack {
    struct llist_head *head;
    struct rcl_thread *thread;
};

struct rcl_thread_list {
    struct list_head *head;
    struct rcl_thread thread;
};

struct rcl_server {

};

//static void rcl_server_thread();
//static void rcl_manager_thread();
//static void rcl_backup_thread();

int rcl_init(struct rcl_cpu_config *cpu_cfg) {
    // Move current to 0.
    // Run server, manager, backup
    return 0;
}

void rcl_lock_init(struct rcl_lock *lck) {
}

rcl_id_t rcl_client_run(rcl_client_t *f, void *arg) {
}

int rcl_client_join(rcl_id_t id) {
}

int rcl_request(struct rcl_lock *lck, rcl_callback_t* cb, void *arg) {
    return 0;
}

#else // !RCL_DEBUG_USE_PTHREAD_LOCK

int rcl_init(struct rcl_cpu_config *cpu_cfg) {
    RCL_USED(cpu_cfg);
    return 0;
}

rcl_id_t rcl_client_run(rcl_client_t *f, void *arg) {
    rcl_id_t id;
    pthread_create(&id, NULL, f, arg);
    return id;
}

int rcl_client_join(rcl_id_t id) {
    return pthread_join(id, NULL);
}

void rcl_lock_init(struct rcl_lock *lck) {
    pthread_mutex_init(&lck->inner, NULL);
}

int rcl_request(struct rcl_lock *lck, rcl_callback_t* cb, void *arg) {
    pthread_mutex_lock(&lck->inner);
    cb(arg);
    pthread_mutex_unlock(&lck->inner);
    return 0;
}


#endif // !RCL_DEBUG_USE_PTHREAD_LOCK

