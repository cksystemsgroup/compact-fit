#ifndef BENCH_H
#define BENCH_H

#include "cf.h"
#include "tree.h"
#include "mdrv.h"
#include <pthread.h>

struct bench_stats {
	int id;
	int run;
	int us_to_sleep;
	long used_pages;
	long max_used_pages;
	long num_alloc;
	long num_free;
	long num_compaction;
	long bytes_allocated_netto;
	long bytes_allocated_brutto;
	long class_counter[SIZECLASSES];
	uint64_t lock_time[SIZECLASSES];
	uint64_t lock_max_time[SIZECLASSES];
	uint64_t num_lock_class[SIZECLASSES];
	uint64_t compaction_time[SIZECLASSES];
	uint64_t max_compaction_time[SIZECLASSES];
	uint64_t num_compact_class[SIZECLASSES];
	uint64_t free_time[SIZECLASSES];
	uint64_t free_max_time[SIZECLASSES];
	uint64_t num_free_class[SIZECLASSES];
	uint64_t malloc_time[SIZECLASSES];
	uint64_t malloc_max_time[SIZECLASSES];
	uint64_t num_malloc_class[SIZECLASSES];
	struct rectree rectree;
} __attribute__((aligned(128)));

extern struct bench_stats *get_bench_stats();

/* true while the benchmark threads should run */
extern int running();

extern pthread_key_t bench_key;

/* implemented by the benchmark */
extern int bench_func(struct bench_stats *stats);

extern int get_num_threads();

extern void set_block_size(int size);
extern void set_sleep_time(int us);
#endif /* BENCH_H */
