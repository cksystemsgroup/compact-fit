#ifndef	_ARCH_DEP_H_
#define _ARCH_DEP_H_

#ifdef __i386__
#define LOCK_PREFIX "lock; "

static __inline__ unsigned long long rdtsc(void)
{
  unsigned long long int x;
     __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
     return x;
}
#define mb() __asm__ __volatile__("mfence")
#define rmb() __asm__ __volatile__("lfence")
#define wmb()	__asm__ __volatile__ ("": : :"memory")

static inline int _ffs(int x) {
        int r;

        __asm__("bsfl %1,%0\n\t"
                "jnz 1f\n\t"
                "movl $-1,%0\n"
                "1:" : "=r" (r) : "g" (x));
        return r;
}

static inline int _fls(int x) {
        int r;

        __asm__("bsrl %1,%0\n\t"
                "jnz 1f\n\t"
		"movl $-1,%0\n"
                "1:" : "=r" (r) : "g" (x));
        return r;
}

static inline int atomic_add_return(int i, int *val)
{
	int __i;
	/* Modern 486+ processor */
	__i = i;
	__asm__ __volatile__(
		"lock xaddl %0, %1"
		:"+r" (i), "+m" (*val)
		: : "memory");
	return i + __i;
}

#define atomic_inc_return(val) atomic_add_return(1, val)
/* the CAS code is copied from the linux kernel sources */

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 * 
 * Atomically increments @v by 1.
 */ 
static __inline__ void atomic_inc(int *v)
{
	__asm__ __volatile__(
		LOCK_PREFIX "incl %0"
		:"+m" (*v));
}

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 * 
 * Atomically decrements @v by 1.
 */ 
static __inline__ void atomic_dec(int *v)
{
	__asm__ __volatile__(
		LOCK_PREFIX "decl %0"
		:"+m" (*v));
}


struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))


static inline long long __cmpxchg64(uint64_t *ptr, uint64_t old, uint64_t new)
{
	unsigned long long prev;
	__asm__ __volatile__(LOCK_PREFIX "cmpxchg8b %3"
			     : "=A"(prev)
			     : "b"((unsigned long)new),
			       "c"((unsigned long)(new >> 32)),
			       "m"(*__xg(ptr)),
			       "0"(old)
			     : "memory");
	return prev;
}


#define cmpxchg64(ptr,o,n)\
	((__typeof__(*(ptr)))__cmpxchg64((ptr),(unsigned long long)(o),\
					(unsigned long long)(n)))

#define ARCH_HAS_CAS64 1
#define USE_RDTSC 1

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long prev;
	switch (size) {
	case 1:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgb %b1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 2:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgw %w1,%2"
				     : "=a"(prev)
				     : "r"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 4:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgl %1,%2"
				     : "=a"(prev)
				     : "r"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	}
	return old;
}
#define cmpxchg(ptr,o,n)\
	((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),\
					(unsigned long)(n),sizeof(*(ptr))))

#elif defined(__x86_64__)
static __inline__ unsigned long long rdtsc(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

#elif __arm__

/* 
   ARM's dependent code has been provided by 
   Adam Scislowicz <proteuskor@gmail.com> 
   Thank you very much Adam.
*/

#define _fls(x) ({ \
  int __r; \
  asm("clz\t%0, %1" : "=r"(__r) : "r"(x) : "cc"); \
  31-__r; \
})

#define _ffs(x) ({ unsigned long __t = (x); _fls(__t & -__t); })


/* FIXME: all this atomic stuff for armv5
 * the problem is that armv5 does not support atomic operations natively. arg ...
 */
static inline int atomic_add_return(int i, int *v)
{
	unsigned long tmp;
	int result;

#if 0
	__asm__ __volatile__("@ atomic_add_return\n"
"1:	ldrex	%0, [%2]\n"
"	add	%0, %0, %3\n"
"	strex	%1, %0, [%2]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp)
	: "r" (v), "Ir" (i)
	: "cc");
#else
	tmp = *v;
	result = ++tmp;
	*v = tmp;
#endif
	return result;
}
#define atomic_inc_return(val) atomic_add_return(1, val)

static inline int atomic_dec_return(int i, int *v)
{
	unsigned long tmp;
	int result;

#if 0
	__asm__ __volatile__("@ atomic_add_return\n"
"1:	ldrex	%0, [%2]\n"
"	add	%0, %0, %3\n"
"	strex	%1, %0, [%2]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp)
	: "r" (v), "Ir" (i)
	: "cc");
#else
	tmp = *v;
	result = tmp--;
	*v = tmp;
#endif
	return result;
}
#define atomic_dec_return(val) atomic_add_return(1, val)

static inline void atomic_dec(int *v)
{
	*v = *v - 1;
}

static inline void atomic_inc(int *v)
{
	*v = *v + 1;
}

static inline int atomic_cmpxchg(long *ptr, int old, int new)
{
	unsigned long oldval, res;

	do {
		__asm__ __volatile__("@ atomic_cmpxchg\n"
		"ldrex	%1, [%2]\n"
		"mov	%0, #0\n"
		"teq	%1, %3\n"
		"strexeq %0, %4, [%2]\n"
		    : "=&r" (res), "=&r" (oldval)
		    : "r" (ptr), "Ir" (old), "r" (new)
		    : "cc");
	} while (res);

	return oldval;
}

#define cmpxchg(ptr,o,n)\
	((__typeof__(*(ptr)))atomic_cmpxchg((ptr),(unsigned long)(o),\
					(unsigned long)(n)))

#undef ARCH_HAS_CAS64

#else

#warning Unsupported CPU architecture, using unoptimized bitmap handling

static inline int _ffs (int x) {
  int r = 0;

  if (!x)
    return -1;

  if (!(x & 0xffff)) {
    x >>= 16;
    r += 16;
  }

  if (!(x & 0xff)) {
    x >>= 8;
    r += 8;
  }

  if (!(x & 0xf)) {
    x >>= 4;
    r += 4;
  }

  if (!(x & 0x3)) {
    x >>= 2;
    r += 2;
  }

  if (!(x & 0x1)) {
    x >>= 1;
    r += 1;
  }

  return r;
}

static inline int _fls (int x) {
  int r = 31;

  if (!x)
    return -1;

  if (!(x & 0xffff0000)) {
    x <<= 16;
    r -= 16;
  }
  if (!(x & 0xff000000)) {
    x <<= 8;
    r -= 8;
  }
  if (!(x & 0xf0000000)) {
    x <<= 4;
    r -= 4;
  }
  if (!(x & 0xc0000000)) {
    x <<= 2;
    r -= 2;
  }
  if (!(x & 0x80000000)) {
    x <<= 1;
    r -= 1;
  }
  return r;
}
#endif
#if USE_RDTSC
#define get_utime rdtsc
#else
#include <sys/time.h>
#include <time.h>
static inline uint64_t get_utime()
{
	struct timeval tv;
	uint64_t usecs;

	gettimeofday(&tv, NULL);

	usecs = tv.tv_usec + tv.tv_sec*1000000;

	return usecs;
}
#endif
#endif 	    /* !_ARCH_DEP_H_ */
