/*
 * Copyright (c) Hannes Payer hpayer@cs.uni-salzburg.at
 * cs.uni-salzburg.at/~hpayer
 *
 * University Salzburg, www.uni-salzburg.at
 * Department of Computer Science, cs.uni-salzburg.at
 */

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "cf.h"
#include "arch_dep.h"
#include "page_stack.h"
#include "aa_stack.h"
#include "aa_bucket_stack.h"
#include "list.h"

#include "benchmarks/bench.h" //include for statistics TODO cleanup

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>


/* 
TODO: CLEANUP
TODO: implement page locking scheme 
TODO: make threads dynamic
TODO: make bitmap operations non-blocking
TODO: make size-classes dynamic
TODO: need free a buckets initialization?
TODO: make partial compaction bound dynamic for each thread
TODO: integrate nice with benchmark
*/


////////////////////////////////////////////////////////////////////////////////////////
// Macros/Constants
////////////////////////////////////////////////////////////////////////////////////////

/**
 * page size
 */
#define PAGESIZE 16384

/**
 * pages are 16k, low 14 bits are 0, and high 18 bits contain address 
 */
#define PAGE_SHIFT (14)
#define PAGE_MASK ((1<<PAGE_SHIFT) - 1)

/**
 * 160B data of page header struct + 24B lock
 */
#define PAGEHEADER 184

/**
 * memory of a page used for objects (heap memory)
 */
#define PAGEDATASIZE (PAGESIZE - PAGEHEADER)

/**
 * size of an abstract address
 */
#define ABSTRACTADDRESSSIZE sizeof(void **)

/**
 * largest size-class
 */
#define LARGESTSC 8000

/** 
 * compaction abort flag
 * another compaction operation moved the target block
 */
#define  ABORT_COMPACT_FLAG 0x1

/**
 * free abort flag
 * another free operation freed the source block
 */
#define  ABORT_FREE_FLAG    0x2

/**
 * pages needed for thread local data structures
 */
#define THREAD_LOCAL_PAGES 2

/**
 * page size of machine
 */
#define MACHINE_PAGESIZE   4096

/**
 * compare exchange
 */
#define CAS cmpxchg

/**
 * list flags for debugging
 */
#define NFULL_LIST_FLAG 1
#define FULL_LIST_FLAG 2
#define SOURCE_LIST_FLAG 3
#define EMPTYING_LIST_FLAG 4
#define FREE_LIST_FLAG 5

////////////////////////////////////////////////////////////////////////////////////////
// Structures
////////////////////////////////////////////////////////////////////////////////////////

/**
 * a page
 */
struct page {
	char memory[PAGEDATASIZE];		/**< memory where objects are stored */
	struct list_head list;
	uint32_t nr_used_page_blocks; 		/**< number of used page blocks of the page*/
	uint32_t list_flags;			/**< determines state of page*/		
	uint32_t sourceentries;			/**< number of source entries of the page (if page is a source page)*/
	struct size_class *sc;			/**< reference to size-class*/
	uint32_t used_page_block_bitmap[34]; 	/**< 2-dimensional bitmap to find used/free page blocks*/
	pthread_mutex_t lock;			/**< page lock*/
};



/**
 * a size-class
 */
struct size_class {
	struct list_head head_full_pages; 	/**< head of full pages of size-class*/
	struct list_head head_nfull_pages; 	/**< head of not-full pages of size-class*/
	struct list_head head_source_pages;	/**< head of source pages of size-class*/
	struct list_head head_emptying_pages;	/**< head of emptying pages of size-class*/
	uint32_t nfullentries;			/**< number of not-full pages of size-class*/
	uint32_t max_nfullentries;		/**< maximum reached number of not-full pages of size-class (stats)*/
	uint32_t sourceentries;			/**< number of source pages of size-class*/
	uint32_t max_sourceentries;		/**< maximum reached number of source pages of size-class (stats)*/
	int32_t partial_compaction_bound;	/**< partial compaction bound*/ 
	int workers;				/**< threads working on size-class (stats)*/
	int block_size;				/**< page-block size*/
	int nr_pages;				/**< number of pages (stats)*/
	int max_nr_pages;			/**< maximum reached number of pages (stats)*/
#if defined(LOCK_CLASS) || defined(LOCK_PAGE)
	pthread_mutex_t lock;			/**< size-class lock*/
#endif
}  __attribute__((aligned(128))); 		/* IMPORTANT: avoids cache invalidations*/

/**
 * private thread data
 */
struct thread_data {
	int id;					/**< thread id*/
	int pages_count;			/**< number of local pages*/
	struct mem_page *pages;			/**< local pages*/
	int pbuckets_count;			/**< number of local page buckets*/
	struct mem_page *pbuckets;		/**< page buckets list*/
	struct mem_page *last_pbucket;		/**< last bucket of page buckets list*/
	int aas_count;				/**< number of local abstract addresses*/
	long aas;				/**< local abstract addresses */
	int abuckets_count;			/**< number of local abstract address buckets*/
	struct mem_aa *abuckets;		/**< local abstract addresses buckets*/
	struct mem_aa *last_abucket;		/**< last bucket of abstract address buckets list*/
	int free_abuckets_count;		/**< number of free abstract address buckets*/
	struct mem_aa_bucket *free_abuckets;	/**< free abstract address buckets*/
	struct size_class *size_classes;	/**< private size-classes*/
};
static struct thread_data *get_thread_data();



////////////////////////////////////////////////////////////////////////////////////////
// Globals
////////////////////////////////////////////////////////////////////////////////////////

#ifdef GLOBAL_STATS
static long nr_sources = 0;
static long nr_nfull = 0; 
static long used_pages = 0;
static long max_sources = 0; 
static long max_nfull = 0; 
static long max_used_pages = 0;
#endif

/**
 * number of pages
 */
static unsigned long nr_pages = 0;

/**
 * head of free pages list
 */
static nb_stack free_page_head;

/**
 * head of free abstract address list
 */
static nb_stack free_aa_head;

/**
 * head of fre abstract address buckets list
 */
static nb_stack free_aa_buckets_head;

/**
 * pages memory
 */
static struct page *pages = NULL;

/**
 * number of per thread local free abstract address buckets
 */
static int nr_local_free_abuckets = 1;

/**
 * number of per thread local page buckets
 */
static int nr_local_pbuckets = 1;

/**
 * number of per thread local abstract address buckets
 */
static int nr_local_abuckets = 1;

/**
 * number of per thread local pages per bucket
 */
static int nr_local_pages = 1;

/**
 * number of per thread local abstract addresses per bucket
 */
static int nr_local_aas = 2;

/**
 * abstract address space size
 */
static unsigned long nr_aas; 

/**
 * number of abstract address buckets
 */
static unsigned long nr_aa_buckets;

/**
 * memcopy multiplicator >=1 (used for tests only)
 *
 */
static int memcpy_multiplicator = 1;

/**
 * memcopy increment, 0 = not in use
 */
static int incremental_memcpy = 0;

/**
 * private size-classes in use (boolean)
 */
static int private_classes = 0;

/**
 * abstract address space memory
 */
static unsigned long *aa_space;

/**
 * abstract address buckets
 */
static struct mem_aa *aa_buckets;

/**
 * heap size (concrete address space)
 */
static unsigned long heap_size; 

/**
 * thread id counter (atomically incremented)
 */
static int next_thread_id = -1;

/**
 * partial compaction bound 
 */
static int partial_compaction_bound;

/**
 * maps an allocation request to a size-class
 */
static uint16_t size_2_size_class_map[2000];

/**
 * maps a size-class index to the max page-block size of the given size-class
 */
