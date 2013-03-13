SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))
BINARIES = bench\
		   alloc_count\
		   memcpy_count\
		   none\
		   class\
		   global\
		   single_malloc_mult_free\
		   mdrv_test\
		   mdrv_tlsf\
		   mdrv_share

BENCH_LIB = bench.o ../nb_stack.o ../page_stack.o ../aa_stack.o ../aa_bucket_stack.o

CFLAGS = -O3 -DUSE_PRIVATE_ADDRESS -DUSE_STATS -Wall -I..
LDFLAGS = -lpthread

all: $(BINARIES)

class: random_alloc_count.o $(BENCH_LIB) ../cf_class.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_class.o

global: random_alloc_count.o $(BENCH_LIB) ../cf_global.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_global.o

page: random_alloc_count.o $(BENCH_LIB) ../cf_page.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_page.o

none: random_alloc_count.o $(BENCH_LIB) ../cf_none.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_none.o

single_malloc_mult_free: single_malloc_mult_free.o $(BENCH_LIB) ../cf_class.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_class.o

bench: $(BENCH_LIB) $(HEADERS)
	${CC} $(LDFLAGS) -o $@ $(BENCH_LIB) ../cf_none.o

alloc_count: alloc_count.o $(BENCH_LIB) $(HEADERS) ../cf_none.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_none.o

memcpy_count: memcpy_count.o $(BENCH_LIB) $(HEADERS)
	${CC} $(LDFLAGS) $(BENCH_LIB) -o $@ $< ../cf_none.o

mdrv_test: mdrv_test.o $(BENCH_LIB) $(HEADERS) ../cf_class.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_class.o

mdrv_share: mdrv_share.o $(BENCH_LIB) $(HEADERS) ../cf_class.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_class.o

tlsf.o: tlsf.c
	${CC} $(CFLAGS) -c -o $@ $< -DUSE_MMAP=1 -DUSE_SBRK=1

mdrv_tlsf: mdrv_tlsf.o $(BENCH_LIB) $(HEADERS) ../cf_none.o tlsf.o
	${CC} $(LDFLAGS)  -o $@ $< $(BENCH_LIB) ../cf_none.o tlsf.o

clean:
	@rm -f $(OBJECTS) $(BINARIES)

.PHONY: clean
