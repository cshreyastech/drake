#include "drake/multibody/contact_solvers/block_sparse_lower_triangular_or_symmetric_matrix.h"

#include <algorithm>
#include <unordered_set>

#include <fmt/format.h>

#include "drake/common/drake_throw.h"
#include "drake/common/fmt_eigen.h"

namespace drake {
namespace multibody {
namespace contact_solvers {
namespace internal {

template <typename MatrixType, bool is_symmetric>
BlockSparseLowerTriangularOrSymmetricMatrix<MatrixType, is_symmetric>::
    BlockSparseLowerTriangularOrSymmetricMatrix(
        BlockSparsityPattern sparsity_pattern)
    : sparsity_pattern_(std::move(sparsity_pattern)),
      block_cols_(ssize(sparsity_pattern_.block_sizes())),
      blocks_(block_cols_),
      starting_cols_(block_cols_, 0),
      block_row_to_flat_(block_cols_, std::vector<int>(block_cols_, -1)) {
  for (int j = 1; j < block_cols_; ++j) {
    starting_cols_[j] =
        starting_cols_[j - 1] + sparsity_pattern_.block_sizes()[j - 1];
  }
  cols_ = block_cols_ == 0
              ? 0
              : starting_cols_.back() + sparsity_pattern_.block_sizes().back();

  for (int j = 0; j < block_cols_; ++j) {
    blocks_[j].reserve(ssize(block_row_indices(j)));
    for (int flat = 0; flat < ssize(block_row_indices(j)); ++flat) {
      const int i = block_row_indices(j)[flat];
      DRAKE_DEMAND(i >= j);
      block_row_to_flat_[j][i] = flat;

      const int rows = sparsity_pattern_.block_sizes()[i];
      const int cols = sparsity_pattern_.block_sizes()[j];
      blocks_[j].emplace_back(MatrixType::Zero(rows, cols));
    }
  }
}

template <typename MatrixType, bool is_symmetric>
void BlockSparseLowerTriangularOrSymmetricMatrix<MatrixType,
                                                 is_symmetric>::SetZero() {
  for (int c = 0; c < block_cols_; ++c) {
    for (MatrixType& block : blocks_[c]) {
      block.setZero();
    }
  }
}

template <typename MatrixType, bool is_symmetric>
MatrixX<double> BlockSparseLowerTriangularOrSymmetricMatrix<
    MatrixType, is_symmetric>::MakeDenseMatrix() const {
  MatrixX<double> result = MatrixX<double>::Zero(rows(), cols());
  for (int j = 0; j < block_cols_; ++j) {
    for (int flat = 0; flat < ssize(block_row_indices(j)); ++flat) {
      const int i = block_row_indices(j)[flat];
      const int rows = sparsity_pattern_.block_sizes()[i];
      const int cols = sparsity_pattern_.block_sizes()[j];
      const int starting_row = starting_cols_[i];
      const int starting_col = starting_cols_[j];
      result.block(starting_row, starting_col, rows, cols) = blocks_[j][flat];
      if (i != j && is_symmetric) {
        result.block(starting_col, starting_row, cols, rows) =
            blocks_[j][flat].transpose();
      }
    }
  }
  return result;
}

template <typename MatrixType, bool is_symmetric>
void BlockSparseLowerTriangularOrSymmetricMatrix<MatrixType, is_symmetric>::
    ZeroRowsAndColumns(const std::vector<int>& indices) {
  DRAKE_THROW_UNLESS(is_symmetric);
  for (int j : indices) {
    if (!(0 <= j && j < block_cols())) {
      throw std::logic_error(fmt::format(
          "Input index out of range. Indices must lie in [0, {}); {} is given.",
          block_cols(), j));
    }
  }
  std::unordered_set<int> indices_set(indices.begin(), indices.end());
  for (int j = 0; j < block_cols(); ++j) {
    /* Zero all blocks in the column except the diagonal if j is among the
     row/column indices to be zeroed out. */
    if (indices_set.count(j) > 0) {
      /* We want to keep the conditioning of the matrix in the ballpark of the
       original matrix (e.g. setting diagonal blocks to identity when the
       original values are the on order of millions is a bad idea), so we keep
       only the diagonal entries of the diagonal blocks and zero the
       off-diagonal entries. */
      /* Calling eval() here is important to avoid aliasing. */
      blocks_[j][0] = blocks_[j][0].diagonal().eval().asDiagonal();
      /* Zero all off diagonal blocks in the j-th column. */
      for (int flat = 1; flat < ssize(blocks_[j]); ++flat) {
        blocks_[j][flat].setZero();
      }
    } else {
      /* Otherwise, zero out all blocks with block row indices in the set. */
      for (int i : indices) {
        const int flat = block_row_to_flat_[j][i];
        if (flat >= 0) {
          blocks_[j][flat].setZero();
        }
      }
    }
  }
}

template <typename MatrixType, bool is_symmetric>
void BlockSparseLowerTriangularOrSymmetricMatrix<
    MatrixType, is_symmetric>::AssertValid(int i, int j,
                                           const std::optional<MatrixType>& Aij,
                                           const char* source) const {
  if (!(0 <= j && j <= i && i <= block_rows())) {
    throw std::runtime_error(fmt::format(
        "{}: block indices out of bound. It is required that 0 <= j && j <= i "
        "&& i < block_rows(). Instead, i = {}, j = {}, block_rows() = {}.",
        source, i, j, block_rows()));
  }
  if (!HasBlock(i, j)) {
    throw std::runtime_error(fmt::format(
        "{}: The requested {},{}-th block doesn't exist.", source, i, j));
  }

  auto is_block_symmetric = [](const MatrixType& M) {
    /* Note that "<=" is critical to allow zero matrices to pass. */
    return (M - M.transpose()).norm() <= 1e-12 * M.norm();
  };

  if (is_symmetric && i == j && Aij.has_value() && !is_block_symmetric(*Aij)) {
    throw std::runtime_error(
        fmt::format("{}: The {}-th diagonal block must be symmetric for a "
                    "symmetric matrix. Instead, the block is:\n {}",
                    source, i, fmt_eigen(*Aij)));
  }
}

template class BlockSparseLowerTriangularOrSymmetricMatrix<MatrixX<double>,
                                                           true>;
template class BlockSparseLowerTriangularOrSymmetricMatrix<MatrixX<double>,
                                                           false>;
template class BlockSparseLowerTriangularOrSymmetricMatrix<Matrix3<double>,
                                                           true>;
template class BlockSparseLowerTriangularOrSymmetricMatrix<Matrix3<double>,
                                                           false>;

}  // namespace internal
}  // namespace contact_solvers
}  // namespace multibody
}  // namespace drake
