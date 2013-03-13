
#include "bench.h"
#include "cf.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>


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

/* may not access global variables unsynchronized, 
 * since this function is executed by multiple thread 
 *
 * stats is thread private
 */
int bench_func(struct bench_stats *stats)
{
	int size;
	void **p;
//	fprintf(stderr, "run with id %d\n", stats->id);

	cf_malloc((stats->id + 1)*32);
	while(running()) {
		size = (stats->id + 1)*32;
		p = cf_malloc(size);
		if(!p) {
			perror("error");
			return 0;
		}
		//sched_yield();
		cf_free(p);
	}
	return 0;
}

/* called prior thread creation*/
void set_block_size(int size)
{
	int i;

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
