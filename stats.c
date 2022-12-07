#include <stdlib.h>
#include <stdio.h>
#include "adrestia.h"

static int cmp(const void *p1, const void *p2)
{
	unsigned long *a1 = (unsigned long *)p1;
	unsigned long *a2 = (unsigned long *)p2;

	if (*a1 < *a2)
		return -1;
	if (*a1 > *a2)
		return 1;

	return 0;
}

void stats_best(unsigned long *times, unsigned int entries, unsigned long *cost)
{
#ifdef MY_SCHED_POLICY
	//printf("entries: %d\n", entries);
	cost[0] = times[0];
	cost[1] = times[entries - 1];
#endif

	qsort(times, entries, sizeof(*times), cmp);

#if 0
	for (i = 0; i < entries;i++)
		printf("%lu ", times[i]);
	printf("\n");
#endif


#ifdef MY_SCHED_POLICY
	cost[2] = times[0];
	cost[3] = times[90 * (entries / 100)];
	cost[4] = times[99 * (entries / 100)];
	cost[5] = times[999 * (entries / 1000)];
	cost[6] = times[9999 * (entries / 10000)];
	cost[NUM_OF_9 - 1] = times[entries - 1];
#else
	/* 90th percentile */
	return times[9*entries/10];
#endif
}
