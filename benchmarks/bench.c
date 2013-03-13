
#include "bench.h"
#include "cf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>

#define PAGESIZE (4*1024)

static inline uint64_t get_utime()
{
	struct timeval tv;
	uint64_t usecs;

	gettimeofday(&tv, NULL);

	usecs = tv.tv_usec + tv.tv_sec*1000000;

	return usecs;
}

pthread_key_t bench_key;

/* private globals */
static volatile int run = 1;
static pthread_t *threads;
static int num_threads = 1;
static int local_pages = 10000;
static int pages_buckets = 1;
static int local_aas = 1000000;
static int aas_buckets = 1;
static int aas_free_buckets = 1;
static struct bench_stats *statistics;
static int k = 1;
static int block_size = 0;
static int heap_size = 400*1024*1024;
static int aa_size = 80*1024*1024;
static int ms_to_run = 1000;
static int us_to_sleep = 0;
static int mcpy_inc = 0;
static int mcpy_mult = 1;
static int private = 0;
static FILE *stats_file;


int get_num_threads()
{
	return num_threads;
}

int running() 
{
	return run;
}

void clear_running()
{
	int i;
	for (i = 0; i < num_threads; ++i) {
		statistics[i].run = 0;
	}
}

/* do nothing 
 *
 * a benchmark implements this function
 */
__attribute__((weak)) int bench_func(struct bench_stats *stats) 
{
	while(running()) {
		sched_yield();
	}
	return 0;
}

/**
 * get benchmark memory (used in combination with bench.c)
 * @return benchmark data
 */
struct bench_stats *get_bench_stats()
{
#if USE_STATS
	return (struct bench_stats *)pthread_getspecific(bench_key);
#else
	return NULL;
#endif

}

static void *thread_func(void *arg)
{
	struct bench_stats *stats = (struct bench_stats *)arg;
	pthread_setspecific(bench_key, arg);
	if(bench_func(stats) == 0)
	    return stats;
	else
	    return NULL;
}

static void ms_wait(int ms)
{
	struct timespec req;

	req.tv_sec = ms/1000;
	req.tv_nsec = (ms%1000) * 1000000;
	nanosleep(&req, NULL);
}

static int size_class_2_size_map[SIZECLASSES];
static void init_size_classes_mapping(){
	uint16_t base;
	uint16_t res;
	uint16_t tmp;
	uint16_t class;

	class = 0;

	size_class_2_size_map[class] = MINPAGEBLOCKSIZE;

	class++;

	for(base=MINPAGEBLOCKSIZE; base<8000;){
		tmp = base;
		base = (int)base*SIZECLASSESFACTOR;
		res = base % 4;

		if(res!=0){
			base += (4 -res);
		}

		size_class_2_size_map[class] = base;

		class++;

		if(base*SIZECLASSESFACTOR>=8000){
			break;
		}
	}

	size_class_2_size_map[class] = 8000;
	size_class_2_size_map[class+1] = 16*1024;
}

static int calc_class_size(int class)
{
	return size_class_2_size_map[class];
}

