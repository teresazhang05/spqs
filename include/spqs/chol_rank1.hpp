#pragma once

#include <vector>

namespace spqs {

// Pre-reserve internal scratch to avoid any first-use allocations in project loop.
void chol_rank1_prealloc(int max_n);

// Append one row/col to an SPD matrix factorized as L*L^T.
// Inputs:
// - l_lower: n x n lower-triangular factor in row-major dense storage
// - g: correlations between existing active rows and appended row (size n)
// - diag: self-correlation of appended row
// Output:
// - l_lower resized to (n+1) x (n+1) lower-triangular factor
// Returns false on SPD failure.
bool chol_append(std::vector<double>* l_lower,
                 int n,
                 const std::vector<double>& g,
                 double diag,
                 double diag_eps);

// Remove one row/col from an SPD matrix factorized as L*L^T.
// Uses stable Givens rotations on the equivalent upper-triangular factor.
// Output factor is (n-1) x (n-1).
bool chol_remove(std::vector<double>* l_lower,
                 int n,
                 int remove_pos,
                 double diag_eps);

}  // namespace spqs
