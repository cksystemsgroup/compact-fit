
cpu = $(shell ./config.guess | awk 'BEGIN { FS = "-" } ; { print $$1 }' | sed 's,i[0-9],x,g')

HEADERS = cf.h arch_dep.h nb_stack.h nb_stack.o page_stack.h page_stack.o aa_stack.h aa_stack.o aa_bucket_stack.h aa_bucket_stack.o

CFLAGS = -Wall -O3 -DUSE_STATS -DUSE_PRIVATE_ADDRESS -I. -DNDEBUG
LDFLAGS = -m32 -L/usr/lib32/ nb_stack.o page_stack.o aa_stack.o aa_bucket_stack.o
BINARIES = 
#cf-test-global cf-test-none cf-test-class cf-test-page

all: $(BINARIES) global class page none
	make -C benchmarks

cf-unit: cf.c cf-unit-test.c
	$(CC) -Wall -O3 -DUSE_PRIVATE_ADDRESS -g -I. -lpthread $(LDFLAGS) -DTESTING -DLOCK_GLOBAL -DUSE_CAS -o $@ $<

global: cf_global.o
	make -C benchmarks $@

class: cf_class.o
	make -C benchmarks $@

page: cf_page.o
	make -C benchmarks $@

none: cf_none.o

cf-test-global: main.o cf_global.o
	$(CC) $(LDFLAGS) -o $@ $^

cf-test-none: main.o cf_none.o
	$(CC) $(LDFLAGS) -o $@ $^

cf-test-class: main.o cf_class.o
	$(CC) $(LDFLAGS) -o $@ $^

cf-test-page: main.o cf_page.o
	$(CC) $(LDFLAGS) -o $@ $^

cf_global.o: cf.c $(HEADERS)
	$(CC) $(CFLAGS) -DLOCK_GLOBAL -c -o $@ $<

cf_class.o: cf.c $(HEADERS)
	$(CC) $(CFLAGS) -DLOCK_CLASS -c -o $@ $<

cf_page.o: cf.c $(HEADERS)
	$(CC) $(CFLAGS) -DLOCK_PAGE -c -o $@ $<

cf_none.o: cf.c $(HEADERS)
	$(CC) $(CFLAGS) -DLOCK_NONE -c -o $@ $<

clean:
	@rm -f $(BINARIES) *.o
	@make -C benchmarks clean

.PHONY: clean

