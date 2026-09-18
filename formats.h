
#pragma once

#include <stdlib.h>
#include <string.h>

/* Everything here is `static inline` so the header can be included from more
 * than one program: every .c here is a complete program that includes it. Do
 * not drop it. */

// COOrdinate matrix (aka IJV or Triplet format)
typedef struct coo_matrix
{
    int num_rows, num_cols, num_nonzeros;
    int * rows;  //row indices
    int * cols;  //column indices
    float * vals;  //nonzero values
} coo_matrix;

// Compressed Sparse Row matrix
//   row i's nonzeros are col_idx[row_ptr[i] .. row_ptr[i+1]-1]
typedef struct csr_matrix
{
    int num_rows, num_cols, num_nonzeros;
    int * row_ptr;   //length num_rows+1, row_ptr[0] == 0
    int * col_idx;   //length num_nonzeros
    float * vals;    //length num_nonzeros
} csr_matrix;


static inline void delete_coo_matrix(coo_matrix* coo){
    free(coo->rows);   free(coo->cols);   free(coo->vals);
    coo->rows = NULL;  coo->cols = NULL;  coo->vals = NULL;
}

static inline void delete_csr_matrix(csr_matrix* csr){
    free(csr->row_ptr);   free(csr->col_idx);   free(csr->vals);
    csr->row_ptr = NULL;  csr->col_idx = NULL;  csr->vals = NULL;
}

/* COO -> CSR. Stable, so the CSR kernel accumulates each row in the same
 * order the COO kernel does. */
static inline void coo_to_csr(const coo_matrix * coo, csr_matrix * csr)
{
    csr->num_rows     = coo->num_rows;
    csr->num_cols     = coo->num_cols;
    csr->num_nonzeros = coo->num_nonzeros;

    csr->row_ptr = (int*)calloc(coo->num_rows + 1, sizeof(int));
    csr->col_idx = (int*)malloc(coo->num_nonzeros * sizeof(int));
    csr->vals    = (float*)malloc(coo->num_nonzeros * sizeof(float));

    // histogram of row lengths, then prefix sum
    for(int n = 0; n < coo->num_nonzeros; n++)
        csr->row_ptr[coo->rows[n] + 1]++;
    for(int i = 0; i < coo->num_rows; i++)
        csr->row_ptr[i+1] += csr->row_ptr[i];

    int * next = (int*)malloc(coo->num_rows * sizeof(int));
    memcpy(next, csr->row_ptr, coo->num_rows * sizeof(int));
    for(int n = 0; n < coo->num_nonzeros; n++){
        int dest = next[coo->rows[n]]++;
        csr->col_idx[dest] = coo->cols[n];
        csr->vals[dest]    = coo->vals[n];
    }
    free(next);
}

/* Sequential CSR SpMV: y = A*x  (overwrites y, does not accumulate). */
static inline void csr_spmv(const csr_matrix * csr, const float * x, float * y)
{
    for(int i = 0; i < csr->num_rows; i++){
        float sum = 0.0f;
        for(int k = csr->row_ptr[i]; k < csr->row_ptr[i+1]; k++)
            sum += csr->vals[k] * x[csr->col_idx[k]];
        y[i] = sum;
    }
}

/* ---- memory traffic of one SpMV (for the GB/s figure) ---- */

static inline size_t bytes_per_coo_spmv(const coo_matrix * coo)
{
    size_t bytes = 0;
    bytes += 2*sizeof(int) * coo->num_nonzeros;   // row and column indices
    bytes += 2*sizeof(float) * coo->num_nonzeros; // A[i,j] and x[j]

    char * occupied = (char*)calloc(coo->num_rows > 0 ? (size_t)coo->num_rows : 1, 1);
    for(int n = 0; n < coo->num_nonzeros; n++)
        occupied[coo->rows[n]] = 1;
    for(int i = 0; i < coo->num_rows; i++)
        if(occupied[i])
            bytes += 2*sizeof(float);             // y[i] = y[i] + ...
    free(occupied);
    return bytes;
}

/* col_idx + val + x[j] per nonzero, row_ptr + y[i] per row. */
static inline size_t bytes_per_csr_spmv(const csr_matrix * csr)
{
    size_t bytes = 0;
    bytes += (size_t)csr->num_nonzeros * (sizeof(int) + 2*sizeof(float));
    bytes += (size_t)csr->num_rows * (sizeof(int) + sizeof(float));
    return bytes;
}

/* ---- storage footprint ---- */

static inline size_t storage_bytes_coo(const coo_matrix * coo)
{
    return (size_t)coo->num_nonzeros * (2*sizeof(int) + sizeof(float));
}

