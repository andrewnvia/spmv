# Every program here is one .c file plus the headers next to it, so this is
# exactly the command line from the assignment page, once per program:
#     gcc -fopenmp -O3 -o spmv-omp-csr spmv-omp-csr.c
CC     = gcc
CFLAGS = -fopenmp -O3
LIBS   = -lm

PROGS   = spmv-csr spmv-ell spmv-omp-csr spmv-omp-ell
HEADERS = $(wildcard *.h)

all: $(PROGS)

%: %.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

.PHONY: all clean
clean:
	rm -f $(PROGS)
