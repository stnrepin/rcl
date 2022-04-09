#define _GNU_SOURCE

#include "rcl.h"

#include <stdint.h>

#include <sched.h>

#define RCL_MAX_CLIENT_COUNT 64

#ifndef RCL_DEBUG_USE_PTHREAD_LOCK

struct rcl_request {
    struct rcl_lock *lock;
    void *arg;
    rcl_callback_t *volatile cb;
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

enum server_state {
    SERVER_STATE_UP,
    SERVER_STATE_DOWN,
    SERVER_STATE_STARTING,
};

struct rcl_server {
    enum server_state state;
    uint64_t timestamp;
    struct rcl_request *reqs;
};

struct rcl_client_global_ctx {
    int index;
};

struct rcl_server_global_ctx {
    int nb_clients;
    int next_client_index;
};

struct rcl_global_ctx {
    int is_srv;
    struct rcl_thread *thread; // unused
    struct rcl_server *srv;
    union {
        struct rcl_server_global_ctx srv_ctx;
        struct rcl_client_global_ctx cnt_ctx;
    };
};

struct rcl_config {
    struct rcl_cpu_config cpu_cfg;
    int next_cnt_cpu;
};

static struct rcl_config g_rcl_cfg;
static __thread struct rcl_global_ctx g_thread_ctx;

static void init_server();
static void server_thread();
static void run_manager_on(rcl_cpu_t cpu);
static void *manager_thread(void *arg);
static void backup_thread();

int rcl_init(struct rcl_cpu_config *cpu_cfg) {
    rcl_cpu_t srv_cpu;
    g_rcl_cfg.cpu_cfg = *cpu_cfg;
    g_rcl_cfg.next_cnt_cpu = 0;

    // Use only one server thread and multiple clients.
    // Run them on different cpus.

    srv_cpu = cpu_cfg->srv_cpu;
    init_server();
    run_manager_on(srv_cpu);

    return 0;
}

void init_server() {
    struct rcl_server *srv;

    srv = malloc(sizeof(struct rcl_server));

    srv->state = SERVER_STATE_STARTING;
    srv->reqs = malloc(sizeof(struct rcl_request) * RCL_MAX_CLIENT_COUNT);
    srv->timestamp = 1;

    g_thread_ctx.is_srv = 1;
    g_thread_ctx.srv = srv;
}

static void run_manager_on(rcl_cpu_t cpu) {
    struct rcl_server *srv;
    cpu_set_t cpuset;
    pthread_attr_t attr;
    pthread_t id;

    srv = g_thread_ctx.srv;

    CPU_ZERO(&cpuset);
    CPU_SET(g_rcl_cfg.cpu_cfg.srv_cpu, &cpuset);
    pthread_attr_init(&attr);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
    pthread_create(&id, &attr, manager_thread, srv);

    //while (srv->state == SERVER_STATE_STARTING) {
        //pthread_cond_wait(&srv->state_cond, &srv->state_lock);
    //}
}

void rcl_lock_init(struct rcl_lock *lck) {
    lck->srv = g_thread_ctx.srv;
    lck->locked = 0;
    lck->req = NULL;
}

struct client_runner_arg {
    void *arg;
    rcl_client_t *f;
    int index;
    struct rcl_server *srv;
};

void *client_runner(void *a) {
    struct client_runner_arg *arg = a;

    g_thread_ctx.is_srv = 0;
    g_thread_ctx.srv = arg->srv;
    g_thread_ctx.thread = NULL;
    g_thread_ctx.cnt_ctx.index = arg->index;

    g_thread_ctx.srv->reqs[g_thread_ctx.cnt_ctx.index].cb = NULL;

    arg->f(arg->arg);
    return NULL;
}

int rcl_client_run(rcl_id_t *id, rcl_client_t *f, void *arg) {
    struct client_runner_arg *runner_arg;
    cpu_set_t cpuset;
    pthread_attr_t attr;

    CPU_ZERO(&cpuset);
    CPU_XOR(&cpuset, &cpuset, &cpuset);
    CPU_CLR(g_rcl_cfg.cpu_cfg.srv_cpu, &cpuset);
    pthread_attr_init(&attr);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

    runner_arg = malloc(sizeof(*runner_arg));
    runner_arg->arg = arg;
    runner_arg->f = f;
    runner_arg->index = g_thread_ctx.srv_ctx.next_client_index++;
    runner_arg->srv = g_thread_ctx.srv;

    return pthread_create(id, &attr, client_runner, runner_arg);
}

int rcl_client_join(rcl_id_t id) {
    return pthread_join(id, NULL);
}

void rcl_request(struct rcl_lock *lck, rcl_callback_t* cb, void *arg) {
    struct rcl_server *srv;
    struct rcl_request *r;
    int atomic_exp;

    srv = lck->srv;

    if (g_thread_ctx.is_srv && g_thread_ctx.srv == srv) {
        atomic_exp = 0;
        while (!atomic_compare_exchange_weak_explicit(
                    &lck->locked, &atomic_exp, 1,
                    memory_order_relaxed, memory_order_relaxed)) {
            g_thread_ctx.thread->timestamp = srv->timestamp;
            sched_yield();
        }

        cb(arg);
        atomic_store_explicit(&lck->locked, 0, memory_order_relaxed);
        return;
    }

    r = &srv->reqs[g_thread_ctx.cnt_ctx.index];
    r->lock = lck;
    r->arg = arg;
    r->cb = cb;

    while (r->cb != NULL) {
        rcl_pause();
    }
}

static void *manager_thread(void *arg) {
    return NULL;
}

#else // !RCL_DEBUG_USE_PTHREAD_LOCK

int rcl_init(struct rcl_cpu_config *cpu_cfg) {
    RCL_USED(cpu_cfg);
    return 0;
}

int rcl_client_run(rcl_id_t *id, rcl_client_t *f, void *arg) {
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