static uint16_t size_class_2_size_map[SIZECLASSES];

/**
 * thread data pthread specific key
 */
static pthread_key_t cf_key;

/**
 * thread global size-classes memory
 */
static char global_classes[THREAD_LOCAL_PAGES*MACHINE_PAGESIZE];

/*
 * benchmark data TODO:move to benchmark
 */
static int pages_count = 0;
static int max_pages_count = 0;


////////////////////////////////////////////////////////////////////////////////////////
// Locks
////////////////////////////////////////////////////////////////////////////////////////

#if LOCK_GLOBAL
static pthread_mutex_t global_alloc_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void lock()
{
	pthread_mutex_lock(&global_alloc_lock);
}

static inline void unlock()
{
	pthread_mutex_unlock(&global_alloc_lock);
}

static pthread_mutex_t page_list_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void lock_free_page()
{
	pthread_mutex_lock(&page_list_lock);
}

static inline void unlock_free_page()
{
	pthread_mutex_unlock(&page_list_lock);
}

static pthread_mutex_t address_list_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void lock_free_address()
{
	pthread_mutex_lock(&address_list_lock);
}

static inline void unlock_free_address()
{
	pthread_mutex_unlock(&address_list_lock);
}

static inline void init_page_lock(struct page *page) { }

static inline void lock_page(struct page *page) { }

static inline void unlock_page(struct page *page) { }

static inline void init_sizeClass_lock(struct size_class *sc) { }

static inline void lock_sizeClass(struct size_class *sc) { }

static inline void unlock_sizeClass(struct size_class *sc) { }

#elif defined(LOCK_CLASS) || defined(LOCK_PAGE)
static inline void lock() { }

static inline void unlock() { }

static pthread_mutex_t page_list_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void lock_free_page()
{
	pthread_mutex_lock(&page_list_lock);
}

static inline void unlock_free_page()
{
	pthread_mutex_unlock(&page_list_lock);
}

static pthread_mutex_t address_list_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void lock_free_address()
{
	pthread_mutex_lock(&address_list_lock);
}

static inline void unlock_free_address()
{
	pthread_mutex_unlock(&address_list_lock);
}
#ifdef LOCK_PAGE
static inline void init_page_lock(struct page *page)
{
	pthread_mutex_init(&page->lock, NULL);
}

static inline void lock_page(struct page *page)
{
    struct thread_data *data = get_thread_data();
    uint64_t end;
    uint64_t start;
    int i = page->sc - data->size_classes;

    start = get_utime();
    pthread_mutex_lock(&page->lock);
    end = get_utime();
#if USE_STATS
    struct bench_stats *stats = get_bench_stats();
    stats->lock_time[i] += end - start;
    if(stats->lock_max_time[i] < end - start)
		stats->lock_max_time[i] = end -start;
    stats->num_lock_class[i]++;
	if(stats->num_lock_class[i] == 5)
		stats->lock_max_time[i] = 0;
#endif
}

static inline void unlock_page(struct page *page)
{
	pthread_mutex_unlock(&page->lock);
}
#else
static inline void init_page_lock(struct page *page) { }

static inline void lock_page(struct page *page) { }

static inline void unlock_page(struct page *page) { }
#endif

static inline void init_sizeClass_lock(struct size_class *sc)
{
	pthread_mutex_init(&sc->lock, NULL);
}

static inline void lock_sizeClass(struct size_class *sc)
{
    if(incremental_memcpy)
	atomic_inc(&sc->workers);

   // struct bench_stats *stats = get_bench_stats();TODO:check this
   // struct thread_data *data = get_thread_data();
   // uint64_t end;
   // uint64_t start;
   // int i = sc - data->size_classes;

   // start = get_utime();
    pthread_mutex_lock(&sc->lock);
#if 0
    end = get_utime();
    stats->lock_time[i] += end - start;
    if(stats->lock_max_time[i] < end - start)
	stats->lock_max_time[i] = end -start;
    stats->num_lock_class[i]++;
#endif
}

static inline void unlock_sizeClass(struct size_class *sc)
{
	pthread_mutex_unlock(&sc->lock);
	if(incremental_memcpy)
		atomic_dec(&sc->workers);
}

#elif LOCK_NONE
/* all noops */
static inline void lock() { }

static inline void unlock() { }

static inline void lock_free_page() { }

static inline void unlock_free_page() { }

static inline void lock_free_address() { }

static inline void unlock_free_address() { }

static inline void init_page_lock(struct page *page) { }

static inline void lock_page(struct page *page) { }

static inline void unlock_page(struct page *page) { }

static inline void init_sizeClass_lock(struct size_class *sc) { }

static inline void lock_sizeClass(struct size_class *sc) { }

static inline void unlock_sizeClass(struct size_class *sc) { }
#else
#error unknown locking scheme
#endif

/////////////////////////////////////////////////////////////////////
// Abstract Address Space
/////////////////////////////////////////////////////////////////////

/**
 * give free abstract address back
 * @parm id abstract address
 */
static void put_free_aa(long id)
{
	struct thread_data *data;
	struct mem_aa *maa;

	data = get_thread_data();

	/*full abstract addresses bucket*/
	if (data->aas_count == nr_local_aas){
		
		/*no free abstract address buckets*/
		if(!data->free_abuckets_count){
			data->free_abuckets = aa_bucket_get(&free_aa_buckets_head);
			assert(data->free_abuckets != NULL);
			if(data->free_abuckets){
				data->free_abuckets_count = nr_local_free_abuckets;
			}
		}
		
		/*take free abstract addresses bucket*/
		maa = (struct mem_aa *)data->free_abuckets;
		data->free_abuckets = data->free_abuckets->local_aa_bucket;
		data->free_abuckets_count--;
		
		/*install the free bucket and initialize new bucket*/
		maa->next = data->abuckets;
		maa->local_aas = data->aas;
		data->abuckets = maa;
		data->abuckets_count++;
		data->aas_count = 0;

		/*initialize last pointer*/
		if(data->abuckets_count == 1){
			data->last_abucket = data->abuckets;
		}

		/*return buckets to the global free list*/
		if(data->abuckets_count >= nr_local_abuckets){
			aa_put(data->abuckets, data->last_abucket, &free_aa_head);
			data->abuckets_count = 0;
			data->abuckets = NULL;
			data->last_abucket = NULL;
		}
	}

	/*skip first entry*/
	if(data->aas_count){
		aa_space[id] = data->aas;
	}

	/*create list links*/
	data->aas = id;
	data->aas_count++;
}

/**
 * get free abstract address
 * @return abstract address id
 */
static long get_free_aa()
{	
	struct thread_data *data;
	struct mem_aa *maa;
	unsigned long next = 0;
	unsigned long new = 0;

	data = get_thread_data();

	/*there are no abstract addresses*/
	if (data->aas_count == 0) {
		/*there are no abstract address buckets*/
		if(data->abuckets_count == 0){
			maa = aa_get(&free_aa_head);
			if(!maa){
				return -1;
			}
		}
		else{
			maa = data->abuckets;
			data->abuckets = maa->next;
			data->abuckets_count--;
		}
		
		/*install new bucket data*/	
		data->aas = maa->local_aas;
		data->aas_count = nr_local_aas;
		
		/*give some of the free buckets back if we have to much*/
		if(data->free_abuckets_count >= nr_local_free_abuckets){
			aa_bucket_put(data->free_abuckets, data->free_abuckets, &free_aa_buckets_head);
			data->free_abuckets = NULL;
			data->free_abuckets_count = 0;
		}
		
		/*add the free bucket to the private free buckets list*/
		((struct mem_aa_bucket *)maa)->local_aa_bucket = data->free_abuckets;
		data->free_abuckets = (struct mem_aa_bucket *)maa;
		data->free_abuckets_count++;
	}

	next = data->aas;
	new = aa_space[next];
	data->aas = new;
	data->aas_count--;

	return next;
}


