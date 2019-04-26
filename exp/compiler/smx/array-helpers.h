// vim: set sts=2 ts=8 sw=2 tw=99 et:
//
// Copyright (C) 2012 David Anderson
//
// This file is part of SourcePawn.
//
// SourcePawn is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// SourcePawn is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SourcePawn. If not, see http://www.gnu.org/licenses/.

#ifndef _include_sourcepawn_emit_contiguous_storage_helpers_h_
#define _include_sourcepawn_emit_contiguous_storage_helpers_h_

#include <amtl/am-bits.h>
#include <amtl/am-vector.h>
#include <limits.h>
#include <sp_vm_types.h>

#include "parser/ast.h"

namespace sp {

class ContiguouslyStoredType;

struct ContiguousStorageInfo
{
  // Base type of the array-like.
  ContiguouslyStoredType* base_type;

  // Total number of bytes to allocate for this array-like.
  int32_t bytes;

  // Total number of bytes needed for indirection vectors.
  int32_t iv_size;

  // Total number of bytes for final dimension data.
  int32_t data_size;
};

// Compute fixed contiguous storage size information; returns false if the size was too big.
bool ComputeContiguousStorageInfo(ContiguouslyStoredType* cst, ContiguousStorageInfo* out);

static inline int32_t CellLengthOfString(size_t str_length)
{
  // :TODO: ensure this
  assert(str_length < INT_MAX);
  return (int32_t)(ke::Align(str_length + 1, sizeof(cell_t)) / sizeof(cell_t));
}

// Total number of bytes needed for each entry in the final fixed-length
// vector. This is always aligned to the size of a cell.
static inline int32_t SizeOfArrayLiteral(ArrayType* t) {
  if (t->isCharArray())
    return CellLengthOfString(t->fixedLength()) * sizeof(cell_t);

  return t->toArray()->fixedLength() * sizeof(cell_t);
}

} // namespace sp

#endif // _include_sourcepawn_emit_contiguous_storage_helpers_h_

