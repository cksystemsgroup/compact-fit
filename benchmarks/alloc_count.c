
#include "bench.h"
#include "cf.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>


#define NUM_BENCH 4494
static int alloc_sizes[NUM_BENCH];

#if 0
/* very good !!! */
static int static_alloc_sizes[] = { 28, 58, 74, 118, 170, 266, 350, 530 };
#else
/* very bad !!! */
static int static_alloc_sizes[] __attribute__((used)) = { 28, 32, 36, 46, 52, 66, 76, 90 };
#endif

/* may not access global variables unsynchronized, 
 * since this function is executed by multiple thread 
 *
 * stats is thread private
 */
int bench_func(struct bench_stats *stats)
{
    int size;
    void **p;
    size = 64;
    cf_malloc(size);
    while(stats->run) {
	size = 64;
	p = cf_malloc(size);
	if(!p) {
	    perror("error cf_malloc");
	    return 0;
	}
	//		sched_yield();
	stats->bytes_allocated_netto += size;
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
