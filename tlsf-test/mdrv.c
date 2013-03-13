/*-
 * Copyright 2005 Aniruddha Bohra <bohra@cs.rutgers.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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

#include "mdrv.h"
#include "tlsf.h"

static int report_interval=5;

struct stats stats;
struct rectree rectree;

FILE	*fp;
FILE	*ofp;

void
report(void)
{
	fprintf(ofp, "%u %u %u %u %p %u %u %u\n",
			stats.nops, stats.nallocs, stats.nreallocs, 
			stats.nfree, stats.top_of_heap, stats.mem_alloced, 
			stats.nrecords, stats.my_memory);
}

struct record *
my_malloc(void)
{
	size_t		size;
	struct record	*prec;

	size = sizeof(struct record);
	
	prec = tlsf_malloc(size);
	if(prec == NULL)
	{
		return NULL;
	}
	memset(prec, 0, size);

	stats.my_memory += size;
	stats.nrecords++;

	return prec;
}

void
my_free(struct record *prec)
{
	if(prec->ptr != NULL)
	{
		err(1, "Trying to free record with valid pointer");
	}
	
	tlsf_free(prec);
	stats.my_memory -= sizeof(struct record);
	stats.nrecords--;
}

int
trace_alloc(uint32_t key, size_t size, traceop_t op)
{
	void 		*ptr;
	struct record	rec;
	struct record	*prec;

	assert(op == TRACE_OP_MALLOC || op == TRACE_OP_REALLOC);
	
	rec.key = key;
	prec = RB_FIND(rectree, &rectree, &rec);
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
		RB_REMOVE(rectree, &rectree, prec);
	}
	
	if(op == TRACE_OP_MALLOC)
	{
		ptr = tlsf_malloc(size);
		if(ptr == NULL)
		{
			warn("[Trace Malloc]: ");
			return -1;
		}
		stats.nallocs++;
	}
	else
	{
		ptr = tlsf_realloc(prec->ptr, size);
		if(ptr == NULL)
		{
			warn("[Trace Realloc]: ");
			return -1;
		}
		stats.nreallocs++;
	}
	
	stats.mem_alloced += (size - prec->size);
	stats.top_of_heap = sbrk(0);
	if((++stats.nops % report_interval) == 0)
	{
		report();
	}
	
	prec->key	= key;
	prec->ptr	= ptr;
	prec->size	= size;
	prec->ptr	= ptr;

	RB_INSERT(rectree, &rectree, prec);

	return 0;
}

int
trace_free(uint32_t key)
{
	struct record	rec;
	struct record	*prec;

	rec.key = key;

	prec = RB_FIND(rectree, &rectree, &rec);
	if(prec == NULL)
	{
		warnx("Unknown key %lu", (unsigned long)key);
		return -1;
	}

	RB_REMOVE(rectree, &rectree, prec);

	tlsf_free(prec->ptr);

	stats.mem_alloced -= prec->size;
	stats.top_of_heap = sbrk(0);
	stats.nfree++;

	prec->ptr = NULL;
	my_free(prec);

	if((++stats.nops % report_interval) == 0)
	{
		report();
	}

	return 0;
}

int
handle_alloc(char *line)
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

	return trace_alloc(key, size, TRACE_OP_MALLOC);
}

int
handle_realloc(char *line)
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

	return trace_alloc(key, size, TRACE_OP_REALLOC);
}

int
handle_free(char *line)
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

	return trace_free(key);

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
handle_line(char *line)
{
	char	c;
	int	error;
	
	assert(line != NULL);
	
	error = 0;
	c = line[0];
	switch(c)
	{
		case 'a':
			error = handle_alloc(line);
			break;
		case 'f':
			error = handle_free(line);
			break;
		case 'r':
			error = handle_realloc(line);
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

static void
usage(void)
{
	fprintf(stderr, "Usage : mdrv [-i ops per report] [-f infile] ");
	fprintf(stderr, "[-o outfile]\n");
	exit(1);
}

#define POOL_SIZE 1024*1024*1024
char pool[POOL_SIZE];

int
main(int argc, char **argv)
{
	char	ch;
	char	line[80];
	char	filename[80];

	fp = NULL;
	ofp = NULL;

	printf("Start ...\n");	
	while ((ch = getopt(argc, argv, "f:hi:o:")) != -1)
	{
		switch (ch) 
		{
		case 'i':
			sscanf(optarg, "%d", &report_interval);
			break;
		case 'f':
			memset(filename, 0, sizeof(filename));
			strncpy(filename, optarg, sizeof(filename));
			fp = fopen(filename, "r");
			if(fp == NULL)
			{
				err(1, "Invalid filename %s", filename);
			}
			break;
		case 'o':
			memset(filename, 0, sizeof(filename));
			strncpy(filename, optarg, sizeof(filename));
			ofp = fopen(filename, "w+");
			if(ofp == NULL)
			{
				err(1, "Invalid filename %s", filename);
			}
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	
	argc -= optind;
	argv += optind;

	if(fp == NULL)
	{
		fp = stdin;
	}
	
	if(ofp == NULL)
	{
		ofp = stdout;
	}
	
	memset(&stats, 0, sizeof(stats));
	memset(line, 0, sizeof(line));		

	int free_mem;

	free_mem = init_memory_pool (POOL_SIZE, pool);
	printf("Total free memory= %d\n", free_mem);
	
	malloc_ex(100, pool);
	int cnt = 0;	
	while(fgets(line, sizeof(line), fp) != NULL)
	{
		if(handle_line(line) < 0)
		{
			return -1;
		}

		memset(line, 0, sizeof(line));
		if(!(cnt%100000)){
			printf("used size: %d\n", get_used_size(pool));
        		printf("max size: %d\n", get_max_size(pool));
		}
		cnt++;
		
	}

	printf("used size: %d\n", get_used_size(pool));
	printf("max size: %d\n", get_max_size(pool));

	destroy_memory_pool(pool);

	return 0;
}