/**
 * initialize abstract address space
 */
static void init_free_aa_list(){
	int i;

	free_aa_head = NB_STACK_INITIALIZER;
	free_aa_buckets_head = NB_STACK_INITIALIZER;	

	for(i=1; i<=nr_aas; i++){
		if(i%nr_local_aas){
			aa_space[i] = i-1;
		}
		else{
			aa_buckets[(i-1)/nr_local_aas].local_aas = i-1;
			aa_put(&aa_buckets[(i-1)/nr_local_aas], &aa_buckets[(i-1)/nr_local_aas], &free_aa_head);
		}		
	}
}

/**
 * get an abstract address
 * @param page_block page-block pointer
 * @return assigned abstract address
 */
static inline void **get_abstract_address(void *page_block){
	long free_id;
	void **retval = NULL;
	free_id = get_free_aa();
	//printf("free id %d\n", free_id);
	if(free_id>=0 && free_id<nr_aas) {
		aa_space[free_id] = (long)page_block;
		retval = (void **)&aa_space[free_id];
	}
	return retval;
}

/**
 * clear abstract address
 * @param aa abstract address
 */
static void clear_abstract_address(void *aa){
	long id;
	id = ((unsigned long *)aa) - aa_space;
	//printf("clear id %ld\n", id);
	put_free_aa(id);
}

/**
 * print the abstract address space
 */
void print_aa_space(){

	printf("\nAbstract Address Space:\n");

	uint32_t i;
	for(i=0; i<nr_aas; i++){
		printf("%d: %p ", i, (void *)aa_space[i]);
	}
	printf("\n\n");
}

/////////////////////////////////////////////////////////////////////
// Pages
/////////////////////////////////////////////////////////////////////

/**
 * init free pages
 */
static void init_free_pages_list(){
	size_t i;
	int j;
	struct page *p = pages;
	struct mem_page *mp;

	free_page_head = NB_STACK_INITIALIZER;

	/*add pages to free list*/
	j = 0;
	mp = NULL;
	for(i=0; i<nr_pages; i++){
		p = pages + i;
		p->list_flags = FREE_LIST_FLAG;
		init_page_lock(p);

		if (j < nr_local_pages) {
			((struct mem_page *)p)->local_pages = mp;
			mp = (struct mem_page *)p;
			++j;
		} 
		if(j == nr_local_pages){
			page_put(mp, mp, &free_page_head);
			j=0;
			mp = NULL;
		}
	}

	//printf("pages btw %p and %p\n", pages, p - 1);
	//page_print_head(&free_page_head);
}

/**
 * returns a page from the page free-list
 * @return free page
 */
static inline struct page *get_free_page(){
	struct page *p;
	struct mem_page *mp;
	struct thread_data *data;

	data = get_thread_data();

	if (data->pages_count == 0) {
		if(data->pbuckets_count == 0){
			mp = page_get(&free_page_head);
			if (mp) {
				data->pages_count = nr_local_pages;
				data->pages = mp;
			} else {
				return NULL;
			}
		}
		else{
			data->pages = data->pbuckets;
			data->pages_count = nr_local_pages;
			data->pbuckets = data->pbuckets->next;
			data->pbuckets_count--;
		}
	}
	p = (struct page *)data->pages;
	data->pages = data->pages->local_pages;
	data->pages_count--;


	if (p) {
		assert(p->list_flags == FREE_LIST_FLAG);
		p->list_flags = 0;
#ifdef GLOBAL_STATS
		used_pages++;
		if (used_pages > max_used_pages)
			max_used_pages = used_pages;
#endif

#ifdef USE_STATS
		struct bench_stats *stats;
		stats = get_bench_stats();
		stats->used_pages++;
		if (stats->used_pages > stats->max_used_pages)
			stats->max_used_pages = stats->used_pages;
#endif
	}
	return p;
}

/**
 * return free page to the list of free pages
 * @param p free page
 */
static inline void add_free_page(struct page *p){
	struct mem_page *mp;
	struct thread_data *data;
	assert(p!=NULL);
	assert(p->list_flags == 0);	
#ifdef USE_STATS
	struct bench_stats *stats;
	stats = get_bench_stats();
	stats->used_pages--;
#endif
#ifdef GLOBAL_STATS
	used_pages--;
#endif
	data = get_thread_data();
	
	if (data->pages_count == nr_local_pages) {
		data->pages->next = data->pbuckets;
		data->pbuckets = data->pages;
		data->pbuckets_count++;		
		data->pages_count = 0;
		data->pages = NULL;
		if(data->pbuckets_count == 1){
			data->last_pbucket = data->pbuckets;
		}
		if(data->pbuckets_count >= nr_local_pbuckets){
			page_put(data->pbuckets, data->last_pbucket, &free_page_head);
			data->pbuckets_count = 0;
			data->pbuckets = NULL;
			data->last_pbucket = NULL;
		}
	}
		
	p->list_flags = FREE_LIST_FLAG;	
	mp = (struct mem_page *)p;
	mp->local_pages = data->pages;
	data->pages = mp;
	data->pages_count++;
}

/**
 * checks state of page; full or not full
 * @param p page
 * @param page_block_size size of page-block
 * @return 1 if full, otherwise 0
 */
static int inline is_page_full(struct page *p, int page_block_size){
	return (p->nr_used_page_blocks == PAGEDATASIZE/page_block_size);
}

/////////////////////////////////////////////////////////////////////
// Page-blocks
/////////////////////////////////////////////////////////////////////

static inline int get_index_direct_of_page_block(struct page *p, void *page_block, int size)
{
	int diff;
	diff = (char *)page_block - (char *)p;
	return diff/size;
}

/**
 * return the page of a page block
 * @param page_block page block
 * @return page of page block
 */
static inline struct page *get_page_direct_of_page_block(void *page_block){
	return (struct page *)((long)page_block & ~PAGE_MASK);
}

/**
 * return the size-class of a page
 * @param p page
 * @return size-class
 */
static inline struct size_class *get_size_class_of_page(struct page *p){
    return p->sc;
}

/**
 * return the page-block size of a size-class
 * @param sc size-class
 * @return page block size
 */
static inline int get_page_block_size_of_size_class(struct size_class *sc){
	return sc->block_size;
}

/**
 * find a used page block of a page
 * @param p page
 * @return page block index
 */
static inline int find_used_page_block(struct page *p)
{
	int bitmap_index;
	int bitmap_2D_index;

	bitmap_index = _ffs(p->used_page_block_bitmap[0]);
	bitmap_2D_index = _ffs(p->used_page_block_bitmap[bitmap_index+2]);

	return bitmap_index*32 + bitmap_2D_index;
}

/**
 * clear bit
 * @param mem bitmap
 * @param bitmap_index index in bitmap
 * @param bitmap_2D_index index in 2nd dimension
 */
static inline void __clear_bit(uint32_t mem[], int bitmap_index, int bitmap_2D_index)
{
	mem[bitmap_index+2] &= ((1 << bitmap_2D_index)^0xffffffff);

	if(mem[bitmap_index+2] == 0){
		mem[0] &= ((1 << bitmap_index)^0xffffffff);
	}

	mem[1] &= ((1 << bitmap_index)^0xffffffff);
}

