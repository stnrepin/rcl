#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <pthread.h>

#include "rcl.h"
#include "rcl_errno.h"

#define LOG_ERROR() printf("fatal error: %s\n", rcl_errno_str())

#define NB_REQUESTS 1000000 // 1M
#define NB_LOOPS_PER_CLIENT 16
#define SHARED_INC 21

static volatile uint64_t g_shared; // volatile, so the client loop won't be unfolded.

static atomic_int g_started_cnt_sz;
static struct rcl_lock g_lck;

static void do_work(int nb_cnts);
static void *process_client();
static void process_client_critical(void *arg);
static int parse_cpu_config(struct rcl_cpu_config *cpu_cfg, int argc, char** argv);

static inline uint64_t calc_expected_shared(int nb_cnts) {
    return SHARED_INC * nb_cnts * NB_REQUESTS * NB_LOOPS_PER_CLIENT;
}

int main(int argc, char** argv) {
    int rc, nb_cnts;
    struct rcl_cpu_config cpu_cfg;

    if (argc < 2) {
        rcl_errno_set(RCL_EARGS);
        LOG_ERROR();
        return 1;
    }

    nb_cnts = atoi(argv[1]);
    argv += 2;
    argc -= 2;

    rc = parse_cpu_config(&cpu_cfg, argc, argv);
    if (rc != 0) {
        LOG_ERROR();
        return 1;
    }

    puts("Initializing RCL Runtime...");

    rc = rcl_init(&cpu_cfg);
    if (rc != 0) {
        return 2;
    }

    g_shared = 0;
    atomic_init(&g_started_cnt_sz, nb_cnts);

    puts("Initializing test lock...");
    rcl_lock_init(&g_lck);

    puts("Running client threads...");

    do_work(nb_cnts);

    return 0;
}

static void do_work(int nb_cnts) {
    int i;
    pthread_t *cnt_ids;

    cnt_ids = malloc(sizeof(pthread_t) * nb_cnts);

    for (i = 0; i < nb_cnts; i++) {
        cnt_ids[i] = rcl_client_run(i, process_client);
    }

    for (i = 0; i < nb_cnts; i++) {
        //pthread_join(cnt_ids[i], NULL);
    }

    printf("Actual `shared`   : %ld\n", g_shared);
    printf("Expected `shared` : %ld\n", calc_expected_shared(nb_cnts));
}

static void *process_client() {
    int i;

    atomic_fetch_sub(&g_started_cnt_sz, 1);
    while (atomic_load(&g_started_cnt_sz) > 0) {
        // pause
    }

    for (i = 0; i < NB_REQUESTS; i++) {
        rcl_request(&g_lck, process_client_critical, NULL);
    }

    return NULL;
}

static void process_client_critical(void *arg) {
    RCL_USED(arg);
    int i;
    for (i = 0; i < NB_LOOPS_PER_CLIENT; i++) {
        g_shared += SHARED_INC;
    }
}

static int parse_cpu_config(struct rcl_cpu_config *cpu_cfg, int argc, char** argv) {
    size_t i;

    if (argc < 2) {
        rcl_errno_set(RCL_EARGS);
        return 1;
    }

    cpu_cfg->srv_cpu = atoi(argv[0]);

    cpu_cfg->cnt_cpus_sz = argc - 1;
    cpu_cfg->cnt_cpus = malloc(sizeof(rcl_cpu_t) * cpu_cfg->cnt_cpus_sz);

    argv++;
    for (i = 0; i < cpu_cfg->cnt_cpus_sz; i++) {
        cpu_cfg->cnt_cpus[i] = atoi(argv[i]);
    }

    return 0;
}
