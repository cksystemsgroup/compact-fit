#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#include "nb_stack.h"
#include "arch_dep.h"

#ifdef ARCH_HAS_CAS64

static inline struct nb_stack_elem *lifo_to_elem(nb_stack stack, int mask, int shift)
{
	return (struct nb_stack_elem *)(((uint32_t)stack & mask) << shift);
}

static inline uint64_t lifo_to_count(nb_stack stack, int bits)
{
	return stack >> bits;
}

static inline nb_stack pack(struct nb_stack_elem *p, uint64_t count, int bits, int shift)
{
	nb_stack stack;

	stack = (uint32_t)p >> shift;
	stack |= (count << bits);

	return stack;
}

void nb_stack_put(struct nb_stack_elem *p, struct nb_stack_elem *last, nb_stack *stack, int bits, int mask, int shift)
{
	nb_stack old, new;
	struct nb_stack_elem *first;
	uint64_t version;

	assert( ((uint32_t)p & ~(mask << shift)) == 0);
	do {
		old = *stack;
		first = lifo_to_elem(old, mask, shift);
		version = lifo_to_count(old, bits);

		++version;
		last->next = first;
		new = pack(p, version, bits, shift);
	} while (cmpxchg64(stack, old, new) != old);

}

struct nb_stack_elem *nb_stack_get(nb_stack *stack, int bits, int mask, int shift)
{
	struct nb_stack_elem *p;
	struct nb_stack_elem *next;
	nb_stack old, new;
	uint64_t version;

	do {
		old = *stack;
		p = lifo_to_elem(old, mask, shift);
		if(!p)
			break;
		version = lifo_to_count(old, bits);
		++version;
		next = p->next;
		new = pack(next, version, bits, shift);
	} while (cmpxchg64(stack, old, new) != old);

	return p;
}

void nb_stack_print_head(nb_stack *stack, int bits, int mask, int shift)
{
	uint64_t version;
	struct nb_stack_elem *e;

	version = lifo_to_count(*stack, bits);
	e = lifo_to_elem(*stack, mask, shift);
	printf("version %lld;\t elem %p\n", version, e);
}

struct nb_stack_elem *nb_stack_peek(nb_stack *stack, int mask, int shift)
{
	struct nb_stack_elem *e;
	e = lifo_to_elem(*stack, mask, shift);

	return e;
}
#else
#warning no double word compare-and-swap

void nb_stack_put(struct nb_stack_elem *p, nb_stack *stack, int bits, int mask, int shift)
{
	pthread_mutex_lock(&stack->lock);
	p->next = stack->head;
	stack->head = p;	
	pthread_mutex_unlock(&stack->lock);
}

struct nb_stack_elem *nb_stack_get(nb_stack *stack, int bits, int mask, int shift)
{
	struct nb_stack_elem *retval;
	pthread_mutex_lock(&stack->lock);
	retval = stack->head;
	if(retval)
		stack->head = retval->next;	
	pthread_mutex_unlock(&stack->lock);
	return retval;
}

void nb_stack_print_head(nb_stack *stack, int bits, int mask, int shift)
{
	printf("elem %p\n", stack->head);
}

struct nb_stack_elem *nb_stack_peek(nb_stack *stack, int mask, int shift)
{
	struct nb_stack_elem *e;

	pthread_mutex_lock(&stack->lock);
	e = stack->head;
	pthread_mutex_unlock(&stack->lock);
	return e;
}
#endif


#ifdef TESTING

#define PAGE_BITS 18 /* pages are 16k, low 14 bits are 0, and high 18 bits contain address */
#define PAGE_SHIFT (32-PAGE_BITS)
#define PAGE_MASK ((1<<PAGE_BITS) - 1)

static void print_head(nb_stack *stack)
{
	uint64_t version;
	struct nb_stack_elem *e;

	version = lifo_to_count(*stack, PAGE_BITS);
	e = lifo_to_elem(*stack, PAGE_MASK, PAGE_SHIFT);
	printf("version %lld;\t elem %p\n", version, e);
}

static nb_stack free_elem_head = 0;
static uint32_t wrong;
static uint32_t correct __attribute__((aligned(16384)));

int main()
{
	uint32_t *p;

	nb_stack_print_head(&free_elem_head, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);
	printf("put correct value %p\n", &correct);
	nb_stack_put((struct nb_stack_elem *)&correct, &free_elem_head, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);
	nb_stack_print_head(&free_elem_head, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);

	printf("head %lld\n", free_elem_head);
	p = (uint32_t *) nb_stack_get(&free_elem_head, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);
	printf("get correct %p\n", p);
	assert(p==&correct);

	nb_stack_put((struct nb_stack_elem *)&correct, &free_elem_head, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);
	nb_stack_print_head(&free_elem_head, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);
	printf("put wrong value %p\n", &wrong);
	nb_stack_put((struct nb_stack_elem *)&wrong, &free_elem_head, PAGE_BITS, PAGE_MASK, PAGE_SHIFT);

	return 0;
}
#endif
