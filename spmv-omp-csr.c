/* spmv-omp-csr.c -- OpenMP SpMV over CSR.  YOUR FILE (Task 1).
 *
 * A complete program: it reads the matrix, runs the sequential CSR baseline,
 * checks the answer, times the kernel and reports. The two kernels marked
 * YOUR CODE are sequential; parallelize them and leave the rest alone so
 * that everyone's numbers are measured the same way.
 *
 *   build:  gcc -fopenmp -O3 -o spmv-omp-csr spmv-omp-csr.c
 *   run:    ./spmv-omp-csr matrices/3elt_dual.mtx [--threads=8]
 *               [--schedule=static|dynamic[,chunk]|guided[,chunk]]
 *               [--partition=row|nnz] [--iters=N]
 *
 * --threads     defaults to OMP_NUM_THREADS (else all cores)
 * --schedule    OpenMP loop schedule, applied with omp_set_schedule(); see
 *               set_schedule() below. Default: static.
 * --partition   row: threads share the rows; nnz: threads share the
 *               nonzeros. Default: row.
 *
 * Every run prints PASS or FAIL against the sequential baseline and exits
 * nonzero on FAIL. Timings are the average over several iterations and
 * exclude the file read.
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

/* Row-based partitioning.  TODO (Task 1a): parallelize with OpenMP; the
 * schedule must follow --schedule without recompiling. */
static void spmv_csr_rows(const csr_matrix * A, const float * x, float * y)
{
    #pragma omp parallel for schedule(runtime)
    for (int i = 0; i < A->num_rows; i++) {
        float sum = 0.0f;
        for (int k = A->row_ptr[i]; k < A->row_ptr[i+1]; k++)
            sum += A->vals[k] * x[A->col_idx[k]];
        y[i] = sum;
    }
}

/* Nonzero-based partitioning: every thread gets about the same number of
 * nonzeros rather than the same number of rows.  TODO (Task 1b). */
static void spmv_csr_nnz(const csr_matrix * A, const float * x, float * y)
{
    #pragma omp parallel
    {
        const int total = A->num_nonzeros;
        const int thread_num = omp_get_num_threads();
        /* Use of Chatgpt to find clean calculation for index boundaries */
        const int curr_thread = omp_get_thread_num();
        const int start = (curr_thread * total) / thread_num;
        const int end   = ((curr_thread + 1) * total) / thread_num;
        
        int start_row = 0;
        int end_row = A->num_rows;
        /* Find start/end index for row */
        for (int i = 0; i < A->num_rows; i++) {
            if (start < A->row_ptr[i+1]) {
                start_row = i;
                break;
            }
        }
        for (int i = 0; i < A->num_rows; i++) {
            if (end < A->row_ptr[i+1]) {
                end_row = i;
                break;
            }
        }
        /* don't ignore empty rows at start */
        if (curr_thread == 0) start_row = 0;

        for (int i = start_row; i < end_row; i++) {
            float sum = 0.0f;
            for (int k = A->row_ptr[i]; k < A->row_ptr[i+1]; k++)
                sum += A->vals[k] * x[A->col_idx[k]];
            y[i] = sum;
        }
    }
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

/* --schedule=static | dynamic,64 | guided  ->  omp_set_schedule().
 * Returns 0 on success. */
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
    void (*kernel)(const csr_matrix *, const float *, float *);
    if      (strcmp(partition, "row") == 0) kernel = spmv_csr_rows;
    else if (strcmp(partition, "nnz") == 0) kernel = spmv_csr_nnz;
    else { fprintf(stderr, "bad --partition=%s (row or nnz)\n", partition); return -1; }

    /* ---- load (not timed) ---- */
    coo_matrix coo;
    read_coo_matrix(&coo, mm_filename);

    float * x     = (float*)alloc_array(coo.num_cols, sizeof(float));
    float * y     = (float*)alloc_array(coo.num_rows, sizeof(float));
    float * y_ref = (float*)alloc_array(coo.num_rows, sizeof(float));
    fill_problem(&coo, x, 13);

    csr_matrix csr;
    coo_to_csr(&coo, &csr);
    delete_coo_matrix(&coo);

    int nthreads = 0;
    #pragma omp parallel
    {
        #pragma omp master
        nthreads = omp_get_num_threads();
    }
    printf("\nfile=%s rows=%d cols=%d nonzeros=%d\n",
           mm_filename, csr.num_rows, csr.num_cols, csr.num_nonzeros);
    printf("threads=%d schedule=%s partition=%s\n", nthreads, schedule, partition);
    printf("storage: CSR %.2f MB\n", storage_bytes_csr(&csr) / 1e6);
    fflush(stdout);

    /* ---- correctness: your kernel against the sequential baseline ---- */
    csr_spmv(&csr, x, y_ref);
    kernel(&csr, x, y);

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
    kernel(&csr, x, y);                                   // warmup
    double est = wall_seconds() - t0;

    int num_iterations = fixed_iters > 0 ? fixed_iters : pick_iterations(est);
    printf("\tPerforming %d iterations\n", num_iterations);

    t0 = wall_seconds();
    for (int j = 0; j < num_iterations; j++)
        kernel(&csr, x, y);
    double sec_per_iteration = (wall_seconds() - t0) / (double)num_iterations;

    double GFLOPs = (sec_per_iteration == 0) ? 0 :
                    (2.0 * (double)csr.num_nonzeros / sec_per_iteration) / 1e9;
    double GBYTEs = (sec_per_iteration == 0) ? 0 :
                    ((double)bytes_per_csr_spmv(&csr) / sec_per_iteration) / 1e9;
    printf("\tbenchmarking OpenMP CSR-SpMV: %8.4f ms ( %5.2f GFLOP/s %5.1f GB/s)\n",
           sec_per_iteration * 1000.0, GFLOPs, GBYTEs);

    delete_csr_matrix(&csr);
    free(x); free(y); free(y_ref);
    return 0;
}