/**
 * set bit
 * @param mem bitmap
 * @param bitmap_index index in bitmap
 * @param bitmap_2D_index index in 2nd dimension
 */
static inline void __set_bit(uint32_t mem[], int bitmap_index, int bitmap_2D_index)
{
	mem[0] |= (1 << bitmap_index);
	mem[bitmap_index+2] |= (1 << bitmap_2D_index);

	if(mem[bitmap_index+2] == 0xffffffff){
		mem[1] |= (1 << bitmap_index);
	}
}

/**
 * clear used bit
 * @param p page
 * @param index page-block index
 */
static inline void page_clear_used_bit(struct page *p, int index)
{
	int bitmap_index;
	int bitmap_2D_index;

	bitmap_index = index/32;
	bitmap_2D_index = index%32;

	__clear_bit(p->used_page_block_bitmap, bitmap_index, bitmap_2D_index);
}

/**
 * free used page-block bitmap entry
 * @param p page
 * @param index bitmap index
 */
static inline void free_used_page_block(struct page *p, int index){
	lock_page(p);

	page_clear_used_bit(p, index);

	assert(p->nr_used_page_blocks > 0);
	p->nr_used_page_blocks--;

	unlock_page(p);
}

/**
 * find free bitmap entry
 * @param p page
 * @return bitmap index
 */
static inline int get_free_page_block(struct page *p){
	int bitmap_index;
	int bitmap_2D_index;

	lock_page(p);

	bitmap_index = _ffs(p->used_page_block_bitmap[1]^0xffffffff);
	bitmap_2D_index = _ffs(p->used_page_block_bitmap[bitmap_index+2]^0xffffffff);

	__set_bit(p->used_page_block_bitmap, bitmap_index, bitmap_2D_index);

	p->nr_used_page_blocks++;

	unlock_page(p);

	return bitmap_index*32 + bitmap_2D_index;
}

/////////////////////////////////////////////////////////////////////
// Size-classes
/////////////////////////////////////////////////////////////////////

/**
 * TODO: CLEANUP
 * init the size-class mappings
 */
static void init_size_classes_mapping(){
	int i;

	uint16_t base;
	uint16_t res;
	uint16_t tmp;
	uint16_t class;

	class = 0;

	for(i=0; i<MINPAGEBLOCKSIZE/4; i++){
		size_2_size_class_map[i] = class;
	}
	size_class_2_size_map[class] = MINPAGEBLOCKSIZE;

	class++;

	for(base=MINPAGEBLOCKSIZE; base<LARGESTSC;){
		tmp = base;
		base = (int)base*SIZECLASSESFACTOR;
		res = base % 4;

		if(res!=0){
			base += (4 -res);
		}

		uint16_t j;
		for(j=0; j<(base-tmp)/4; j++, i++){
			size_2_size_class_map[i] = class;
		}
		size_class_2_size_map[class] = base;

		class++;

		if(base*SIZECLASSESFACTOR>=LARGESTSC){
			break;
		}
	}

	for(; i<LARGESTSC/4; i++){
		size_2_size_class_map[i] = class;
	}
	size_class_2_size_map[class] = LARGESTSC;
	size_class_2_size_map[class+1] = PAGEDATASIZE;
	assert(class < SIZECLASSES - 1);
}

/**
 * get page-block size to size-class index
 * @param i index
 * @return page-block size
 */
static int get_page_block_size_of_index(int i)
{
	return size_class_2_size_map[i];
}

/**
 * init the size-class instances
 * @param sc size-classes
 */
static void init_size_class_instance(struct size_class *sc){
	int i;

	for(i=0; i<SIZECLASSES;i++){
		memset(&sc[i], 0, sizeof(struct size_class));
		sc[i].partial_compaction_bound = partial_compaction_bound;
		sc[i].block_size = get_page_block_size_of_index(i);
		INIT_LIST_HEAD(&sc[i].head_full_pages);
		INIT_LIST_HEAD(&sc[i].head_nfull_pages);
		INIT_LIST_HEAD(&sc[i].head_source_pages);
		INIT_LIST_HEAD(&sc[i].head_emptying_pages);
		init_sizeClass_lock(&sc[i]);
	}
}

/**
 * return the tail of the not full pages list of size-class
 * @param sc size-class
 * @return not-full page
 */
static inline struct page *get_nfull_pages_tail(struct size_class *sc){
	return list_entry((sc->head_nfull_pages).prev, struct page, list);
}

/**
 * check not full pages ratio; used for partial compaction
 * @param sc size-class
 * @param add increment
 * @return 1/0 boolean
 */
static inline int valid_nfull_pages_ratio(struct size_class *sc, int add){
	if((sc->nfullentries + add) > sc->partial_compaction_bound){
		return 0;
	}
	return 1;
}

/**
 * get the size-class number to size
 * @param size requested size
 * @return size class index
 */
static inline int get_size_class_index (size_t size){
	if(size<=LARGESTSC){
		return size_2_size_class_map[size/4];
	}
	return SIZECLASSES-1;
}

/**
 * get the size-class to a given size
 * @param size requested size
 * @return return reference to size-class
 */
static inline struct size_class *get_size_class(size_t size){
	struct thread_data *data;
	data = get_thread_data();
	return &data->size_classes[get_size_class_index(size)];	
}

/**
 * add page to a list
 * @param sc size-class
 * @param list list to add
 * @param p page
 */
static void inline add_page_to_list(struct size_class *sc, struct list_head *list, struct page *p){
	list_add(list, &p->list);

	sc->nr_pages++;
	if(sc->nr_pages > sc->max_nr_pages){
		sc->max_nr_pages++;
	}
}

/**
 * remove page from its list
 * @param sc size-class
 * @param p page
 */
static void inline remove_page_from_list(struct size_class *sc, struct page *p){
	list_del(&p->list);
	sc->nr_pages--;
}

/**
 * add page to not-full list of size-class
 * @param sc size-class 
 * @param p page
 */
static void add_page_to_nfull_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == 0);
	add_page_to_list(sc, &sc->head_nfull_pages, p);
	p->list_flags = NFULL_LIST_FLAG;
	sc->nfullentries++;

	if (sc->nfullentries > sc->max_nfullentries)
		sc->max_nfullentries = sc->nfullentries;

#ifdef GLOBAL_STATS
	nr_nfull++;
	if (nr_nfull > max_nfull)
		max_nfull = nr_nfull;
#endif
}

/**
 * remove page from not full list of size-class
 * @param sc size-class
 * @param p page
 */
static inline void remove_page_from_nfull_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == NFULL_LIST_FLAG);
	remove_page_from_list(sc, p);

	sc->nfullentries--;
#ifdef GLOBAL_STATS
	nr_nfull--;
#endif
	p->list_flags = 0;
}

/**
 * add page to full list of size-class
 * @param sc size-class
 * @param p page
 */
static void add_page_to_full_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == 0);
	add_page_to_list(sc, &sc->head_full_pages, p);
	//list_add(&sc->head_full_pages, &p->list);
	p->list_flags = FULL_LIST_FLAG;
}

/**
 * remove page from full list of size-class 
 * @param sc size-class
 * @param p page
 */
static void remove_page_from_full_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == FULL_LIST_FLAG);
	remove_page_from_list(sc, p);
	p->list_flags = 0;
}

/**
 * add page to source list of size-class
 * @param sc size-class
 * @param p page
 */

static void add_page_to_source_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == 0);
	add_page_to_list(sc, &sc->head_source_pages, p);
	p->list_flags = SOURCE_LIST_FLAG;

	sc->sourceentries++;
	if (sc->sourceentries > sc->max_sourceentries)
		sc->max_sourceentries = sc->sourceentries;
