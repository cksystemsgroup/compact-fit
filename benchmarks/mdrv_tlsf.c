
#include "bench.h"
#include "cf.h"
#include "arch_dep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
//#include <sys/tree.h>
#include "tree.h"
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <malloc.h>
#include "mdrv.h"
#include "tlsf.h"

static char *trace_file;
static int use_cf = 0;

void set_use_cf()
{
	use_cf = 1;
}

struct tlsf_stats {
	uint64_t max_free;
	uint64_t max_alloc;
	int cnt_alloc;
	int cnt_free;
	uint64_t sum_alloc;
	uint64_t sum_free;
};

static struct tlsf_stats tlsf_stats = { 0, 0, 0, 0 };

void do_free(void *p)
{
	uint64_t time = get_utime();
	
	if(use_cf)
		cf_free(p);
	else
		tlsf_free(p);

	time = get_utime() - time;
	tlsf_stats.sum_free += time;
	tlsf_stats.cnt_free++;
	if (tlsf_stats.cnt_free > 10 && time > tlsf_stats.max_free)
		tlsf_stats.max_free = time;
}

void *do_malloc(size_t size)
{
	void *p;
	uint64_t time = get_utime();

	if (use_cf)
		p = cf_malloc(size);
	else
		p = tlsf_malloc(size);

	time = get_utime() - time;
	tlsf_stats.sum_alloc += time;
	tlsf_stats.cnt_alloc++;
	if (tlsf_stats.cnt_alloc > 10 && time > tlsf_stats.max_alloc)
		tlsf_stats.max_alloc = time;

	return p;
}

void set_input_file(char *filename){
	trace_file = filename;
}

struct record *
my_malloc(void)
{
	size_t		size;
	struct record	*prec;

	size = sizeof(struct record);
	
	prec = malloc(size);
	if(prec == NULL)
	{
		return NULL;
	}
	memset(prec, 0, size);


	return prec;
}

void
my_free(struct record *prec)
{
	if(prec->ptr != NULL)
	{
		err(1, "Trying to free record with valid pointer");
	}
	
	free(prec);
}

int
trace_alloc(uint32_t key, size_t size, traceop_t op, struct rectree *rectree)
{
	void 		*ptr;
	struct record	rec;
	struct record	*prec;

	assert(op == TRACE_OP_MALLOC || op == TRACE_OP_REALLOC);
	
	rec.key = key;
	prec = RB_FIND(rectree, rectree, &rec);
	if(prec != NULL && op != TRACE_OP_REALLOC)
	{
		/* 
		 * In the trace, if we are not reallocing, we should not
		 * have a record with the same key.
		 * This means there is some error in the driver program.
		 */
		warnx("Allocating same key %u twice without free.", key);
		return -1;
	}
	if(prec == NULL)
	{
		prec = my_malloc();
		if(prec == NULL)
		{
			err(1, "[MREC alloc]: ");
		}
	}
	else
	{
		RB_REMOVE(rectree, rectree, prec);
	}
	
	if(op == TRACE_OP_MALLOC)
	{
		ptr = do_malloc(size);
		if(ptr == NULL)
		{
			warn("[Trace Malloc]: ");
			return -1;
		}
	}
	else
	{
		do_free(prec->ptr);
		ptr = do_malloc(size);
		if(ptr == NULL)
		{
			warn("[Trace Realloc]: ");
			return -1;
		}
	}
	
	prec->key	= key;
	prec->ptr	= ptr;
	prec->size	= size;
	prec->ptr	= ptr;

	RB_INSERT(rectree, rectree, prec);

	return 0;
}

int
trace_free(uint32_t key, struct rectree *rectree)
{
	struct record	rec;
	struct record	*prec;

	rec.key = key;

	prec = RB_FIND(rectree, rectree, &rec);
	if(prec == NULL)
	{
		warnx("Unknown key %lu", (unsigned long)key);
		return -1;
	}

	RB_REMOVE(rectree, rectree, prec);

	do_free(prec->ptr);


	prec->ptr = NULL;
	my_free(prec);

	return 0;
}

int
handle_alloc(char *line, struct rectree *rectree)
{
	char		*args[4];
	size_t		size;
	uint32_t	key;
	char		**ap;

	for(ap = args; (*ap = strsep(&line, " \t")) != NULL; )
	{
		if(**ap != '\0')
		{
			if(++ap >= &args[4])
				break;
		}
	}

	sscanf(args[1], "%x", &key);
	sscanf(args[2], "%zu", &size);

	return trace_alloc(key, size, TRACE_OP_MALLOC, rectree);
}

int
handle_realloc(char *line, struct rectree *rectree)
{
	char		*args[4];
	size_t		size;
	uint32_t	key;
	char		**ap;

	for(ap = args; (*ap = strsep(&line, " \t")) != NULL; )
	{
		if(**ap != '\0')
		{
			if(++ap >= &args[4])
				break;
		}
	}

	sscanf(args[1], "%x", &key);
	sscanf(args[2], "%zu", &size);

	return trace_alloc(key, size, TRACE_OP_REALLOC, rectree);
}

int
handle_free(char *line, struct rectree *rectree)
{
	int		i;
	char		*args[3];
	uint32_t	key;
	char		**ap;

	for(ap = args, i = 0; (*ap = strsep(&line, " \t")) != NULL; i++)
	{
		if(**ap != '\0')
		{
			if(++ap >= &args[3])
				break;
		}
	}

	sscanf(args[1], "%x", &key);

	return trace_free(key, rectree);

}

int
handle_label(char *line)
{
	/* 
	 * Handle labels, this would require annotating
	 * every record with a label type and recording
	 * per label stats.
	 *
	 * In any case emacs trace does not have label,
	 * so just return 0 here.
	 */

	return 0;
}

int
handle_line(char *line, struct rectree *rectree)
{
	char	c;
	int	error;
	
	assert(line != NULL);
	
	error = 0;
	c = line[0];
	switch(c)
	{
		case 'a':
			error = handle_alloc(line, rectree);
			break;
		case 'f':
			error = handle_free(line, rectree);
			break;
		case 'r':
			error = handle_realloc(line, rectree);
			break;
		case 'l':
			error = handle_label(line);
			break;
		case '#':
		default:
			break;
	}

	return error;
}


int bench_func(struct bench_stats *stats)
{
	char	line[80];
	FILE	*fp;

	fp = NULL;

	//fp = fopen("/home/hpayer/projects/malloc/hummingbird.malloc.4day", "r");
	//fp = fopen("emacs.new.malloc", "r");
	fp = fopen(trace_file, "r");
	if(fp == NULL)
	{
		err(1, "Invalid filename emacs.new.malloc");
	}

	memset(line, 0, sizeof(line));	

	int cnt = 0;	
	while(fgets(line, sizeof(line), fp) != NULL)
	{
		if(handle_line(line, &stats->rectree) < 0)
		{
			return -1;
		}

		memset(line, 0, sizeof(line));
		/*
		if(!(cnt%100000))		
			printf("%d\n",cnt);
			*/
		cnt++;	
	}
	printf("avg alloc %llu, max alloc %llu, avg free %llu, max free %llu\n", 
			tlsf_stats.sum_alloc/tlsf_stats.cnt_alloc, tlsf_stats.max_alloc, tlsf_stats.sum_free/tlsf_stats.cnt_free, tlsf_stats.max_free);
	return 0;
}

