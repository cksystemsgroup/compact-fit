#ifndef PAGE_STACK
#define PAGE_STACK

#include "arch_dep.h"
#include "nb_stack.h"

struct mem_page {
	struct mem_page *next;
	struct mem_page *local_pages;
};

extern void page_put(struct mem_page *p, struct mem_page *last, nb_stack *stack);
extern struct mem_page *page_get(nb_stack *stack);
extern void page_print_head(nb_stack *stack);
extern struct mem_page *page_peek(nb_stack *stack);

#endif
