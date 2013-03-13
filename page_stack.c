#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#include "page_stack.h"

#define PAGE_BITS 18 /* pages are 16k, low 14 bits are 0, and high 18 bits contain address */
#define PAGE_SHIFT (32-PAGE_BITS)
#define PAGE_MASK ((1<<PAGE_BITS) - 1)

void page_put(struct mem_page *p, struct mem_page *last, nb_stack *stack)
{
	nb_stack_put((struct nb_stack_elem *)p, (struct nb_stack_elem *)last, stack, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);
}

struct mem_page *page_get(nb_stack *stack){
	return (struct mem_page *)nb_stack_get(stack, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);
}

void page_print_head(nb_stack *stack)
{
	nb_stack_print_head(stack, PAGE_BITS, PAGE_MASK, PAGE_SHIFT); 
}

struct mem_page *page_peek(nb_stack *stack)
{
	return (struct mem_page *)nb_stack_peek(stack, PAGE_MASK, PAGE_SHIFT);
}

