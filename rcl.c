#define _GNU_SOURCE

#include "rcl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <linux/futex.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#define RCL_MAX_CLIENT_COUNT 64

#ifndef RCL_DEBUG_USE_PTHREAD_LOCK

struct rcl_request {
    struct rcl_lock *lock;
    void *arg;
    rcl_callback_t *volatile cb;
};

struct rcl_thread {
    pthread_t tid;
    struct rcl_server *srv;
    uint64_t timestamp;
    int servicing;
    struct rcl_thread *next;
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
    SERVER_STATE_DOWN = 0x00,
    SERVER_STATE_STARTING = 0x01,
    SERVER_STATE_UP = 0x02,
};

struct rcl_server {
    volatile enum server_state state;
    volatile int alive;
    volatile uint64_t timestamp;
    atomic_int ready_and_servicing_count;
    struct rcl_request *reqs;

    struct rcl_thread *all_threads;
    struct rcl_thread *prepared_threads;
    atomic_int free_servicing_count;

    int wakeup;
};

struct rcl_client_global_ctx {
    int index;
};

struct rcl_server_global_ctx {
    volatile int next_client_index;
    volatile struct rcl_thread *thread;
};

struct rcl_global_ctx {
    int is_srv;
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

enum RT_THREAD_PRIO {
    RT_THREAD_PRIO_BACKUP = 2,
    RT_THREAD_PRIO_SERVICING = 3,
    RT_THREAD_PRIO_MANAGER = 4,
};

static struct rcl_config g_rcl_cfg;
static __thread struct rcl_global_ctx g_thread_ctx;

static void init_server();
static void *servicing_thread(void *arg);
static void run_manager_on(rcl_cpu_t cpu);
static void *manager_thread(void *arg);
static void *backup_thread(void *arg);

static inline long sys_futex(void *addr, int op, int val, struct timespec *to) {
    return syscall(SYS_futex, addr, op, val, to, NULL, NULL);
}

static inline void set_priority(pthread_t id, unsigned int prio) {
    int rc = pthread_setschedprio(id, prio);
    if (rc != 0) {
        printf("fatal error: unable to set prio %d for %ld (rc=%d)\n", prio, id, rc);
    }
}

static void create_thread_on(rcl_cpu_t cpu, pthread_t *id, pthread_attr_t *attr, void *(*f)(void *), void *arg) {
    int rc;
    pthread_t def_id;
    cpu_set_t cpuset;
    pthread_attr_t def_attr;

    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (id == NULL) {
        id = &def_id;
    }
    if (attr == NULL) {
        attr = &def_attr;
        pthread_attr_init(attr);
    }
    pthread_attr_setaffinity_np(attr, sizeof(cpu_set_t), &cpuset);
    rc = pthread_create(id, attr, f, arg);
    printf("info: create thread %ld on %d\n", *id, cpu);
    if (rc != 0) {
        printf("fatal: cant create thread on %d (rc=%d)\n", cpu, rc);
    }
}

int rcl_init(struct rcl_cpu_config *cpu_cfg) {
    rcl_cpu_t srv_cpu;
    g_rcl_cfg.cpu_cfg = *cpu_cfg;
    g_rcl_cfg.next_cnt_cpu = 0;

    srv_cpu = cpu_cfg->srv_cpu;
    init_server();
    run_manager_on(srv_cpu);

    return 0;
}

void init_server() {
    struct rcl_server *srv;

    srv = malloc(sizeof(*srv));
    memset(srv, 0, sizeof(*srv));

    srv->state = SERVER_STATE_STARTING;
    srv->alive = 0;
    srv->timestamp = 1;
    atomic_init(&srv->ready_and_servicing_count, 0);
    srv->reqs = malloc(sizeof(struct rcl_request) * RCL_MAX_CLIENT_COUNT);
    memset(srv->reqs, 0, sizeof(struct rcl_request) * RCL_MAX_CLIENT_COUNT);
    atomic_init(&srv->free_servicing_count, 0);
    srv->all_threads = NULL;
    srv->prepared_threads = NULL;
    srv->wakeup = 0;

    g_thread_ctx.is_srv = 1;
    g_thread_ctx.srv = srv;

    g_thread_ctx.srv_ctx.next_client_index = 0;
}

static void run_manager_on(rcl_cpu_t cpu) {
    struct rcl_server *srv;

    srv = g_thread_ctx.srv;

    create_thread_on(cpu, NULL, NULL, manager_thread, srv);

    while (srv->state == SERVER_STATE_STARTING) {
        sched_yield();
    }
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
    g_thread_ctx.cnt_ctx.index = arg->index;

    g_thread_ctx.srv->reqs[g_thread_ctx.cnt_ctx.index].cb = NULL;

    arg->f(arg->arg);
    return NULL;
}

int rcl_client_run(rcl_id_t *id, rcl_client_t *f, void *arg) {
    int i;
    struct client_runner_arg *runner_arg;
    cpu_set_t cpuset;
    pthread_attr_t attr;

    CPU_ZERO(&cpuset);
    for (i = 0; i < CPU_SETSIZE; i++) {
        CPU_SET(i, &cpuset);
    }
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
            g_thread_ctx.srv_ctx.thread->timestamp = srv->timestamp;
            sched_yield();
        }

