# Program Assignment 1 starter code — OpenMP SpMV, CSR and ELL

The assignment itself — tasks, matrices, report items, grading and the due
date — is the course page:

<https://longing-duckling-38b.notion.site/Program-Assignment-1-OpenMP-SpMV-CSR-ELL-100-points-Due-Mon-Sep-21-3b6c5f0cb5bf81c89d20cbc3deb86e93>

This file describes only the code in this directory.

You edit **two files: `spmv-omp-csr.c` and `spmv-omp-ell.c`**, and those two
files are what you submit. Each is a complete program that reads the matrix,
runs the sequential baseline, checks the answer, times the kernel and
reports. The two kernels marked `YOUR CODE` at the top of each file are
yours; the rest is provided so that everyone's numbers are measured the same
way.

Everything else in this directory is provided and should not need editing.
Since only your two `.c` files are submitted, the graders compile them
against *their* copy of the headers, so changes to a header will not travel
with your submission.

## What is here

| file | what it is |
| --- | --- |
| `spmv-omp-csr.c` | **your file.** OpenMP SpMV over CSR — row-based and nonzero-based partitioning, schedule selectable at run time. |
| `spmv-omp-ell.c` | **your file.** The same over ELL. |
| `spmv-csr.c` | sequential CSR SpMV — the baseline every run is checked against. |
| `spmv-ell.c` | sequential ELL SpMV: converts CSR → ELL, multiplies, checks itself against CSR, prints the storage and padding cost. |
| `formats.h` | COO, CSR and ELL (row-major, `K = max_row_len` slots per row); `coo_to_csr()`, `csr_to_ell()`, the sequential `csr_spmv()` / `ell_spmv()` kernels, and the byte-counting helpers. |
| `input.h`, `mmio.h` | MatrixMarket reader (symmetric files are expanded to the full matrix on load). |
| `cmdline.h`, `timer.h`, `config.h`, `alloc.h` | small helpers. |
| `Makefile` | one `gcc` line per program, see below. |

Every program is one `.c` file plus the headers next to it, so the build is
exactly the command from the assignment page:

```sh
gcc -fopenmp -O3 -o spmv-omp-csr spmv-omp-csr.c -lm
gcc -fopenmp -O3 -o spmv-omp-ell spmv-omp-ell.c -lm
make            # builds all four programs the same way
```

`-lm` goes last, after the source file: the relative-L2 check calls `sqrt`,
and a `-l` placed before the file that needs it is dropped on Linux
distributions whose gcc defaults to `--as-needed`.

## Matrices

No matrices ship with the starter code. The download loop on the assignment
page fetches the four assigned ones into `matrices/`:

```sh
ls matrices/    # ecology2.mtx  atmosmodd.mtx  lp_wood1p.mtx  heart1.mtx
```

`lp_wood1p` is rectangular (244 × 2,595): `x` has `num_cols` entries and `y`
has `num_rows`. `ecology2` and `heart1` are stored symmetric, so the file
holds one triangle and the `nonzeros` the program prints is the expanded
count, roughly twice the number in the file's header line.

## Run

```sh
export OMP_NUM_THREADS=8
./spmv-omp-csr matrices/heart1.mtx
./spmv-omp-csr matrices/heart1.mtx --threads=8 --schedule=dynamic,64 --partition=nnz
./spmv-omp-ell matrices/heart1.mtx --threads=8 --schedule=guided
./spmv-csr     matrices/heart1.mtx          # sequential CSR
./spmv-ell     matrices/heart1.mtx          # sequential ELL
```

| flag | effect |
| --- | --- |
| `--threads=N` | threads for the timed loop (default `OMP_NUM_THREADS`, else every core) |
| `--schedule=S[,C]` | `static`, `dynamic`, `guided` with an optional chunk size, applied with `omp_set_schedule()` |
| `--partition=row\|nnz` | row-based or nonzero-based work partitioning (your two kernels) |
| `--iters=N` | fix the timed iteration count instead of letting the program pick one |

Every run prints `PASS` or `FAIL` against the sequential CSR baseline and
exits nonzero on `FAIL`. Timings are the average over the iterations shown
and exclude the file read and the CSR → ELL conversion. On ARC, run the
binaries under `srun -n 1 -c 8` as the assignment page says.

## Output

`./spmv-ell matrices/heart1.mtx`:

```
file=matrices/heart1.mtx rows=3557 cols=3557 nonzeros=1387773 max_row_len=1120
storage: COO 16.65 MB, CSR 11.12 MB, ELL 31.87 MB (ELL/CSR = 2.87)
padding: 2596067 of 3983840 slots are explicit zeros (65.2%)
conversion: 13.5370 ms
correctness vs CSR: PASS (relative L2 error 0.000e+00)
	Performing 689 iterations
	benchmarking ELL-SpMV:   2.7746 ms (  1.00 GFLOP/s  17.2 GB/s)
```

GFLOP/s counts the `2·nnz` useful flops; GB/s counts every slot moved,
padding included.

A small MatrixMarket file you can check by hand: one header line, then
`rows cols nnz`, then 1-indexed `row col value` triples.

```
%%MatrixMarket matrix coordinate real general
4 3 4
1 1 1.0
2 2 2.0
3 3 3.0
4 1 4.0
```
