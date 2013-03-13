
#include "bench.h"
#include "cf.h"
#include "arch_dep.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define STEPS 2048

static int us_to_sleep = 0;

#define NUM_BENCH 4494
static int alloc_sizes[NUM_BENCH];

#define NUM_CLASSES 9
static int class_map[] = {
	16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
};

static int get_alloc_size(int class)
{
	int retval;
	int rand_num;

	rand_num = random();

	if(class == 0) {
		retval = rand_num % class_map[class];
	} else {
		retval = class_map[class] + rand_num % (class_map[class] - class_map[class - 1]);
	}
	return retval;
}

static int get_alloc_class()
{
	int retval;
	retval = random() % NUM_BENCH;
	return alloc_sizes[retval];
}

static int next_size()
{
	return get_alloc_size(get_alloc_class());
}


#define P_SIZE (1024*1024*100)
static volatile long w_idx = 0;
static volatile long r_idx = 0;
static void **pointers[P_SIZE];
static int inc = 1;


static int allocator(struct bench_stats *stats)
{
	int size;
	void **p;

	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = us_to_sleep * 1000;

	while(running()) {
		size = next_size();
		p = cf_malloc(size);
		if (!p) {
			printf("ERROR: out of Memory\n");
			return 1;
		}

		stats->bytes_allocated_netto += size;
		pointers[w_idx] = p;

		w_idx += 1;
		if(w_idx == P_SIZE) {
			printf("ERROR: pointer array too small\n");
			break;
		}
		if (us_to_sleep)
			nanosleep(&ts, NULL);
	}
	return 0;
}

static int deallocator(struct bench_stats *stats)
{
	void **p;
	int idx;
	while(running()) {
		idx = r_idx;
		if (idx + inc > w_idx) {
			sched_yield();
			continue;
		}
		if (cmpxchg(&r_idx, idx, idx + inc) != idx)
			continue;

		p = pointers[idx];

		cf_free(p);
	}
	return 0;
}

/* may not access global variables unsynchronized, 
 * since this function is executed by multiple thread 
 *
 * stats is thread private
 */
int bench_func(struct bench_stats *stats)
{
	if(stats->id == 0) {
		return allocator(stats);
	} else {
		return deallocator(stats);
	}
}

/* called prior thread creation*/
void set_block_size(int size)
{
	int i;

	if (size)
		inc = size;

	for(i=0; i<NUM_BENCH; ++i)
	{
		if (i<10) {
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
