/* spmv-omp-ell.c -- OpenMP SpMV over ELL.  YOUR FILE (Task 1).
 *
 * The ELL twin of spmv-omp-csr.c: same program, same flags, but the matrix
 * is converted to ELL once (csr_to_ell() in formats.h, provided) and the
 * timed kernel multiplies with that. The CSR copy is kept for the
 * correctness check and is passed to your kernels as well.
 *
 *   build:  gcc -fopenmp -O3 -o spmv-omp-ell spmv-omp-ell.c
 *   run:    ./spmv-omp-ell matrices/3elt_dual.mtx [--threads=8]
 *               [--schedule=static|dynamic[,chunk]|guided[,chunk]]
 *               [--partition=row|nnz] [--iters=N]
 *
 * Flags are as in spmv-omp-csr.c. Every run prints PASS or FAIL against the
 * sequential CSR baseline and exits nonzero on FAIL. Timings exclude the
 * file read and the CSR -> ELL conversion.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include "cmdline.h"
#include "input.h"
#include "config.h"
#include "timer.h"
#include "formats.h"
#include "alloc.h"

/* ====================================================================
 *                            YOUR CODE
 * ==================================================================== */

/* Row-based partitioning. This is ell_spmv() from formats.h: row i's K
 * slots are contiguous at i*K.  TODO (Task 1a): parallelize with OpenMP;
 * the schedule must follow --schedule without recompiling. */
static void spmv_ell_rows(const ell_matrix * A, const csr_matrix * csr,
                          const float * x, float * y)
{
    (void)csr;
    const int K = A->max_row_len;
    for (int i = 0; i < A->num_rows; i++) {
        const float * v = A->vals    + (size_t)i * K;
        const int   * c = A->col_idx + (size_t)i * K;
        float sum = 0.0f;
        for (int k = 0; k < K; k++)
            sum += v[k] * x[c[k]];
        y[i] = sum;
    }
}

/* Nonzero-based partitioning: every thread gets about the same number of
 * TRUE nonzeros. The ELL arrays do not record where padding starts; `csr`
 * is the same matrix.  TODO (Task 1b). */
static void spmv_ell_nnz(const ell_matrix * A, const csr_matrix * csr,
                         const float * x, float * y)
{
    spmv_ell_rows(A, csr, x, y);   /* placeholder */
}

/* ====================================================================
 *                      provided -- no need to edit
 * ==================================================================== */

static void usage(char** argv)
{
    printf("Usage: %s my_matrix.mtx [--threads=N] [--schedule=S[,chunk]] "
           "[--partition=row|nnz] [--iters=N]\n", argv[0]);
    printf("  --schedule=S    static (default), dynamic, guided; optional ,chunk\n");
    printf("  --partition=P   row (default) or nnz\n");
    printf("  --iters=N       fix the timed iteration count instead of picking it\n");
}

