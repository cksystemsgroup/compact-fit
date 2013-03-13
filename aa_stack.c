#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#include "aa_stack.h"

#define AA_BITS 30 /* abstract addresses are one word long, low 2 bits are 0, and high 30 bits contain address */
#define AA_SHIFT (32-AA_BITS)
#define AA_MASK ((1<<AA_BITS) - 1)

void aa_put(struct mem_aa *aa, struct mem_aa *aa_last, nb_stack *stack)
{
	nb_stack_put((struct nb_stack_elem *)aa, (struct nb_stack_elem *)aa_last, stack, AA_BITS, AA_MASK, AA_SHIFT);
}

struct mem_aa *aa_get(nb_stack *stack){
	return (struct mem_aa *)nb_stack_get(stack, AA_BITS, AA_MASK, AA_SHIFT);
}

void aa_print_head(nb_stack *stack)
{
	nb_stack_print_head(stack, AA_BITS, AA_MASK, AA_SHIFT); 
}

struct mem_aa *aa_peek(nb_stack *stack)
{
	return (struct mem_aa *)nb_stack_peek(stack, AA_MASK, AA_SHIFT);
}

