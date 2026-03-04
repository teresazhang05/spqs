#pragma once

#include <cmath>
#include <vector>

#include "spqs/assert.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/rhs.hpp"
#include "spqs/violator_types.hpp"

namespace spqs {

struct ConstraintRef {
  ConstraintScope scope = ConstraintScope::GLOBAL;
  int constraint_id = -1;
  int block_id = -1;
  int row_in_block = -1;
  int global_row = -1;
};

inline int total_constraints(const ConstraintsLocal& local,
                             const ConstraintsGlobal& global) {
  return local.total_rows() + global.m_global;
}

inline bool decode_constraint_id(const ConstraintsLocal& local,
                                 const ConstraintsGlobal& global,
                                 int constraint_id,
                                 ConstraintRef* out) {
  SPQS_CHECK(out != nullptr, "decode_constraint_id requires non-null output");
  if (constraint_id < 0 || constraint_id >= total_constraints(local, global)) {
    return false;
  }
  const int local_rows = local.total_rows();
  if (constraint_id < local_rows) {
    const int block_id = constraint_id / local.m_local_per_block;
    const int row_in_block = constraint_id % local.m_local_per_block;
    out->scope = ConstraintScope::LOCAL;
    out->constraint_id = constraint_id;
    out->block_id = block_id;
    out->row_in_block = row_in_block;
    out->global_row = -1;
    return true;
  }
  const int global_row = constraint_id - local_rows;
  out->scope = ConstraintScope::GLOBAL;
  out->constraint_id = constraint_id;
  out->block_id = -1;
  out->row_in_block = -1;
  out->global_row = global_row;
  return true;
}

inline double constraint_rhs_value(const ConstraintRef& ref,
                                   const RHSAll& rhs) {
  if (ref.scope == ConstraintScope::LOCAL) {
    return rhs.local.block_ptr(ref.block_id)[ref.row_in_block];
  }
  return rhs.global.b[static_cast<std::size_t>(ref.global_row)];
}

inline const double* constraint_row_ptr(const ConstraintRef& ref,
                                        const ConstraintsLocal& local,
                                        const ConstraintsGlobal& global) {
  if (ref.scope == ConstraintScope::LOCAL) {
    return local.A_block[static_cast<std::size_t>(ref.block_id)].row_ptr(ref.row_in_block);
  }
  return global.row_ptr(ref.global_row);
}

inline int constraint_row_len(const ConstraintRef& ref,
                              const ConstraintsLocal& local,
                              const ConstraintsGlobal& global) {
  if (ref.scope == ConstraintScope::LOCAL) {
    return local.A_block[static_cast<std::size_t>(ref.block_id)].cols;
  }
  return global.n;
}

inline double dot_constraint_q(const ConstraintRef& ref,
                               const ConstraintsLocal& local,
                               const ConstraintsGlobal& global,
                               const double* q_n) {
  const double* row = constraint_row_ptr(ref, local, global);
  if (ref.scope == ConstraintScope::LOCAL) {
    const int offset = local.layout.block_offsets[ref.block_id];
    const int len = local.A_block[static_cast<std::size_t>(ref.block_id)].cols;
    double s = 0.0;
    for (int j = 0; j < len; ++j) {
      s += row[j] * q_n[offset + j];
    }
    return s;
  }
  double s = 0.0;
  for (int j = 0; j < global.n; ++j) {
    s += row[j] * q_n[j];
  }
  return s;
}

inline double dot_constraint_constraint(const ConstraintRef& a,
                                        const ConstraintRef& b,
                                        const ConstraintsLocal& local,
                                        const ConstraintsGlobal& global) {
  if (a.scope == ConstraintScope::LOCAL && b.scope == ConstraintScope::LOCAL) {
    if (a.block_id != b.block_id) {
      return 0.0;
    }
    const double* ra = local.A_block[static_cast<std::size_t>(a.block_id)].row_ptr(a.row_in_block);
    const double* rb = local.A_block[static_cast<std::size_t>(b.block_id)].row_ptr(b.row_in_block);
    const int len = local.A_block[static_cast<std::size_t>(a.block_id)].cols;
    double s = 0.0;
    for (int j = 0; j < len; ++j) {
      s += ra[j] * rb[j];
    }
    return s;
  }

  if (a.scope == ConstraintScope::GLOBAL && b.scope == ConstraintScope::GLOBAL) {
    const double* ra = global.row_ptr(a.global_row);
    const double* rb = global.row_ptr(b.global_row);
    double s = 0.0;
    for (int j = 0; j < global.n; ++j) {
      s += ra[j] * rb[j];
    }
    return s;
  }

  const ConstraintRef* local_ref = (a.scope == ConstraintScope::LOCAL) ? &a : &b;
  const ConstraintRef* global_ref = (a.scope == ConstraintScope::GLOBAL) ? &a : &b;

  const int block_id = local_ref->block_id;
  const int offset = local.layout.block_offsets[block_id];
  const int len = local.A_block[static_cast<std::size_t>(block_id)].cols;

  const double* row_local =
      local.A_block[static_cast<std::size_t>(block_id)].row_ptr(local_ref->row_in_block);
  const double* row_global = global.row_ptr(global_ref->global_row);
  double s = 0.0;
  for (int j = 0; j < len; ++j) {
    s += row_local[j] * row_global[offset + j];
  }
  return s;
}

inline void add_scaled_constraint_to_q(const ConstraintRef& ref,
                                       const ConstraintsLocal& local,
                                       const ConstraintsGlobal& global,
                                       double scale,
                                       double* q_n) {
  if (ref.scope == ConstraintScope::LOCAL) {
    const int block_id = ref.block_id;
    const int offset = local.layout.block_offsets[block_id];
    const int len = local.A_block[static_cast<std::size_t>(block_id)].cols;
    const double* row = local.A_block[static_cast<std::size_t>(block_id)].row_ptr(ref.row_in_block);
    for (int j = 0; j < len; ++j) {
      q_n[offset + j] += scale * row[j];
    }
    return;
  }

  const double* row = global.row_ptr(ref.global_row);
  for (int j = 0; j < global.n; ++j) {
    q_n[j] += scale * row[j];
  }
}

}  // namespace spqs
