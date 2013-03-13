#ifndef NB_STACK
#define NB_STACK

#include "arch_dep.h"

struct nb_stack_elem{
	struct nb_stack_elem *next;
};

#if ARCH_HAS_CAS64
typedef uint64_t nb_stack;	/*higher bits are version number, lower bits are memory address*/
#define NB_STACK_INITIALIZER 0

#else /* ARCH_HAS_CAS64 */

#include <pthread.h>
typedef struct nb_stack {
	struct mem_page *head;
	pthread_mutex_t lock;
} nb_stack;
#define NB_STACK_INITIALIZER { .head = NULL, .lock = PTHREAD_MUTEX_INITIALIZER}
#endif

extern struct nb_stack_elem *nb_stack_get(nb_stack *stack, int bits, int mask, int shift);
extern void nb_stack_put(struct nb_stack_elem *p, struct nb_stack_elem *last, nb_stack *stack, int bits, int mask, int shift);
extern struct nb_stack_elem *nb_stack_peek(nb_stack *stack, int mask, int shift);
extern void nb_stack_print_head(nb_stack *stack, int bits, int mask, int shift);

#endif
