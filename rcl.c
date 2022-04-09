#include "rcl.h"

#include <stdint.h>

struct rcl_request {
    rcl_callback_t *cb;
    void *ctx;
    struct rcl_lock *lock;
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

int rcl_client_run(int id, rcl_client_t *f) {
    f();
    return 0;
}

int rcl_request(struct rcl_lock *lck, rcl_callback_t* cb, void *arg) {
    cb(arg);
    return 0;
}