#ifdef GLOBAL_STATS
	nr_sources++;
	if (nr_sources > max_sources)
		max_sources = nr_sources;
#endif
}

/**
 * remove page from source list of size-class
 * @param sc size-class
 * @param p page
 */
static void remove_page_from_source_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == SOURCE_LIST_FLAG);
	remove_page_from_list(sc, p);
	p->list_flags = 0;

	sc->sourceentries--;
#ifdef GLOBAL_STATS
	nr_sources--;
#endif
}

/**
 * add page to emptying list of size-class
 * @param sc size-class
 * @param p page
 */
static void add_page_to_emptying_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == 0);
	add_page_to_list(sc, &sc->head_emptying_pages, p);
	p->list_flags = EMPTYING_LIST_FLAG;

	sc->sourceentries++;
	if (sc->sourceentries > sc->max_sourceentries)
		sc->max_sourceentries = sc->sourceentries;

#ifdef GLOBAL_STATS
	nr_sources++;
	if (nr_sources > max_sources)
		max_sources = nr_sources;
#endif
}

/**
 * remove page from emptying list of size-class
 * @param sc size-class
 * @param p page
 */
static void remove_page_from_emptying_list(struct size_class *sc, struct page *p){
	assert(p->list_flags == EMPTYING_LIST_FLAG);
	remove_page_from_list(sc, p);
	//list_del(&p->list);
	p->list_flags = 0;

	sc->sourceentries--;
#ifdef GLOBAL_STATS
	nr_sources--;
#endif
}

/**
 * get a source page of size-class; caller holds size-class lock
 * @param sc size-class
 * @return source page
 */
static struct page *get_source_page(struct size_class *sc)
{
	struct page *nf_page;

	if (!list_empty(&sc->head_source_pages)) {
		nf_page = list_entry((sc->head_source_pages).next, struct page, list);
		if(--nf_page->nr_used_page_blocks == 0){
			remove_page_from_source_list(sc, nf_page);
			add_page_to_emptying_list(sc, nf_page);
		}
	} else {
		nf_page = list_entry((sc->head_nfull_pages).next, struct page, list);
		remove_page_from_nfull_list(sc, nf_page);
		if(--nf_page->nr_used_page_blocks == 0){
			add_page_to_emptying_list(sc, nf_page);
		} else {
			add_page_to_source_list(sc, nf_page);
		}
	}
	nf_page->sourceentries++;
	
	return nf_page;

}

/////////////////////////////////////////////////////////////////////
// Threads
/////////////////////////////////////////////////////////////////////

static void init_thread(struct thread_data *data){
	int id;	
	id = atomic_inc_return(&next_thread_id);
	data->id = id;
	//printf("new id %d\n", id);

	data->abuckets_count = 0;
	data->abuckets = NULL;
	data->last_abucket = NULL;
	data->free_abuckets_count = 0;
	data->free_abuckets = NULL;
	data->pbuckets_count = 0;
	data->pbuckets = NULL;
	data->last_pbucket = NULL;
	data->pages_count = 0;
	data->pages = NULL;
	data->aas_count = 0;
	data->aas = 0;

	if (private_classes) {
		data->size_classes = (struct size_class *)(data + 1);
		init_size_class_instance(data->size_classes);
	} else {
		data->size_classes = (struct size_class *)global_classes;
	}

}

