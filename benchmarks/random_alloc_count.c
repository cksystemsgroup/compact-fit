
#include "bench.h"
#include "cf.h"
#include "arch_dep.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define random rdtsc
#define STEPS 512
#define BUFFERS_PER_THREAD 32

static int us_to_sleep = 0;
static int share = 0;

#define NUM_BENCH 4494
static int alloc_sizes[NUM_BENCH];

#define NUM_CLASSES 10
static int class_map[] = {
	16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 16272,
};

static int get_alloc_size(int class)
{
	int retval;
	int rand_num;

	rand_num = random();

	if(class == 0) {
		retval = rand_num % class_map[class];
	} else {
		retval = class_map[class - 1] + rand_num % (class_map[class] - class_map[class - 1]);
	}
	return retval;
}

static int get_alloc_class()
{
#if 1
	int retval;
	retval = random() % NUM_BENCH;
	return alloc_sizes[retval];
#else
	return random() % NUM_CLASSES;
#endif
}

static int next_size()
{
	//return rdtsc() % 16272;

	return get_alloc_size(get_alloc_class());
}

struct objects {
	void *pointers[STEPS];
	int freed;
	int alloced;
};

static struct objects objs[8][BUFFERS_PER_THREAD];

/* may not access global variables unsynchronized, 
 * since this function is executed by multiple thread 
 *
 * stats is thread private
 */


static inline int get_free_objects(int id)
{
	int i;
	for (i=0;i<BUFFERS_PER_THREAD; ++i)
		if (!objs[id][i].alloced && objs[id][i].freed)
			return i;

	return -1;
}

static inline int get_alloc_objects(int id)
{
	int i;
	for (i=0;i<BUFFERS_PER_THREAD; ++i)
		if (objs[id][i].alloced && !objs[id][i].freed )
			return i;

	return -1;
}

int bench_func(struct bench_stats *stats)
{
	int i;
	int j;
	int size;
	struct timespec ts;
	int k;
	int loop = 0;
	int free_id;
	//long *mem;
	//	fprintf(stderr, "run with id %d\n", stats->id);

	ts.tv_sec = 0;
	ts.tv_nsec = stats->us_to_sleep * 1000;

	for(i=0; i<BUFFERS_PER_THREAD; ++i) {
		objs[stats->id][i].freed = 1;
		objs[stats->id][i].alloced = 0;
	}

	while (stats->run) {
		j = get_free_objects(stats->id);
		if (j < 0) {
			sched_yield();
			perror("free objs");
			continue;
		}
		//printf("%d uses alloc set %d\n", stats->id, j);

		objs[stats->id][j].freed = 0;
		for(i=0; i<STEPS; ++i) {
			size = next_size();
			if(size == 0)
				size = 4;
			stats->bytes_allocated_netto += size;
			objs[stats->id][j].pointers[i] = cf_malloc(size);
			if(!objs[stats->id][j].pointers[i]) {
				perror("alloc");
				break;
			}
			//			mem = (long *)cf_dereference(pointers[stats->id][i], (size/2)/4);
			//			*mem = (long)get_utime();
			if (stats->us_to_sleep)
				nanosleep(&ts, NULL);
		}
		objs[stats->id][j].alloced = i;
		//		sched_yield();

		if (share && ++loop == share) {
			free_id = (stats->id + 1) % get_num_threads();
			loop = 0;
		} else {
			free_id = stats->id;
		}
retry:
		j = get_alloc_objects(free_id);
		if (j < 0) {
			sched_yield();
			//			goto retry;
			perror("alloc objs");
			continue;
		}
		//printf("%d uses %d to free set %d\n", stats->id, free_id, j);
		k = objs[free_id][j].alloced;
		if (k == 0 || cmpxchg(&objs[free_id][j].alloced, k, 0) != k) {
			perror("cmpxchg");
			goto retry;
		}

		for(i=0; i<k; ++i) {
			cf_free(objs[free_id][j].pointers[i]);
			objs[free_id][j].pointers[i] = NULL;
			if (stats->us_to_sleep)
				nanosleep(&ts, NULL);
		}
		objs[free_id][j].freed = 1;

	}
#if 1
	/* cleanup my buffers */
	free_id = stats->id;
	for(j=0; j < BUFFERS_PER_THREAD; ++j) {
		k = objs[free_id][j].alloced;
		if (k == 0 || objs[free_id][j].freed == 1 || cmpxchg(&objs[free_id][j].alloced, k, 0) != k) {
			continue;
		}

		for(i=0; i<k; ++i) {
			//			mem = (long *)cf_dereference(pointers[stats->id][i], 0);
			//			*mem = (long)get_utime();
			cf_free(objs[free_id][j].pointers[i]);
			objs[free_id][j].pointers[i] = NULL;
			if (stats->us_to_sleep)
				nanosleep(&ts, NULL);
		}
		objs[free_id][j].freed = 1;
	}
#endif
	return 0;
}

/* called prior thread creation*/
void set_block_size(int size)
{
	int i;

	share = size;
	printf("use share value %d\n", share);
	for(i=0; i<NUM_BENCH; ++i)
	{
		if (i<NUM_CLASSES) {
			alloc_sizes[i] = 0;
		} else if (i < 108) {
			alloc_sizes[i] = 1;
		} else if (i < 4433) {
			alloc_sizes[i] = 2;
		} else if (i < 4451) {
			alloc_sizes[i] = 3;
		} else if (i < 4462) {
			alloc_sizes[i] = 4;
		} else if (i < 4475) {
			alloc_sizes[i] = 5;
		} else if (i < 4480) {
			alloc_sizes[i] = 6;
		} else if (i < 4488) {
			alloc_sizes[i] = 7;
		} else {
			alloc_sizes[i] = 8;
		}
	}
}

void set_sleep_time(int us)
{
	us_to_sleep = us;
}
