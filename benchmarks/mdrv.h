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
#ifndef __MDRV_H__
#define __MDRV_H__
	
typedef enum {
	TRACE_OP_MALLOC,
	TRACE_OP_REALLOC
}traceop_t;

struct stats {
	void		*top_of_heap;
	uint32_t	mem_alloced;
	uint32_t	my_memory;
	uint32_t	nops;
	uint32_t	nallocs;
	uint32_t	nreallocs;
	uint32_t	nfree;
	uint32_t	nrecords;
};

struct record {
	uint32_t		key;
	uint32_t		type;
	size_t			size;
	void			*ptr;
	RB_ENTRY(record)	link_rb;
};

static inline int 
cmpmrec(struct record *a, struct record *b)
{
	if(a->key < b->key)
	{
		return -1;
	}
	else if(a->key == b->key)
	{
		return 0;
	}
	return 1;
}

void set_input_file(char *filename);

RB_HEAD(rectree, record);
RB_PROTOTYPE_STATIC(rectree, record, link_rb, cmpmrec);
RB_GENERATE_STATIC(rectree, record, link_rb, cmpmrec);

#endif
