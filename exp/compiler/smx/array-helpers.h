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

static inline int32_t getFixedLength(ContiguouslyStoredType* t)
{
  if (t->isArray())
    return t->toArray()->fixedLength();

  if (t->isEnumStruct()) {
    int fieldCount = 0;
    ast::LayoutDecls* lds = t->toEnumStruct()->decl()->body();
    for (int i = 0; i < lds->length(); ++i) {
      ast::LayoutDecl* ld = lds->at(i);
      if (!ld->isFieldDecl())
        continue;
      ++fieldCount;
    }

    // should also fix smx-compiler::emitIndex
    return fieldCount;
  }

  assert(0); // :TODO: proper error reporting?
  // the new contiguously stored type isn't supported
}

// this one is too complicated to inline arguably
int32_t OffsetOfEnumStructField(EnumStructType* t, int n);

static inline int32_t SizeOfEnumStructLiteral(EnumStructType* t) {
  return OffsetOfEnumStructField(t, -1); // :TODO: get rid of this pesky -1
}

// :TODO: do we want to make other CST functions public
static inline bool hasFixedLength(ContiguouslyStoredType* t) {
  if (t->isArray())
    return t->toArray()->hasFixedLength();
  if (t->isEnumStruct())
    return true;

  assert(false); // :TODO: this should never really happen
}

// because we don't have the definition of RecordDecl in types.h
// we cannot extract the field types from it, so we have to do it somewhere where ast.h has already been imported
// like here
static inline Type* getUniformSubType(ContiguouslyStoredType* t)
{
  if (!t->isArray()) {
    assert(0); // :TODO: this should never really happen
    return nullptr;
  }

  return t->toArray()->contained();
}

// have to keep this non-inlinable to avoid recursive dependencies in types.h
Type* getEnumStructField(EnumStructType* t, Atom* field);

static inline Type* getNonUniformAddressableSubType(ContiguouslyStoredType* t, size_t i)
{
  if (!t->isEnumStruct()) {
    assert(0); // :TODO: this should never really happen
    return nullptr;
  }

  ast::LayoutDecls* lds = t->toEnumStruct()->decl()->body();
  for (int j = i; j < lds->length(); ++j) {
    ast::LayoutDecl* ld = lds->at(i);
    if (ld->isFieldDecl())
      return ld->toFieldDecl()->te().resolved();
  }

  // field index out of range
  assert(false); // :TODO: proper error reporting
  return nullptr;
}

} // namespace sp

#endif // _include_sourcepawn_emit_contiguous_storage_helpers_h_