static void sum_stats(uint64_t run_time)
{
	int i,j,class_size;
	long sum_allocs = 0;
	long sum_frees = 0;
//	cf_print_pages_status();
	init_size_classes_mapping();
	cf_print_sc_fragmentation();
	fprintf(stderr, "run time in ms %ld\n", (long)(run_time/1000));
	fprintf(stats_file, "size lock_avg lock_max compact max_compact free_avg free_max malloc_avg malloc_max\n");
	for(j=0; j<SIZECLASSES; ++j)
	{
		class_size = calc_class_size(j);
		for (i=0; i<num_threads; ++i) {
			fprintf(stats_file, "%d %llu %llu %llu %llu %llu %llu %llu %llu\n",
					class_size,
					statistics[i].num_lock_class[j]?
						statistics[i].lock_time[j]/statistics[i].num_lock_class[j]:0,
					statistics[i].lock_max_time[j],
					statistics[i].num_compact_class[j]?
						statistics[i].compaction_time[j]/statistics[i].num_compact_class[j]:0,
					statistics[i].max_compaction_time[j],
					statistics[i].num_free_class[j]?
						statistics[i].free_time[j]/statistics[i].num_free_class[j]:0,
					statistics[i].free_max_time[j],
					statistics[i].num_malloc_class[j]?
						statistics[i].malloc_time[j]/statistics[i].num_malloc_class[j]:0,
					statistics[i].malloc_max_time[j]
				   );

		}
	}

	fprintf(stderr, "thrd\t#alloc  \t#compact\t#free\tnetto  \tbrutto \tused_pages  \tmax_used_pages\n");
	for (i=0; i<num_threads; ++i) {
		sum_allocs += statistics[i].num_alloc/(run_time/1000000);
		sum_frees += statistics[i].num_free/(run_time/1000000);
		fprintf(stderr, "%4d\t%9ld\t%8ld\t%5ld\t%7ld\t%7ld\t%ld\t%ld\n", 
				i, 
				statistics[i].num_alloc,
				statistics[i].num_compaction,
			   	statistics[i].num_free,
				statistics[i].bytes_allocated_netto/(long)(run_time/1000000),
				statistics[i].bytes_allocated_brutto/(long)(run_time/1000000),
					statistics[i].used_pages,
					statistics[i].max_used_pages
				);
		/*
		for(j=0; j<SIZECLASSES; ++j)
			printf("%ld ", statistics[i].class_counter[j]);
		printf("\n");
		*/
	}
	fprintf(stderr, "nr_thread,block_size,allocs/sec,frees/sec\n"); 
	fprintf(stderr, "%d,%d,%ld,%ld\n", num_threads, block_size, sum_allocs, sum_frees);
	fprintf(stdout, "%d %d %ld %ld\n", num_threads, block_size, sum_allocs, sum_frees);
}

__attribute__((weak)) void set_block_size(int size)
{

}
__attribute__((weak)) void set_sleep_time(int us)
{

}
__attribute__((weak)) void set_input_file(char *file)
{

}
__attribute__((weak)) void set_mod_param(int mod)
{

}
__attribute__((weak)) void set_use_cf()
{

}

static void init_allocator()
{
	printf("heap size %d, addresses %d\n", heap_size, aa_size);
	cf_init(aa_size, heap_size, 
		local_pages, pages_buckets, 
		local_aas, aas_buckets, aas_free_buckets, 
		k, mcpy_inc, mcpy_mult, private);
}

#include <getopt.h>