static int set_schedule(const char * spec)
{
    char kind[32]; int chunk = 0;
    if (sscanf(spec, "%31[a-z],%d", kind, &chunk) < 1) return -1;
    if      (strcmp(kind, "static")  == 0) omp_set_schedule(omp_sched_static,  chunk);
    else if (strcmp(kind, "dynamic") == 0) omp_set_schedule(omp_sched_dynamic, chunk);
    else if (strcmp(kind, "guided")  == 0) omp_set_schedule(omp_sched_guided,  chunk);
    else if (strcmp(kind, "auto")    == 0) omp_set_schedule(omp_sched_auto,    chunk);
    else return -1;
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 1 || get_arg(argc, argv, "help") != NULL) {
        usage(argv);
        return argc == 1 ? -1 : 0;
    }
    const char * mm_filename = argv[1];

    int fixed_iters = get_argval_int(argc, argv, "iters", 0);
    int threads     = get_argval_int(argc, argv, "threads", 0);
    if (threads > 0) omp_set_num_threads(threads);

    const char * schedule = get_argval(argc, argv, "schedule");
    if (!schedule) schedule = "static";
    if (set_schedule(schedule) != 0) {
        fprintf(stderr, "bad --schedule=%s\n", schedule); return -1;
    }

    const char * partition = get_argval(argc, argv, "partition");
    if (!partition) partition = "row";
    void (*kernel)(const ell_matrix *, const csr_matrix *, const float *, float *);
    if      (strcmp(partition, "row") == 0) kernel = spmv_ell_rows;
    else if (strcmp(partition, "nnz") == 0) kernel = spmv_ell_nnz;
    else { fprintf(stderr, "bad --partition=%s (row or nnz)\n", partition); return -1; }

    /* ---- load and convert (not timed) ---- */
    coo_matrix coo;
    read_coo_matrix(&coo, mm_filename);

    float * x     = (float*)alloc_array(coo.num_cols, sizeof(float));
    float * y     = (float*)alloc_array(coo.num_rows, sizeof(float));
    float * y_ref = (float*)alloc_array(coo.num_rows, sizeof(float));
    fill_problem(&coo, x, 13);

    csr_matrix csr;
    coo_to_csr(&coo, &csr);
    delete_coo_matrix(&coo);

    ell_matrix ell;
    if (csr_to_ell(&csr, &ell) != 0) {
        fprintf(stderr, "csr_to_ell failed: %d rows x %d slots is too much memory\n",
                csr.num_rows, csr_max_row_len(&csr));
        return 1;
    }

    int nthreads = 0;
    #pragma omp parallel
    {
        #pragma omp master
        nthreads = omp_get_num_threads();
    }
    long long slots = (long long)ell.num_rows * (long long)ell.max_row_len;
    printf("\nfile=%s rows=%d cols=%d nonzeros=%d max_row_len=%d\n",
           mm_filename, csr.num_rows, csr.num_cols, csr.num_nonzeros, ell.max_row_len);
    printf("threads=%d schedule=%s partition=%s\n", nthreads, schedule, partition);
    printf("storage: CSR %.2f MB, ELL %.2f MB (ELL/CSR = %.2f), padding %.1f%%\n",
           storage_bytes_csr(&csr) / 1e6, storage_bytes_ell(&ell) / 1e6,
           (double)storage_bytes_ell(&ell) / (double)storage_bytes_csr(&csr),
           slots > 0 ? 100.0 * (double)(slots - csr.num_nonzeros) / (double)slots : 0.0);
    fflush(stdout);

    /* ---- correctness: your kernel against the sequential CSR baseline ---- */
    csr_spmv(&csr, x, y_ref);
    kernel(&ell, &csr, x, y);

    double num = 0.0, den = 0.0;
    for (int i = 0; i < csr.num_rows; i++) {
        double d = (double)y[i] - (double)y_ref[i];
        num += d * d;
        den += (double)y_ref[i] * (double)y_ref[i];
    }
    double rel_err = (den > 0.0) ? sqrt(num / den) : sqrt(num);
    int ok = rel_err < 1e-5;
    printf("correctness vs sequential CSR: %s (relative L2 error %.3e)\n",
           ok ? "PASS" : "FAIL", rel_err);
    if (!ok) return 1;

    /* ---- timing ---- */
    double t0 = wall_seconds();
    kernel(&ell, &csr, x, y);                             // warmup
    double est = wall_seconds() - t0;

    int num_iterations = fixed_iters > 0 ? fixed_iters : pick_iterations(est);
    printf("\tPerforming %d iterations\n", num_iterations);

    t0 = wall_seconds();
    for (int j = 0; j < num_iterations; j++)
        kernel(&ell, &csr, x, y);
    double sec_per_iteration = (wall_seconds() - t0) / (double)num_iterations;

    /* GFLOP/s counts the 2*nnz useful flops; GB/s counts every slot moved. */
    double GFLOPs = (sec_per_iteration == 0) ? 0 :
                    (2.0 * (double)csr.num_nonzeros / sec_per_iteration) / 1e9;
    double GBYTEs = (sec_per_iteration == 0) ? 0 :
                    ((double)bytes_per_ell_spmv(&ell) / sec_per_iteration) / 1e9;
    printf("\tbenchmarking OpenMP ELL-SpMV: %8.4f ms ( %5.2f GFLOP/s %5.1f GB/s)\n",
           sec_per_iteration * 1000.0, GFLOPs, GBYTEs);

    delete_ell_matrix(&ell);
    delete_csr_matrix(&csr);
    free(x); free(y); free(y_ref);
    return 0;
}
