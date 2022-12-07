#include <stdbool.h>
#include "adrestia.h"
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>

struct wake_data {
	struct timeval tv;
	int start_fds[2];
	int result_fds[2];
};

extern int num_of_9[NUM_OF_9];

static void *wake_func(void *arg)
{
	struct thread_data *td = arg;
	struct wake_data *wd = td->arg;
	struct timeval now, *tv, res;
	char data;

	while (1) {
		if (read(wd->start_fds[0], &data, 1) != 1) {
			fprintf(stderr, "error waiting for wakeup\n");
			abort();
		}

		gettimeofday(&now, NULL);
		tv = &wd->tv;

		timersub(&now, tv, &res);

		*tv = res;

		if (write(wd->result_fds[1], &data, 1) != 1) {
			fprintf(stderr, "error waking master\n");
			abort();
		}

		pthread_testcancel();
	}

	return NULL;
}

static unsigned long wake_tasks(unsigned int nr_threads)
{
	struct timeval tmp, res;
	struct thread *t;
	int i;

	for (i = 0; i < nr_threads;i++) {
		struct wake_data *wd;
		struct timeval *tv;
		char data;

		t = &threads[i];
		wd = t->td->arg;
		tv = &wd->tv;

		gettimeofday(tv, NULL);
		write(wd->start_fds[1], &data, 1);
	}

	res.tv_sec = 0;
	res.tv_usec = 0;

	for (i = 0; i < nr_threads; i++) {
		struct wake_data *wd;
		char data;

		t = &threads[i];
		wd = t->td->arg;

		if (read(wd->result_fds[0], &data, 1) != 1) {
			perror("read");
			abort();
		}

		tmp = res;
		timeradd(&tmp, &wd->tv, &res);
	}

	return (res.tv_sec * 1000000 + res.tv_usec) / nr_threads;
}

static void wake_ctor(struct thread *t, void *arg)
{
	struct wake_data *wd;

	wd = calloc(1, sizeof(*wd));
	if (!wd) {
		perror("calloc");
		abort();
	}

	t->td->arg = wd;

	if (pipe(wd->start_fds) || pipe(wd->result_fds)) {
		perror("pipe");
		abort();
	}
}

static void wake_dtor(struct thread *t)
{
	struct wake_data *wd = t->td->arg;
	int i;

	for (i = 0; i < 2; i++) {
		close(wd->start_fds[i]);
		close(wd->result_fds[i]);
	}

	free(wd);
}

static void
measure_wakeup(unsigned int nr_threads, unsigned int nr_loops,
	       unsigned long (*func)(unsigned int), unsigned long *cost)
{
	unsigned long *wake_times;
	int i;

	wake_times = calloc(nr_loops, sizeof(*wake_times));
	if (!wake_times) {
		perror("calloc");
		abort();
	}

	thread_startup(nr_threads, NULL, wake_ctor, wake_func);

	for (i = 0; i < nr_loops; i++)
		wake_times[i] = func(nr_threads);

	for (i = 0; i < nr_threads; i++) {
		struct thread *t = &threads[i];
		pthread_cancel(t->tid);
	}

	thread_teardown(nr_threads, wake_dtor);

	return stats_best(wake_times, nr_loops, cost);
}

static void
measure_wakeup_multi(unsigned int nr_threads, unsigned int nr_loops,
	       void (*func)(unsigned int nr_threads, int loop, unsigned long *wake_times), unsigned long *cost)
{
	unsigned long *wake_times;
	int i;

	wake_times = calloc(nr_loops * nr_threads, sizeof(*wake_times));
	if (!wake_times) {
		perror("calloc");
		abort();
	}

	thread_startup(nr_threads, NULL, wake_ctor, wake_func);

	for (i = 0; i < nr_loops; i++)
		func(nr_threads, i, wake_times);

	for (i = 0; i < nr_threads; i++) {
		struct thread *t = &threads[i];
		pthread_cancel(t->tid);
	}

	thread_teardown(nr_threads, wake_dtor);

	return stats_best(wake_times, nr_loops * nr_threads, cost);
}

static void wake_task_multi(unsigned int nr_threads, int loop, unsigned long *wake_times)
{
	struct thread *t;
	int i;

	for (i = 0; i < nr_threads;i++) {
		struct wake_data *wd;
		struct timeval *tv;
		char data;

		t = &threads[i];
		wd = t->td->arg;
		tv = &wd->tv;

		gettimeofday(tv, NULL);
		write(wd->start_fds[1], &data, 1);
	}

	for (i = 0; i < nr_threads; i++) {
		struct wake_data *wd;
		char data;

		t = &threads[i];
		wd = t->td->arg;

		if (read(wd->result_fds[0], &data, 1) != 1) {
			perror("read");
			abort();
		}

		wake_times[(loop * nr_threads) + i] = (wd->tv.tv_sec * 1000000 + wd->tv.tv_usec);
	}

}

static void wake_tasks_periodic(unsigned int nr_threads, int loop, unsigned long *wake_times)
{
	usleep(arrival_rate);

	return wake_task_multi(nr_threads, loop, wake_times);
}

/*
 * Measure the cost of waking up a single thread.
 */
int test_wakeup_single(void)
{
	unsigned long cost[NUM_OF_9];

	measure_wakeup(1, num_loops*num_threads, wake_tasks, cost);
	printf("wakeup cost first   (single): %lu \t\tus\n", cost[0]);
	printf("wakeup cost last    (single): %lu \t\tus\n", cost[1]);
	printf("wakeup cost best    (single): %lu \t\tus\n", cost[2]);
	printf("wakeup cost 90%%     (single): %lu \t\tus\n", cost[3]);
	printf("wakeup cost 99%%     (single): %lu \t\tus\n", cost[4]);
	printf("wakeup cost 99.9%%   (single): %lu \t\tus\n", cost[5]);
	printf("wakeup cost 99.99%%  (single): %lu \t\tus\n", cost[6]);
	printf("wakeup cost worst   (single): %lu \t\tus\n", cost[NUM_OF_9 - 1]);

	printf("\n");
	return 0;
}

/*
 * Periodically wake up threads, which then immediately block waiting
 * for the next wakeup to arrive. %arrival_rate specifies the waiting
 * time between wakeups.
 */
int test_wakeup_periodic(void)
{
	unsigned long cost[NUM_OF_9];

	measure_wakeup_multi(num_threads, num_loops, wake_tasks_periodic, cost);
	printf("wakeup cost first   (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[0]);
	printf("wakeup cost last    (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[1]);
	printf("wakeup cost best    (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[2]);
	printf("wakeup cost 90%%     (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[3]);
	printf("wakeup cost 99%%     (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[4]);
	printf("wakeup cost 99.9%%   (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[5]);
	printf("wakeup cost 99.99%%  (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[6]);
	printf("wakeup cost worst   (periodic, %uus): %lu \t\tus\n", arrival_rate, cost[NUM_OF_9 - 1]);

	return 0;
}
