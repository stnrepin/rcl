#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "rcl.h"

#define NB_REQUESTS 1000000 // 1M
#define NB_LOOPS_PER_CLIENT 16
#define SHARED_INC UINT64_C(21)

static volatile uint64_t g_shared; // volatile, so the client loop won't be unfolded.

static atomic_int g_started_cnt_sz;
static struct rcl_lock g_lck;

static void do_work(int nb_cnts);
static void *process_client(void *arg);
static void process_client_critical(void *arg);
static int parse_cpu_config(struct rcl_cpu_config *cpu_cfg, int argc, char **argv);

static inline uint64_t calc_expected_shared(int nb_cnts) {
    return SHARED_INC * nb_cnts * NB_REQUESTS * NB_LOOPS_PER_CLIENT;
}

struct stopwatch {
    struct timespec start;
    struct timespec end;
};

static void stopwatch_start(struct stopwatch *sw) {
    clock_gettime(CLOCK_REALTIME, &sw->start);
}

static void stopwatch_stop(struct stopwatch *sw) {
    clock_gettime(CLOCK_REALTIME, &sw->end);
}

static uint64_t stopwatch_get_elapsed_ns(struct stopwatch *sw) {
    const uint64_t billion = 1000000000;
    return (sw->end.tv_sec - sw->start.tv_sec) * billion + (sw->end.tv_nsec - sw->start.tv_nsec);
}

int main(int argc, char **argv) {
    int rc, nb_cnts;
    struct rcl_cpu_config cpu_cfg;

    if (argc < 3) {
        printf("invalid args: no number of client\n");
        return 1;
    }

    nb_cnts = atoi(argv[1]);
    argv += 2;
    argc -= 2;

    rc = parse_cpu_config(&cpu_cfg, argc, argv);
    if (rc != 0) {
        return 1;
    }

    puts("Initializing RCL Runtime...");

    rc = rcl_init(&cpu_cfg);
    if (rc != 0) {
        return 2;
    }

    printf("nb_client = %d\n", nb_cnts);
    printf("server_cpu = %d\n", cpu_cfg.srv_cpu);

    g_shared = 0;
    atomic_init(&g_started_cnt_sz, nb_cnts);

    puts("Initializing test lock...");
    rcl_lock_init(&g_lck);

    puts("Running client threads...");

    do_work(nb_cnts);

    return 0;
}

static void do_work(int nb_cnts) {
    int i, rc;
    rcl_id_t *cnt_ids;
    struct stopwatch sw;
    uint64_t expected;

    cnt_ids = malloc(sizeof(pthread_t) * nb_cnts);

    stopwatch_start(&sw);
    for (i = 0; i < nb_cnts; i++) {
        rc = rcl_client_run(&cnt_ids[i], process_client, NULL);
        assert(rc == 0);
    }

    for (i = 0; i < nb_cnts; i++) {
        rc = rcl_client_join(cnt_ids[i]);
        assert(rc == 0);
    }
    stopwatch_stop(&sw);

    expected = calc_expected_shared(nb_cnts);
    printf("Actual `shared`   : %ld\n", g_shared);
    printf("Expected `shared` : %ld\n", expected);
    printf("Actual = expected : %d\n", g_shared == expected);
    printf("Elapsed (ns)      : %ld\n", stopwatch_get_elapsed_ns(&sw));
}

static void *process_client(void *arg) {
    RCL_USED(arg);
    int i;

    atomic_fetch_sub(&g_started_cnt_sz, 1);
    while (atomic_load(&g_started_cnt_sz) > 0) {
        rcl_pause();
    }

    printf("info: run client 2\n");
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

static int parse_cpu_config(struct rcl_cpu_config *cpu_cfg, int argc, char **argv) {
    if (argc < 1) {
        printf("invalid args: no server cpu\n");
        return 1;
    }

    cpu_cfg->srv_cpu = atoi(argv[0]);

    return 0;
}
