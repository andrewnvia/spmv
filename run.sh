export OMP_NUM_THREADS=8
./spmv-omp-csr matrices/heart1.mtx
./spmv-omp-csr matrices/heart1.mtx --threads=8 --schedule=dynamic,64 --partition=nnz
./spmv-omp-ell matrices/heart1.mtx --threads=8 --schedule=guided
./spmv-csr     matrices/heart1.mtx          # sequential CSR
./spmv-ell     matrices/heart1.mtx          # sequential ELL
