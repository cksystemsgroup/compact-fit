#ifndef AA_STACK
#define AA_STACK

#include "arch_dep.h"
#include "nb_stack.h"

struct mem_aa {
	struct mem_aa *next;
	unsigned long local_aas;
};

extern void aa_put(struct mem_aa *aa, struct mem_aa *aa_last, nb_stack *stack);
extern struct mem_aa *aa_get(nb_stack *stack);
extern void aa_print_head(nb_stack *stack);
extern struct mem_aa *aa_peek(nb_stack *stack);

#endif