        cb(arg);
        atomic_store_explicit(&lck->locked, 0, memory_order_relaxed);
        printf("info: is_srv\n");
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

static void *servicing_thread(void *arg) {
    struct rcl_thread *th;
    struct rcl_server *srv;
    struct rcl_request *req;
    struct rcl_lock *lck;
    int i, tmp;

    th = arg;
    srv = th->srv;

    atomic_fetch_sub_explicit(&srv->free_servicing_count, 1, memory_order_relaxed);

    while (srv->state >= SERVER_STATE_STARTING) {
        srv->alive = 1;
        th->timestamp = srv->timestamp;

        for (i = 0; i < RCL_MAX_CLIENT_COUNT; i++) {
            req = &srv->reqs[i];
            if (req->cb) {
                lck = req->lock;
                tmp = 0;
                if (!atomic_compare_exchange_strong_explicit(
                            &lck->locked, &tmp, 1,
                            memory_order_relaxed, memory_order_relaxed)) {
                    req->cb(req->arg);
                    req->cb = NULL;
                    atomic_store_explicit(&lck->locked, 0, memory_order_relaxed);
                }
            }
        }

        atomic_fetch_add(&srv->free_servicing_count, 1);
        if (atomic_load_explicit(&srv->ready_and_servicing_count, memory_order_relaxed) > 1) {
            printf("error: not implemented slow path\n");
        }
    }

    printf("info: servicing exit\n");

    return NULL;
}

static void ensure_has_free_servicing_thread(struct rcl_server *srv) {
    struct rcl_thread *elected;
    int has_prepared;
    pthread_attr_t attr;
    struct sched_param param;

    if (srv->free_servicing_count > 0) {
        return;
    }

    if (srv->state == SERVER_STATE_DOWN) {
        return;
    }

    elected = srv->prepared_threads;
    has_prepared = (elected != NULL);

    if (!has_prepared) {
        elected = malloc(sizeof(*elected));
        elected->srv = srv;
        elected->next = srv->all_threads;
        srv->all_threads = elected;
    }

    elected->timestamp = srv->timestamp - 1;
    atomic_fetch_add_explicit(&srv->free_servicing_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&srv->ready_and_servicing_count, 1, memory_order_seq_cst);
    elected->servicing = 1;

    g_thread_ctx.srv_ctx.thread = elected;

    if (has_prepared) {
        sys_futex(&elected->servicing, FUTEX_WAKE_PRIVATE, 1, NULL);
    }
    else {
        param.sched_priority = RT_THREAD_PRIO_SERVICING;
        pthread_attr_init(&attr);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        pthread_attr_setschedparam(&attr, &param);

        create_thread_on(g_rcl_cfg.cpu_cfg.srv_cpu, &elected->tid, &attr, servicing_thread, elected);
        printf("info: (ensure) run new servicing %ld\n", elected->tid);
    }
}

static void *manager_thread(void *arg) {
    int rc, done, state;
    struct rcl_server *srv;
    pthread_attr_t attr;
    pthread_t backup_id;
    struct sched_param param;
    struct rcl_thread *cur;
    struct timespec manager_to;

    srv = arg;

    srv->alive = 1;

    param.sched_priority = RT_THREAD_PRIO_MANAGER;
    rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (rc != 0) {
        printf("fatal error (manager_thread): cant set RT_THREAD_PRIO_MANAGER\n");
        return NULL;
    }

    param.sched_priority = RT_THREAD_PRIO_BACKUP;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &param);

