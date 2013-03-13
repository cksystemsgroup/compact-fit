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
 
/*! \file cf.h
    \brief A compacting real-time memory management
    
    Compaction is done in incremental steps (event-triggerd on memory deallocation)
*/
 
#ifndef CF_H_
#define Cf_H_

#include <stdint.h>
#include <stddef.h>

/**
 * number of size classes
 * (page size 16384)
 * n >= SIZECLASSESFACTOR log 256
 */
#define SIZECLASSES 51
/**
 * min size of a page-block
 * has to be >= 16B
 */
#define MINPAGEBLOCKSIZE 16 

/**
 * proposed by Berger et. al - size classes of increasing size
 */
#define SIZECLASSESFACTOR 1.125


/**
 * initialize the memory
 * @param size size of the memory
 * @param memory memory start pointer
 */
void cf_init(unsigned long abstract_address_space_size, unsigned long concrete_address_space_size, 
		int local_pages, int local_pbuckets, 
		int local_aas, int local_abuckets, int local_free_abuckets, 
		int partial_compaction_bound, int memcpy_increment, int memcpy_mult, int private);

/**
 * allocates memory of a given size
 * @param size amount of bytes to allocate
 * @return abstract address
 */
void **cf_malloc(size_t size);

/**
 * returns a word reference
 * @param address abstract address
 * @param index object offset
 * @return reference to physical memory
 */
void *cf_dereference(void **address, int index);

/**
 * frees a object
 * @param address proxy address
 */
void cf_free(void **address);

/**
 * print memory information (debugging)
 */
void cf_print_memory_information();

/**
 * print free pages (debugging)
 */
void cf_print_free_pages();

/**
 * print pages status (debugging)
 */
void cf_print_pages_status();

void cf_print_sc_fragmentation();

void cf_print_sc_fragmentation();
/**
 * print abstract address table (debugging)
 */
void cf_print_aa_space();

void cf_set_memcpy_multiplicator(int n);

void cf_print_pages_count(void);

#endif /*CF_H_*/

