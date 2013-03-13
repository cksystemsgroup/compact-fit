#include "bench.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define MEM_SIZE (1024*1024*10)

static char mem[MEM_SIZE];

static int block_size = 16;

void set_block_size(int size)
{
	block_size = size;
	printf("block size set to %d\n", size);
	int i;
	for(i=0;i<MEM_SIZE;++i)
		mem[i] = i;
}


int bench_func(struct bench_stats *stats)
{
	int num_threads = get_num_threads();
	int i = 0;
	while(running()) {
		memcpy(&mem[MEM_SIZE/2 + (i + stats->id)*block_size],
				&mem[(i + stats->id)*block_size], 
				block_size);

		i += num_threads;
		if ((i + num_threads) >= MEM_SIZE /(block_size * 2))
			i = 0;
	}
	stats->num_alloc *= block_size;
	return 0;
}