    create_thread_on(g_rcl_cfg.cpu_cfg.srv_cpu, &backup_id, &attr, backup_thread, srv);

    ensure_has_free_servicing_thread(srv);

    srv->state = SERVER_STATE_UP;

    while (srv->state == SERVER_STATE_UP) {
        srv->wakeup = 0;

        if (!srv->alive) {
            ensure_has_free_servicing_thread(srv);

            srv->alive = 1;
            done = 0;
            while (!done) {
                cur = srv->all_threads;
                while (cur) {
                    if (cur->servicing) {
                        if (done || cur->timestamp == cur->srv->timestamp) {
                            set_priority(cur->tid, RT_THREAD_PRIO_BACKUP);
                            set_priority(cur->tid, RT_THREAD_PRIO_SERVICING);
                        }
                        else {
                            cur->timestamp = srv->timestamp;
                            done = 1;
                        }
                    }
                    cur = cur->next;
                }
                if (!done) {
                    srv->timestamp++;
                }
            }
        }
        else {
            srv->alive = 0;
        }

        manager_to.tv_sec = 0;
        manager_to.tv_nsec= 50000000;
        sys_futex(&srv->wakeup, FUTEX_WAIT_PRIVATE, 0, &manager_to);
    }

    cur = srv->all_threads;
    while (cur) {
        state = atomic_exchange(&cur->servicing, 1);
        if (!state) {
            atomic_fetch_add_explicit(&srv->free_servicing_count, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&srv->ready_and_servicing_count, 1, memory_order_seq_cst);
            sys_futex(&cur->servicing, FUTEX_WAKE_PRIVATE, 1, NULL);
        }
        cur = cur->next;
    }

    pthread_join(backup_id, NULL);

    srv->state = SERVER_STATE_DOWN;

    printf("info: manager exit\n");

    return NULL;
}

static void *backup_thread(void *arg) {
    struct rcl_server *srv = arg;

    while (srv->state >= SERVER_STATE_STARTING) {
        srv->alive = 0;
        srv->wakeup = 1;
        sys_futex(&srv->wakeup, FUTEX_WAKE_PRIVATE, 1, NULL);
    }

    printf("info: backup exit\n");
    return NULL;
}

#else // !RCL_DEBUG_USE_PTHREAD_LOCK

int rcl_init(struct rcl_cpu_config *cpu_cfg) {
    RCL_USED(cpu_cfg);
    return 0;
}

int rcl_client_run(rcl_id_t *id, rcl_client_t *f, void *arg) {
    return pthread_create(id, NULL, f, arg);
}

int rcl_client_join(rcl_id_t id) {
    return pthread_join(id, NULL);
}

void rcl_lock_init(struct rcl_lock *lck) {
    pthread_mutex_init(&lck->inner, NULL);
}

void rcl_request(struct rcl_lock *lck, rcl_callback_t* cb, void *arg) {
    pthread_mutex_lock(&lck->inner);
    cb(arg);
    pthread_mutex_unlock(&lck->inner);
}


#endif // !RCL_DEBUG_USE_PTHREAD_LOCK

