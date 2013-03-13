#ifndef AA_BUCKET_STACK
#define AA_BUCKET_STACK

#include "arch_dep.h"
#include "nb_stack.h"

struct mem_aa_bucket {
	struct mem_aa_bucket *next;
	struct mem_aa_bucket *local_aa_bucket;
};

extern void aa_bucket_put(struct mem_aa_bucket *aa, struct mem_aa_bucket *aa_last, nb_stack *stack);
extern struct mem_aa_bucket *aa_bucket_get(nb_stack *stack);
extern void aa_bucket_print_head(nb_stack *stack);
extern struct mem_aa_bucket *aa_bucket_peek(nb_stack *stack);

#endif