int main(int argc, char **argv)
{

	int i;
	int use_rt = 0;
	uint64_t start_time;
	stats_file = stderr;

	static const struct option long_options[] =
	{
        { "aa-size", 		required_argument,	0, 'a' },
        { "block-size", 	required_argument,	0, 'b' },
        { "mcpy-mult",	 	required_argument, 	0, 'c' },
        { "use-cf", 		no_argument, 		0, 'd' },
        { "statfile", 		required_argument,	0, 'f' },
        { "mcpy-inc",		required_argument, 	0, 'i' },
	{ "ifile", 		required_argument,	0, 'j' },
        { "partial-comp",	required_argument,	0, 'k' },
        { "mem-size", 		required_argument, 	0, 'm' },
        { "num-threads", 	required_argument, 	0, 'n' },
        { "private", 		no_argument,		0, 'p' },
	{ "free-aas_buckets",	required_argument,	0, 'q' },
        { "sched", 		required_argument, 	0, 'r' },
	{ "sleep", 		required_argument, 	0, 's' },
        { "time",		required_argument, 	0, 't' },
        { "local-aas", 		required_argument,	0, 'v' },
        { "aas-buckets",	required_argument, 	0, 'w' },
	{ "mod", 		required_argument, 	0, 'x' },
        { "local-pages",	required_argument, 	0, 'y' },
        { "pages-buckets",	required_argument,	0, 'z' },
        { "help", 		no_argument, 		0, 'h' },
	{ "usage", 		no_argument,	 	0, 'u' },
		{0}	
	};

	while (optind < argc)
	{
		int index = -1;
		struct option * opt = 0;
		int result = getopt_long(argc, argv,
			"a:b:c:df:i:j:k:m:n:pq:r:s:t:v:w:x:y:z:hu",
			long_options, &index);
		if (result == -1) break; /* end of list */
		switch (result)
		{
			case 'a':
				aa_size = atoi(optarg);
				break;
			case 'b':
				block_size = atoi(optarg);
				break;
			case 'c':
				mcpy_mult = atoi(optarg);
				break;
			case 'd':
				set_use_cf();
				break;
			case 'f':
				stats_file = fopen(optarg, "w");
				if (!stats_file) {
					perror("fopen");
					exit(2);
				}
				break;
			case 'i':
				mcpy_inc = atoi(optarg);
				break;
			case 'j':
				set_input_file(optarg);
				break;
			case 'k':
				k = atoi(optarg);
				break;
			case 'm':
				heap_size = atoi(optarg);
				break;
			case 'n':
			    	num_threads = atoi(optarg);
			    	break;
			case 'p':
				private = 1;
				break;
			case 'q':
				aas_free_buckets = atoi(optarg);
			case 'r':
				use_rt = atoi(optarg);
				break;
			case 's':
				us_to_sleep = atoi(optarg);
				break;
			case 't':
			   	 ms_to_run = atoi(optarg);
			   	 break;
			case 'v' :
				local_aas = atoi(optarg);
				break;
			case 'w' :
				aas_buckets = atoi(optarg);
				break;
			case 'x':
			    	set_mod_param(atoi(optarg));
			    	break;
			case 'y':
				local_pages = atoi(optarg);
				break;
			case 'z':
				pages_buckets = atoi(optarg);
				break;
			case 'h':
			case 'u':
				fprintf(stderr, "\nUsage:\n%s\n", argv[0]);
				for(index=0; index<sizeof(long_options)/sizeof(struct option)-1; index++){
					opt = (struct option *)&(long_options[index]);
					fprintf(stderr, "   --%s ", opt->name);
					if(opt->has_arg == required_argument){
						fprintf(stderr, "<int>\n");
					}
					else{
						fprintf(stderr, "\n");
					}
				}
				fprintf(stderr, "\n");
				exit(0);
		}
	}

	set_block_size(block_size);
	set_sleep_time(us_to_sleep);
	init_allocator();

	printf("start %d threads for %d ms\n", num_threads, ms_to_run);
	struct sched_param param = { .sched_priority = 40 };
	if (use_rt) {
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			perror("setscheduler");
			exit(2);
		}
	}
	param.sched_priority--;
	struct sched_param _param;
	sched_getparam(0, &_param);
	printf("prio set to %d\n", _param.sched_priority);

	threads = malloc(num_threads*sizeof(pthread_t));
	if (!threads)
	    perror("malloc threads");

	statistics = malloc(num_threads*sizeof(struct bench_stats));
	if (!statistics)
	    perror("malloc statistics");

	memset(statistics, 0, num_threads*sizeof(struct bench_stats));
	if (!threads || !statistics) {
		fprintf(stderr, 
			"ERROR: could not allocate memory for threads or statistic data\n");
		exit(1);
	}

	if (pthread_key_create(&bench_key, NULL)) {
		perror("pthread_key_create");
		exit(2);
	}
	for (i=0;i<num_threads;++i) {
		statistics[i].id = i;
		statistics[i].run = 1;
		if (i == 0)
			statistics[i].us_to_sleep = us_to_sleep;
		else
			statistics[i].us_to_sleep = 0;

		pthread_attr_t attr;
		pthread_attr_init(&attr);
		if (use_rt == 1) {
			printf("use SCHED_FIFO\n");
		    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) perror("schedpolicy");
		} else if (use_rt == 2) {
			printf("use SCHED_RR\n");
		    if (pthread_attr_setschedpolicy(&attr, SCHED_RR)) perror("schedpolicy");
		} 

		if (use_rt) {
		    if (pthread_attr_setschedparam(&attr, &param)) perror("schedparam");
			printf("use EXPLICIT_SCHED priority %d\n", param.sched_priority);
		    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) perror("schedparam");
		}

		pthread_create(&threads[i], &attr, thread_func, statistics + i);
		pthread_attr_destroy(&attr);
/*		if (i == 0) {
			param.sched_priority--;
		}*/
	}

	start_time = get_utime();

	ms_wait(ms_to_run);
	clear_running();
	printf("running flag cleared\n");
	for (i=0;i<num_threads;++i)
	    pthread_join(threads[i], NULL);

	sum_stats(get_utime() - start_time);
	return 0;
}

