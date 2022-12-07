/*
 * Copyright (C) 2016 Matt Fleming
 */
#include "adrestia.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
	const char usage_str[] =
"Usage: [OPTION] [TESTNAME]\n"
"\n"
"\t-a\tarrival time (microseconds)\n"
"\t-l\tnumber of loops\n"
"\t-L\tlist tests\n"
"\t-s\tservice time (microseconds)\n"
"\t-t\tthread count\n"
#ifdef MY_SCHED_POLICY
"\t-R\tRT policy(0:non-RT, 1: FIFO, 2: RR)\n"
#endif
;

	fprintf(stderr, usage_str);
}

struct test {
	const char *name;
	int (*func)(void);
};

static struct test tests[] = {
	{ "wakeup-single", test_wakeup_single },
	{ "wakeup-periodic", test_wakeup_periodic },
};

#define for_each_test(i, t)		\
	for (i = 0, t = &tests[0]; i < ARRAY_SIZE(tests); i++, t++)

static void list_tests(void)
{
	struct test *t;
	int i;

	for_each_test(i, t)
		printf("%s\n", t->name);
}

long num_cpus;
long num_threads;
#ifdef MY_SCHED_POLICY
static int policy;
int num_of_9[NUM_OF_9] = {};
#endif

/* Arrival rate in microseconds */
#define ARRIVAL_RATE_US	10
int arrival_rate = ARRIVAL_RATE_US;

/* Task service time in microseconds */
#define SERVICE_TIME_US 50
int service_time = SERVICE_TIME_US;

#define NR_LOOPS	10000
int num_loops = NR_LOOPS;

struct thread *threads;

#ifdef MY_SCHED_POLICY
static int set_rt_thread_priority(pthread_attr_t *attr)
{
	struct sched_param param;

	if (policy != SCHED_OTHER) {
		param.sched_priority = 80;
		if (pthread_attr_setschedparam(attr, &param)) {
			printf("pthread setschedparam failed\n");
			return -1;
		}
	}

	/* Use scheduling parameters of attr */
	if (pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED)) {
		printf("pthread setinheritsched failed\n");
		return -1;
	}

	return 0;
}

static int set_rt_thread_attr(pthread_attr_t *attr)
{
	int ret;

	ret = pthread_attr_init(attr);
	if (ret) {
		printf("ERROR: pthread_attr_init failed, status=%d\n",	ret);
		return -1;
	}

	ret = pthread_attr_setschedpolicy(attr, policy);
	if (ret) {
		printf("ERROR: pthread_attr_setschedpolicy failed, status=%d\n",  ret);
		return -1;
	}

	if (set_rt_thread_priority(attr)) {
		printf("ERROR: set_rt_thread_priority failed\n");
		return -1;
	}

	return 0;
}
#endif /* MY_SCHED_POLICY */

void thread_startup(unsigned int nr_threads, void *arg,
		    void (*ctor)(struct thread *t, void *arg),
		    void *(*thread_func)(void *arg))
{
	int i;
#ifdef MY_SCHED_POLICY
	pthread_attr_t attr;
#endif

	threads = calloc(nr_threads, sizeof(*threads));
	if (!threads) {
		perror("calloc");
		abort();
	}

	for (i = 0; i < nr_threads; i++) {
		struct thread_data *td;
		struct thread *t;

		t = &threads[i];

		td = calloc(1, sizeof(*td));
		if (!td) {
			perror("calloc");
			abort();
		}

		td->cpu = i;
		t->td = td;

		ctor(t, arg);

#ifdef MY_SCHED_POLICY
		if (set_rt_thread_attr(&attr)) {
			perror("set_rt_thread_attr failed!\n");
			abort();
		}
		if (pthread_create(&t->tid, &attr, thread_func, t->td)) {
#else
		if (pthread_create(&t->tid, NULL, thread_func, t->td)) {
#endif
			perror("pthread_create");
			abort();
		}
	}
}

void thread_teardown(unsigned int nr_threads,
		    void (*dtor)(struct thread *t))
{
	int dead_threads = 0;
	int i;

	while (dead_threads < nr_threads) {
		for (i = 0; i < nr_threads; i++) {
			struct thread *t;

			t = &threads[i];

			if (t->dead)
				continue;

			if (pthread_tryjoin_np(t->tid, NULL))
				continue;

			t->dead = 1;
			dead_threads++;

			dtor(t);
		}
	}

	free(threads);
}

#ifdef MY_SCHED_POLICY
static void test_cfg_dump(void)
{
	printf("test configuration:\n");
	printf("\twakeup interval: %d us\n", arrival_rate);
	printf("\tnum_loops: %d\n", num_loops);
	printf("\tnum_threads: %ld\n", num_threads);
	printf("\tRT policy: %s\n",
		(policy == SCHED_FIFO) ? "FIFO" : ((policy == SCHED_RR) ? "RR" : "other"));
}
#endif

int main(int argc, char **argv)
{
	char *testname = NULL;
	struct test *t;
	bool found = false;
	int opt;
	int i;

	num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	num_threads = 1;

	while ((opt = getopt(argc, argv, "a:l:Ls:t:R:")) != -1) {
		switch (opt) {
		case 'a':
			arrival_rate = atoi(optarg);
			break;
		case 'l':
			num_loops = atoi(optarg);
			if (num_loops < 10000 || num_loops % 10000) {
				printf("loop must be multiple of 10000\n");
				return -1;
			}
			break;
		case 'L':
			list_tests();
			exit(0);
			break;
		case 's':
			service_time = atoi(optarg);
			break;
		case 't':
			num_threads = atoi(optarg);
			break;
#ifdef MY_SCHED_POLICY
		case 'R':
            policy = atoi(optarg);
            break;
#endif
		default: /* ? */
			usage();
			exit(1);
		}
	}

	testname = argv[optind];

#ifdef MY_SCHED_POLICY
	test_cfg_dump();
#endif

	for_each_test(i, t) {
		if (testname && strcmp(t->name, testname))
			continue;

		found = true;
		t->func();
	}

	if (!found) {
		fprintf(stderr, "Invalid testname\n");
		exit(1);
	}

	return 0;
}