static inline size_t storage_bytes_csr(const csr_matrix * csr)
{
    return (size_t)csr->num_nonzeros * (sizeof(int) + sizeof(float))
         + (size_t)(csr->num_rows + 1) * sizeof(int);
}


/* ================================================================== *
 * ELLPACK (ELL)
 *
 * Every row gets the same number of slots, K = the longest row length, and
 * short rows are padded with explicit zeros. Entry (i,k) is at i*K + k, so
 * there is no row_ptr and the inner loop has a fixed trip count K. The cost
 * is num_rows*K storage instead of nnz.
 *
 * Padded slots carry col_idx = 0 and val = 0.0f, so a kernel may multiply
 * them unconditionally: 0.0f * x[0] contributes nothing and the load is in
 * range.
 * ================================================================== */

typedef struct ell_matrix
{
    int num_rows, num_cols, num_nonzeros;  //num_nonzeros = the TRUE nnz
    int max_row_len;                       //K: slots stored per row
    int   * col_idx;                       //length num_rows*K, row-major, 0 in padded slots
    float * vals;                          //length num_rows*K, row-major, 0.0f in padded slots
} ell_matrix;

static inline void delete_ell_matrix(ell_matrix * ell){
    free(ell->col_idx);   free(ell->vals);
    ell->col_idx = NULL;  ell->vals = NULL;
}

/* Longest row of a CSR matrix, i.e. the K an ELL conversion would use. */
static inline int csr_max_row_len(const csr_matrix * csr)
{
    int k = 0;
    for (int i = 0; i < csr->num_rows; i++) {
        int len = csr->row_ptr[i+1] - csr->row_ptr[i];
        if (len > k) k = len;
    }
    return k;
}

/* CSR -> ELL. PROVIDED.
 *
 * calloc, not malloc: it zeroes vals (the padding value) and col_idx (a
 * valid column). Returns 0 on success, nonzero if the allocation failed,
 * which can happen where the CSR fit, since num_rows*K can be far larger
 * than nnz. */
static inline int csr_to_ell(const csr_matrix * csr, ell_matrix * ell)
{
    ell->num_rows     = csr->num_rows;
    ell->num_cols     = csr->num_cols;
    ell->num_nonzeros = csr->num_nonzeros;
    ell->max_row_len  = csr_max_row_len(csr);

    size_t slots = (size_t)csr->num_rows * (size_t)ell->max_row_len;
    if (slots == 0) slots = 1;               //see alloc.h: an empty matrix still needs a pointer

    ell->col_idx = (int*)  calloc(slots, sizeof(int));
    ell->vals    = (float*)calloc(slots, sizeof(float));
    if (!ell->col_idx || !ell->vals) {
        delete_ell_matrix(ell);
        return 1;
    }

    const size_t K = (size_t)ell->max_row_len;
    for (int i = 0; i < csr->num_rows; i++) {
        size_t at = (size_t)i * K;
        for (int p = csr->row_ptr[i]; p < csr->row_ptr[i+1]; p++, at++) {
            ell->col_idx[at] = csr->col_idx[p];
            ell->vals[at]    = csr->vals[p];
        }
        /* the remaining slots of row i keep calloc's zeros */
    }
    return 0;
}

/* Sequential ELL SpMV: y = A*x. Padded slots are multiplied like any other. */
static inline void ell_spmv(const ell_matrix * ell, const float * x, float * y)
{
    const int K = ell->max_row_len;
    for (int i = 0; i < ell->num_rows; i++) {
        const float * v = ell->vals    + (size_t)i * K;
        const int   * c = ell->col_idx + (size_t)i * K;
        float sum = 0.0f;
        for (int k = 0; k < K; k++)
            sum += v[k] * x[c[k]];
        y[i] = sum;
    }
}

/* ---- ELL storage and traffic: set by the slot count, padding included ---- */

static inline size_t storage_bytes_ell(const ell_matrix * ell)
{
    return (size_t)ell->num_rows * (size_t)ell->max_row_len
         * (sizeof(int) + sizeof(float));
}

static inline size_t bytes_per_ell_spmv(const ell_matrix * ell)
{
    size_t slots = (size_t)ell->num_rows * (size_t)ell->max_row_len;
    return slots * (sizeof(int) + 2*sizeof(float))   //col_idx + val + x[j]
         + (size_t)ell->num_rows * sizeof(float);    //y[i]
}

/* Slots that hold an explicit zero. */
static inline long long ell_padded_entries(const ell_matrix * ell)
{
    return (long long)ell->num_rows * (long long)ell->max_row_len
         - (long long)ell->num_nonzeros;
}