static void *alloc_pages(int num_pages)
{
	void *mem;
	mem = mmap(NULL, num_pages*MACHINE_PAGESIZE, 
			PROT_READ | PROT_WRITE, 
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (!mem) {
		perror("alloc_page mmap");
		exit(2);
	}
	return mem;
}

static void thread_local_destructor(void *mem)
{
#if 0
	if (private_classes)
		munmap(mem, THREAD_LOCAL_PAGES*MACHINE_PAGESIZE);
	else
		munmap(mem, MACHINE_PAGESIZE);
#endif
}

static void init_cf_key()
{
	if (pthread_key_create(&cf_key, thread_local_destructor)) {
		perror("init_cf_key pthread_key_create");
		exit(4);
	}
}


/** allocate thread local storage, assign it to 
 * the thread and return a pointer to that memory
 * @return thread memory
 */ 
static void *assign_thread_specific()
{
	void *mem;

	if (private_classes) {
		mem = alloc_pages(THREAD_LOCAL_PAGES);
	} else {
		mem = alloc_pages(1);
	}

	if ((errno = pthread_setspecific(cf_key, mem))){
		perror("pthread_setspecific");
		exit(5);
	}


	return mem;
}

/**
 * get thread local data
 * @return thread local data
 */
static struct thread_data *get_thread_data()
{
	struct thread_data *data;

	data = (struct thread_data *)pthread_getspecific(cf_key);
	if(!data) {
		data = (struct thread_data *)assign_thread_specific();
		init_thread(data);
	}
	return data;
}


/////////////////////////////////////////////////////////////////////
// Compact-fit
/////////////////////////////////////////////////////////////////////

/**
 * sets abstract address field in page-block
 * @param p page
 * @param index index of page-block in page
 * @param page_block_size size of page-block
 * @param abstract_address used abstract address
 */
static void inline set_abstract_address(struct page *p, int index, int page_block_size, void **abstract_address){
	long **field;
	field = (long **)(p->memory + ((index+1)*page_block_size-sizeof(long)));
	*field = (long *)abstract_address;
}

/**
 * get reference to abstract address of page-block
 * @param p page
 * @param page_block_index index of page-block
 * @param page_block_size size of page-block
 * @return reference to abstract address
 */
static inline long *get_abstract_address_of_page_block(struct page *p, int page_block_index, int page_block_size){
	return *(long **)((char *)p->memory + (page_block_index + 1)*page_block_size - sizeof(long));

}

/**
 * set partition field in page-block
 * @param p page
 * @param index index of page-block in page
 * @param page_block_size size of page-block
 * @param value partition field entry
 */
static void inline set_partition_field(struct page *p, int index, int page_block_size, void *value){
	long **field;
	field = (long **)(p->memory + ((index+1)*page_block_size-2*sizeof(long)));
	*field = value;
}

/**
 * get partition field in page-block
 * @param p page
 * @param page_block_index index of page-block
 * @param page_block_size size of page-block
 */
static inline void **get_partition_field_of_page_block(struct page *p, int page_block_index, int page_block_size){
	return (void **)(p->memory + ((page_block_index + 1)*page_block_size - 2*sizeof(long)));
}

/**
 * checks whether compaction is turned on
 * @param sc size-class
 * @return 1 if compaction is on, otherwise 0 
 */
static inline int compaction_on(struct size_class *sc){
	return sc->partial_compaction_bound != -1;
}

/**
 * checks wether a size-class is in full state
 * @param sc size-class
 * @return 1 if size-class is in full state, 0 otherwise
 */
static inline int in_full_state(struct size_class *sc){
	return (sc->nfullentries == 0 && list_empty(&sc->head_source_pages));
}

/**
 * perform a free operation in source state
 * @param sc size-class
 * @param p page
 */
static inline void do_free_source_state(struct size_class *sc, struct page *p){
	if(p->sourceentries == 0) {
		if(p->nr_used_page_blocks == 0) {
			remove_page_from_emptying_list(sc, p);
			add_free_page(p);
		} else {
			remove_page_from_source_list(sc, p);
			add_page_to_nfull_list(sc, p);
		}
	}
}

static inline void measure_compaction(struct bench_stats *stats, uint64_t compact_time, int size_class_index){
	stats->num_compaction++;
	stats->num_compact_class[size_class_index]++;
	stats->compaction_time[size_class_index] += compact_time;
	if (stats->max_compaction_time[size_class_index] < compact_time)
		stats->max_compaction_time[size_class_index] = compact_time;
}

/**
 * compaction operation;
 * sc locked on entry
 * target_page locked on entry
 * @param sc size-class
 * @param target_page target page
 * @param target_index page-block index in target page
 * @param src_page source page
 * @param size size of page-block
 * @return if compaction was successfull 0, otherwise abort flag
 */
static int do_compaction(struct size_class *sc,	struct page *target_page, int target_index, 
				struct page *src_page, int size) {
	int src_index;
	char *dest, *src;
	int cpy_size;
	int j;
	int rem_size = size;
	int aborted = 0;
	void **src_part_field;
	void **target_part_field;
	long *target_aa;
	int abort_compact = 0;
	int increments = 0;
	long *src_aa;

	assert(memcpy_multiplicator > 0);

#ifdef USE_STATS
	uint64_t compact_start = get_utime();
	struct bench_stats *stats = get_bench_stats();
#endif
	lock_page(target_page);
	lock_page(src_page);

	src_index = find_used_page_block(src_page);
	page_clear_used_bit(src_page, src_index);

	/* page is now either in source list or emptying list; concurrent allocs won't see the page */
	src_part_field = get_partition_field_of_page_block(src_page, src_index, size);
	src = src_page->memory + src_index*size;

	if (incremental_memcpy && size > incremental_memcpy && *src_part_field) { 
		/* my source is a target block of a concurrent compaction operation */
		/* get new src: src of my src block 
		 * new src is already in the correct lists and marked as source block
		 */
		long *aa = *((long **)src_part_field + 1);
		*aa |= ABORT_COMPACT_FLAG; /* abort other compaction */

		//printf("abort other compaction on block %p page %p aa %p %p\n", src, src_page, aa, (long *)*aa);
		assert(*(long **)aa > (long *)pages);

		src = *src_part_field; /* get new source */

		src_page->sourceentries--; 
		do_free_source_state(sc, src_page);

		*src_part_field = 0;

		abort_compact = 1;
		src_page = get_page_direct_of_page_block(src);

		assert(src_page->sourceentries > 0);
		assert(src_page->list_flags == SOURCE_LIST_FLAG || src_page->list_flags == EMPTYING_LIST_FLAG);

		src_index = ((char *)src - (char *)src_page)/size;
		/* get new src part field */
		src_part_field = get_partition_field_of_page_block(src_page, src_index, size);
	} 

	dest = target_page->memory + target_index*size;

	src_aa = get_abstract_address_of_page_block(src_page, src_index, size);	
	assert(src_aa);

	*src_aa |= ABORT_COMPACT_FLAG;

	target_part_field = get_partition_field_of_page_block(target_page, target_index, size);
	target_aa = get_abstract_address_of_page_block(target_page, target_index, size);

	assert(target_aa);
	long target_aa_val = *target_aa;

#ifdef LOCK_PAGE
	*src_aa = target_aa_val;
	unlock_sizeClass(sc);
#endif
	for(j=0; j<memcpy_multiplicator; ++j) {
		aborted = 0;
		while (rem_size > 0 && !aborted) {
			if (incremental_memcpy) {
				cpy_size = rem_size < incremental_memcpy? rem_size:incremental_memcpy;
			} else {
				cpy_size = rem_size;
			}
			rem_size -= cpy_size;
			/* copy pb include last word that points to its abstract address */
			memcpy(dest, src, cpy_size);

			dest += cpy_size;
			src += cpy_size;
			if (rem_size > 0) { //sc->workers > 1) { TODO: check this
				*target_part_field = src_page->memory + src_index * size;
				*src_part_field = target_page->memory + target_index * size;
#ifdef USE_STATS
				measure_compaction(stats, get_utime() - compact_start, get_size_class_index(size));//TODO: cleanup
#endif
				unlock_sizeClass(sc);

				increments++;
				lock_sizeClass(sc);
#ifdef USE_STATS
				compact_start = get_utime();
#endif
				/* check if we got aborted; we should continue where we left off,
				 * else another thread touched this block */
				if (*target_aa != target_aa_val)
					aborted = 1; /* abort since my target is a src of another deallocation */
			}
		}
	}


#ifdef USE_STATS
	measure_compaction(stats, get_utime() - compact_start, get_size_class_index(size));//TODO: cleanup
#endif

	if (!aborted) {
		*src_aa = target_aa_val;
		unlock_page(target_page);
		unlock_page(src_page);
#ifdef LOCK_PAGE
		lock_sizeClass(sc);
#endif
		if (incremental_memcpy && size > incremental_memcpy) {
			*target_part_field = 0;
		}
		--src_page->sourceentries;
		do_free_source_state(sc, src_page);
		return 0;
	} else {
		/* my target is a source of another compaction */
		if (*target_aa & ABORT_COMPACT_FLAG) { 
			clear_abstract_address(target_aa);
			return ABORT_COMPACT_FLAG;
		}
		/* my source was freed by another thread */
		else if (*target_aa & ABORT_FREE_FLAG) { 
			/* cleanup src page block */
			/* do not touch target block in this case!! it is used by another compaction */
			clear_abstract_address(target_aa);
			return ABORT_FREE_FLAG;
		} else { /* report BUG */
			assert(0);
			return 0;
		}
	}
}

/**
 * return partition field of a concurrent compaction operation
 * @param p page
 * @param page_block_index index of page-block
 * @param size page-block size
 * @return return partition field if incremental compaction is performed, otherwise NULL
 */
static inline long *check_for_concurrent_compaction(struct page *p, int page_block_index, int size)
{
	long *partition_field = NULL;
	if (incremental_memcpy && size > incremental_memcpy) {
		partition_field = (long *)(p->memory + (page_block_index + 1)*size - 2*sizeof(long));
	}
	return partition_field;
}

/**
 * perform free in a full size-class
 * @param sc size-class
 * @param p page
 * @param page_block_size size of page-block
 * @param page_block_index index of page-block
 */
static inline void do_free_full_state(struct size_class *sc, struct page *p, int page_block_size, int page_block_index){
	free_used_page_block(p, page_block_index);
	remove_page_from_full_list(sc, p);
	if(p->nr_used_page_blocks == 0){
		add_free_page(p);
	}else{
		add_page_to_nfull_list(sc, p);
	}
}

/**
 * perform free in a not full size-class
 * @param sc size-class
 * @param p page
 * @param page_block_size size of page-block
 * @param page_block_index index of page-block
 */
static inline void do_free_nfull_state(struct size_class *sc, struct page *p, int page_block_index){
	if(p->nr_used_page_blocks == 1){
		free_used_page_block(p, page_block_index);
		if(p->sourceentries == 0){
			remove_page_from_nfull_list(sc, p);
			add_free_page(p);
		}else{
			remove_page_from_source_list(sc, p);
			add_page_to_emptying_list(sc, p);
		}
	}else{
		free_used_page_block(p, page_block_index);
	}
}

/**
 * perform free
 * @param sc size-class
 * @param p page
 * @param page_block_size size of page-block
 * @param page_block_index index of page-block
 */
static inline void do_free(struct size_class *sc, struct page *p, int page_block_size, int page_block_index){
	if(is_page_full(p, page_block_size)){
		do_free_full_state(sc, p, page_block_size, page_block_index);
	}else{
		do_free_nfull_state(sc, p, page_block_index);
	}
}

/////////////////////////////////////////////////////////////////////
// Public functions
/////////////////////////////////////////////////////////////////////

void cf_init(unsigned long abstract_address_space_size, 
		unsigned long concrete_address_space_size, 
		int local_pages, int local_pbuckets, 
		int local_aas, int local_abuckets, int local_free_abuckets, 
		int k, int inc, int mult, int private){
	
	long alignment = 0;
	/*set globals*/
	nr_local_pages = local_pages;
	nr_local_pbuckets = local_pbuckets;
	nr_local_aas = local_aas;
	nr_local_abuckets = local_abuckets;
	nr_local_free_abuckets = local_free_abuckets;
	nr_aa_buckets = abstract_address_space_size/sizeof(unsigned long)/nr_local_aas;
	nr_aas = nr_aa_buckets*nr_local_aas;
	heap_size = concrete_address_space_size;
	partial_compaction_bound = k;
	private_classes = private;

#ifndef LOCK_PAGE
	incremental_memcpy = inc;
#else
	fprintf(stderr, "incremental memcpy for PAGE lock configuration not supported\n");
	incremental_memcpy = 0;
#endif
	memcpy_multiplicator = mult;

	assert(sizeof(struct page) == PAGESIZE);
	assert(nr_local_pages > 0);
	assert(nr_local_pbuckets > 0);
	assert(nr_local_aas > 0);
	assert(nr_local_abuckets > 0);
	assert(nr_local_free_abuckets > 0);
	assert(nr_aa_buckets > 0);
	assert(nr_aas > 0);
	assert(heap_size > 0);
	assert(partial_compaction_bound > 0);

	/*init abstract address space*/
	aa_space = mmap(NULL, abstract_address_space_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(aa_space == MAP_FAILED) {
		perror("mmap");
		exit(3);
	}

	//*buckets allocated 2 times*/
	aa_buckets = mmap(NULL, nr_aa_buckets*sizeof(struct mem_aa)*2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(aa_buckets == MAP_FAILED){
		perror("mmap");
		exit(3);
	}
	init_free_aa_list();

	pages = mmap(NULL, heap_size+PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(pages == MAP_FAILED) {
		perror("mmap");
		exit(3);
	}
	if((unsigned long)pages & (PAGESIZE - 1)){
		void *tmp =  (void *)(((unsigned long)pages) & (0xffffffff^(PAGESIZE - 1)));
		alignment = PAGESIZE + (long)tmp - (long)pages; 
		pages = (void *)(((unsigned long)pages) & (0xffffffff^(PAGESIZE - 1)));
		pages = (struct page *)((char *)pages + PAGESIZE);
	}
	nr_pages = (heap_size-alignment)/PAGESIZE/nr_local_pages;
	nr_pages *= nr_local_pages;
	init_free_pages_list();

	init_cf_key();

	init_size_classes_mapping();

	init_size_class_instance((struct size_class *)global_classes);
	//printf("local pages: %d; local aas: %d\n", local_pages, local_aas);
}


void **cf_malloc(size_t size){
	struct size_class *sc;
	int page_block_size;
	struct page *p;
	int index;
	void **address = (void **)0x123;

#ifdef USE_STATS
	uint64_t start_time;
	start_time = get_utime();
#endif

	/*add size of explicit reference*/
	size += sizeof(uint32_t);

	/*incremental CF needs extra word*/
	if (incremental_memcpy && size > incremental_memcpy){
	    size += sizeof(uint32_t);
	}

	sc = get_size_class(size);
	page_block_size = get_page_block_size_of_size_class(sc);

	lock();
	lock_sizeClass(sc);

	/*first page in size-class or size class is full => beginn new page*/
	if(list_empty(&sc->head_nfull_pages)){
		p = get_free_page();

		/*no page available*/
		if(p==NULL){
			unlock_sizeClass(sc);
			unlock();
			errno = ENOMEM;
			fprintf(stderr, "no more pages\n");
			cf_print_free_pages();
			return NULL;
		}
	
		/*init page and add to not-full pages list of size-class*/
		p->nr_used_page_blocks = 0;
		p->sc = sc;
		add_page_to_nfull_list(sc, p);
	}
	/*there is a not-full page*/
	else{
		p = get_nfull_pages_tail(sc);
	}

	index = get_free_page_block(p);

	/*is this a full page now?*/
	if(is_page_full(p, page_block_size)){
		remove_page_from_nfull_list(sc, p);
		add_page_to_full_list(sc, p);
	}

	/*if compaction turned of, return page-block address*/
	if (!compaction_on(sc)) {
		address = (void **)&p->memory[index*page_block_size];
	}
	/*create an abstract address*/
	else {
		address = get_abstract_address(&p->memory[index*page_block_size]);
		if(address == NULL) {
			fprintf(stderr, "no more aa\n");
			errno = ENOMEM;
		} else {
			set_abstract_address(p, index, page_block_size, address);
			/*incremental memcpy needs partition field*/
			if(incremental_memcpy && page_block_size > incremental_memcpy) { 
				set_partition_field(p, index, page_block_size, NULL);
			}
		}
	}

	unlock_sizeClass(sc);
	unlock();

#ifdef USE_STATS
	struct bench_stats *stats = get_bench_stats();
	int size_class_index = get_size_class_index(size);//TODO: CLEANUP
	stats->class_counter[size_class_index]++;
	stats->num_alloc++;
	stats->bytes_allocated_brutto += page_block_size;
	start_time = get_utime() - start_time;
	stats->malloc_time[size_class_index] += start_time;
	if (stats->malloc_max_time[size_class_index] < start_time)
		stats->malloc_max_time[size_class_index] = start_time;
	stats->num_malloc_class[size_class_index]++;
#endif

	return address;
}

void cf_free(void **address) {
	void *page_block;
	struct thread_data *thread_data;
	struct page *target_page;
	struct size_class *sc;
	int page_block_size;
	int compact_abort = 0;
	int page_block_index;
	long *partition_field;
	struct page *src_page;
	char *target_block;
	int target_index;
	long *target_aa;

#ifdef USE_STATS
	uint64_t start_time;
	start_time = get_utime();
#endif

	lock();

	thread_data = get_thread_data();

	/*no compaction*/
	if (!compaction_on(thread_data->size_classes)){
		/*no abstract address*/
		page_block = address;
 
		target_page = (struct page *)get_page_direct_of_page_block(page_block);
		sc = get_size_class_of_page(target_page);

		lock_sizeClass(sc);
	}
	/*try to get size-class lock
	IMPORTANT: try-and-error strategy*/
	else {
retry:
		/*dereference the abstract address*/
		page_block = *address;

		target_page = (struct page *)get_page_direct_of_page_block(page_block);
		sc = get_size_class_of_page(target_page);

		/*take lock optimistically*/
		lock_sizeClass(sc);

		/*dereference the abstract address*/
		page_block = *address;

		target_page = (struct page *)get_page_direct_of_page_block(page_block);

		/*synchronize over size-class, if same size-class then we are fine*/
		if (sc != get_size_class_of_page(target_page)) {
			/* we got the wrong lock! --> backtrack */
			unlock_sizeClass(sc);
			goto retry;
		}

	}

	page_block_size = get_page_block_size_of_size_class(sc);
	page_block_index = get_index_direct_of_page_block(target_page, page_block, page_block_size);

	/*no compaction*/
	if(!compaction_on(sc)){
		do_free(sc, target_page, page_block_size, page_block_index);
	}
	else if (in_full_state(sc)){
		partition_field = check_for_concurrent_compaction(target_page, page_block_index, page_block_size);
		/* page-block is being used by another compaction operation -> abort other compaction operation
		 * the page-block is freed by the thread doing the other compaction operation*/
		if (partition_field && *partition_field) {
			assert(target_page->list_flags == EMPTYING_LIST_FLAG);

			src_page = target_page;

			target_block = (char *)*partition_field;
			target_aa = *(long **)(target_block + (page_block_size - sizeof(long)));
			*target_aa |= ABORT_FREE_FLAG;

			target_page = get_page_direct_of_page_block(target_block);
			target_index = ((char *)target_block - (char *)target_page)/page_block_size;

			do_free_full_state(sc, target_page, page_block_size, target_index);

			--src_page->sourceentries;
			if(src_page->sourceentries == 0) {
				remove_page_from_emptying_list(sc, src_page);
				add_free_page(src_page);
			}
		} else {
			do_free_full_state(sc, target_page, page_block_size, page_block_index);
		}

	} else {
		partition_field = check_for_concurrent_compaction(target_page, page_block_index, page_block_size);
		if (partition_field && *partition_field) {
			src_page = target_page;

			target_block = (char *)*partition_field;
			target_aa = *(long **)(target_block + (page_block_size - sizeof(long)));
			*target_aa |= ABORT_FREE_FLAG;

			target_page = get_page_direct_of_page_block(target_block);
			target_index = ((char *)target_block - (char *)target_page)/page_block_size;

			if(valid_nfull_pages_ratio(sc, 1)) { /* n < k  partial compaction support  */
				do_free(sc, target_page, page_block_size, target_index);
			} else if (is_page_full(target_page, page_block_size)) {
				struct page *nf_page = get_source_page(sc);

				*(long *)(target_block + (page_block_size - sizeof(long))) = (long)address;
				*address = target_block;

				compact_abort = do_compaction(sc, target_page, target_index, nf_page, page_block_size);

			} else {
				do_free_nfull_state(sc, target_page, target_index);
			}

			--src_page->sourceentries;
			do_free_source_state(sc, src_page);
		} else if(!compaction_on(sc) || valid_nfull_pages_ratio(sc, 1)) { 
			do_free(sc, target_page, page_block_size, page_block_index);
		} else if (is_page_full(target_page, page_block_size)) { /* n == k && page_full --> compaction */
			struct page *nf_page = get_source_page(sc);
			compact_abort = do_compaction(sc, target_page, page_block_index, nf_page, page_block_size);

		} else { /* n == k && page not full */
			do_free_nfull_state(sc, target_page, page_block_index);
		}
	}

	if (sc->partial_compaction_bound >= 0 && !compact_abort)
		clear_abstract_address(address);

#ifdef USE_STATS
	struct bench_stats *stats = get_bench_stats();
	stats->num_free++;
	uint64_t end = get_utime();
	stats->free_time[get_size_class_index(page_block_size)] += end - start_time;
	if (stats->free_max_time[get_size_class_index(page_block_size)] < end - start_time)
	    stats->free_max_time[get_size_class_index(page_block_size)] = end -start_time;

	stats->num_free_class[get_size_class_index(page_block_size)]++;
#endif
	unlock_sizeClass(sc);
	unlock();

}

void *cf_dereference(void **address, int index)
{
	if(partial_compaction_bound == -1) {
		return address + index;
	} else {
		return *address + index;
	}
}

/**
 * prints the page free-list (debugging)
 */
void print_pages_free_list(){
	struct mem_page *p;
	page_print_head(&free_page_head);

	printf("Free List: ");
	p = page_peek(&free_page_head);
	while(p!=NULL) {
		printf("%p ", p);
		p = p->next;
	}
}

void cf_print_memory_information(){
/*	printf("\nMemory Information:\n");
	printf("memory size: %d\n", memory_size);
	printf("pages amount: %d\n", nr_pages);
	printf("proxy entriest: %d\n", nr_max_page_blocks);
	printf("\n");*/
}	

void cf_print_free_pages(){
	printf("Free Pages List\n");
	lock();
	print_pages_free_list();
	unlock();
}

void cf_print_aa_space(){
    	lock();
	print_aa_space();
	unlock();
}

static void print_pages_status(){
	/*if (!private_classes) {
		int flag = 0;
		struct size_class *size_classes = (struct size_class *)global_classes;

		printf("\nPages Status: \n");
		uint32_t i;
		for(i=0; i<SIZECLASSES; i++){
			if(size_classes[i].head_full_pages == NULL){
				//printf("size class %d full pages: empty\n", i);
			} else {
				struct page *p;
				p = size_classes[i].head_full_pages;

				uint32_t j;
				j=1;
				printf("size-class %d full pages: ", i);
				do{
					if (p->nr_used_page_blocks)
						printf("%d: %d entries; ", j, p->nr_used_page_blocks);
					j++;
					p = p->next;
				}while(p!=size_classes[i].head_full_pages);

				flag = 1;
				printf("\n");
			}
			if(size_classes[i].head_nfull_pages == NULL){
				//printf("size class %d not full pages: empty\n", i);
			} else {
				struct page *p;
				p = size_classes[i].head_nfull_pages;

				uint32_t j;
				j=1;
				printf("size-class %d not full pages %d: ", 
						i, size_classes[i].nfullentries);
				do{
					if (p->nr_used_page_blocks)
						printf("%d: %d entries; ", j, p->nr_used_page_blocks);
					j++;
					p = p->next;
				} while(p!=size_classes[i].head_nfull_pages);

				flag =1;
				printf("\n");
			}
		}
		if(!flag){
			printf("all size-classes are empty\n\n");
		}
	}*/
}

void cf_print_pages_count(){
	printf("max pages count: %d\n", max_pages_count);
	printf("pages count: %d\n", pages_count);
}

void cf_print_sc_fragmentation(){
		/*int sc_frag_sum = 0;
		struct size_class *size_classes = get_thread_data()->size_classes;
		uint32_t i;
		int sum_max_nr_pages = 0;
		int sum_max_nfull_pages = 0;
		int sum_max_source_pages = 0;
		for(i=0; i<SIZECLASSES; i++){
			int sc_frag = 0;
			int max_size = get_page_block_size_of_size_class(size_classes + i);

			sum_max_nr_pages += size_classes[i].max_nr_pages;
			sum_max_nfull_pages += size_classes[i].max_nfullentries;
			sum_max_source_pages += size_classes[i].max_sourceentries;
			if(size_classes[i].head_nfull_pages == NULL){
				//printf("size class %d not full pages: empty\n", i);
			} else {
				struct page *p;
				p = size_classes[i].head_nfull_pages;

				uint32_t j;
				j=1;

				do{
					sc_frag += PAGEDATASIZE/max_size - p->nr_used_page_blocks;
					p = p->next;
				}while(p!=size_classes[i].head_nfull_pages);
				printf("Size-class fragmentation sc %d (pb: %d, #pb: %d): %d free pb, %d B\n", i, max_size, PAGEDATASIZE/max_size, sc_frag, sc_frag*max_size); 
				sc_frag_sum += sc_frag*max_size;
			}
		}
#ifdef GLOBAL_STATS
		fprintf(stderr, "Total size-class fragmentation: %d B\n"
			   	"max used pages %ld, max nfull pages %ld, max source pages %ld\n", 
				sc_frag_sum, max_used_pages, max_nfull, max_sources);
#else
		fprintf(stderr, "Total size-class fragmentation: %d B\n"
			   	"max used pages %d\n", 
				sc_frag_sum, sum_max_nr_pages);

#endif*/
}

void cf_print_pages_status(){
	page_print_head(&free_page_head);
	lock();
	print_pages_status();
	unlock();
}

#ifdef TESTING
#include "cf-unit-test.c"
#endif
