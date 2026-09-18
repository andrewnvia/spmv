/* Sequential ELL SpMV -- PROVIDED.
 *
 * The ELL counterpart of spmv-csr.c: converts CSR -> ELL with csr_to_ell(),
 * multiplies with ell_spmv() (both in formats.h), checks the result against
 * the sequential CSR baseline and prints the storage cost of the format.
 *
 *   ./spmv-ell matrix.mtx [--iters=N]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cmdline.h"
#include "input.h"
#include "config.h"
#include "timer.h"
#include "formats.h"
#include "alloc.h"

static void usage(char** argv)
{
    printf("Usage: %s my_matrix.mtx [--iters=N]\n", argv[0]);
    printf("  my_matrix.mtx   real-valued sparse matrix in MatrixMarket format\n");
    printf("  --iters=N       fix the timed iteration count instead of picking it\n");
}

int main(int argc, char** argv)
{
    if (argc == 1 || get_arg(argc, argv, "help") != NULL) {
        usage(argv);
        return argc == 1 ? -1 : 0;
    }
    const char * mm_filename = argv[1];

    int fixed_iters = get_argval_int(argc, argv, "iters", 0);

    coo_matrix coo;
    read_coo_matrix(&coo, mm_filename);

    float * x = (float*)alloc_array(coo.num_cols, sizeof(float));
    float * y = (float*)alloc_array(coo.num_rows, sizeof(float));
    fill_problem(&coo, x, 13);

    size_t coo_bytes = storage_bytes_coo(&coo);

    csr_matrix csr;
    coo_to_csr(&coo, &csr);
    delete_coo_matrix(&coo);

    size_t csr_bytes = storage_bytes_csr(&csr);

    /* One-off conversion cost, timed separately from the SpMV. */
    ell_matrix ell;
    double t0 = wall_seconds();
    if (csr_to_ell(&csr, &ell) != 0) {
        fprintf(stderr, "\ncsr_to_ell failed: %d rows x %d slots is too much memory.\n",
                csr.num_rows, csr_max_row_len(&csr));
        return 1;
    }
    double convert_ms = (wall_seconds() - t0) * 1000.0;

    size_t ell_bytes = storage_bytes_ell(&ell);
    long long slots  = (long long)ell.num_rows * (long long)ell.max_row_len;

    printf("\nfile=%s rows=%d cols=%d nonzeros=%d max_row_len=%d\n",
           mm_filename, csr.num_rows, csr.num_cols, csr.num_nonzeros, ell.max_row_len);
    printf("storage: COO %.2f MB, CSR %.2f MB, ELL %.2f MB (ELL/CSR = %.2f)\n",
           coo_bytes / 1e6, csr_bytes / 1e6, ell_bytes / 1e6,
           (double)ell_bytes / (double)csr_bytes);
    printf("padding: %lld of %lld slots are explicit zeros (%.1f%%)\n",
           slots - (long long)csr.num_nonzeros, slots,
           slots > 0 ? 100.0 * (double)(slots - csr.num_nonzeros) / (double)slots : 0.0);
    printf("conversion: %.4f ms\n", convert_ms);
    fflush(stdout);

    /* ---- equivalence with the CSR baseline ---- */
    float * y_ref = (float*)alloc_array(csr.num_rows, sizeof(float));
    csr_spmv(&csr, x, y_ref);
    ell_spmv(&ell, x, y);

    double num = 0.0, den = 0.0;
    for (int i = 0; i < csr.num_rows; i++) {
        double d = (double)y[i] - (double)y_ref[i];
        num += d * d;
        den += (double)y_ref[i] * (double)y_ref[i];
    }
    double rel_err = (den > 0.0) ? sqrt(num / den) : sqrt(num);
    printf("correctness vs CSR: %s (relative L2 error %.3e)\n",
           (rel_err == 0.0) ? "PASS" : (rel_err < 1e-5 ? "PASS (inexact)" : "FAIL"),
           rel_err);
    free(y_ref);

    // warmup, and an estimate of one iteration
    t0 = wall_seconds();
    ell_spmv(&ell, x, y);
    double est = wall_seconds() - t0;

    int num_iterations = fixed_iters > 0 ? fixed_iters : pick_iterations(est);
    printf("\tPerforming %d iterations\n", num_iterations);

    t0 = wall_seconds();
    for (int j = 0; j < num_iterations; j++)
        ell_spmv(&ell, x, y);
    double sec_per_iteration = (wall_seconds() - t0) / (double)num_iterations;

    /* GFLOP/s counts the 2*nnz useful flops; GB/s counts every slot moved. */
    double GFLOPs = (sec_per_iteration == 0) ? 0 :
                    (2.0 * (double)csr.num_nonzeros / sec_per_iteration) / 1e9;
    double GBYTEs = (sec_per_iteration == 0) ? 0 :
                    ((double)bytes_per_ell_spmv(&ell) / sec_per_iteration) / 1e9;
    printf("\tbenchmarking ELL-SpMV: %8.4f ms ( %5.2f GFLOP/s %5.1f GB/s)\n",
           sec_per_iteration * 1000.0, GFLOPs, GBYTEs);

#ifdef TESTING
    printf("Writing y vector to test_y_ell ...");
    FILE * fp = fopen("test_y_ell", "w");
    for (int i = 0; i < ell.num_rows; i++)
        fprintf(fp, "%f\n", y[i]);
    fclose(fp);
    printf(" done\n");
#endif

    delete_ell_matrix(&ell);
    delete_csr_matrix(&csr);
    free(x);
    free(y);
    return 0;
}
