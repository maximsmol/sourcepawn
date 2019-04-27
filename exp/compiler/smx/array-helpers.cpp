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

#include "array-helpers.h"
#include "types.h"
#include <amtl/am-bits.h>
#include <amtl/am-algorithm.h>
#include <assert.h>
#include <sp_vm_types.h>

namespace sp {

using namespace ke;

struct CCSINode {
  bool descend;
  Type* item;
};

struct IVSizeNode {
  bool arrayLike;
  uint64_t size;
};

// :TODO: warn of array dim overflow in type-resolver.
bool
ComputeContiguousStorageInfo(ContiguouslyStoredType* base, ContiguousStorageInfo* out)
{
  out->base_type = base;

  Vector<uint64_t> bytesStack;
  Vector<IVSizeNode> iv_sizeStack;

  bytesStack.append(0);
  iv_sizeStack.append<IVSizeNode>({true, 0});

  Vector<CCSINode> q;
  q.append<CCSINode>({true, base});

  // unrolled recursive visitor
  // implemented through depth-first traversal with subgraph completion tombstones (CCSINode)
  //
  // for uniform contents (arrays):
  // q               | bytesStack   | iv_sizeStack
  // ----------------+--------------+-------------
  // int[2][3][4]    | 0            | 0
  // *2, int[3][4]   | 0, 0         | 0, 0
  // *2, *3, int[4]  | 0, 0, 0      | 0, 0, 0
  // *2, *3, *4, int | 0, 0, 0, 0   | 0, 0, 0, 0
  // *2, *3, *4      | 0, 0, 0, 4   | 0, 0, 0, 0, -1
  // *2, *3          | 0, 0, 16     | 0, 0, 0
  // *2              | 0, 48        | 0, 3
  // none            | 96           | 8
  //
  //
  // for single-dimensional uniform content:
  //
  // int a[32];
  //
  // q         | bytesStack   | iv_sizeStack
  // ----------+--------------+-------------
  // int[32]   | 0            | 0
  // *32, int  | 0, 0         | 0, 0
  // *32       | 0, 4         | 0, 0, -1
  // none      | 128          | 0
  //
  //
  // for non-uniform contents (enum structs):
  //
  // enum struct A {
  //   int a; // 4
  //   int b[2][3]; // 6*4 = 24
  //   int c[3][4]; // 12*4 = 48
  //   int d; // 4
  // } // 80
  //
  // q                              | bytesStack   | iv_sizeStack
  // -------------------------------+--------------+-------------
  // A                              | 0            | 0
  // int, int[2][3], int[3][4], int | 0, 0         | 0, -1
  // int, int[2][3], int[3][4]      | 4            | 0, -1
  // int, int[2][3], *3, int[4]     | 4, 0         | 0, -1, 0
  // int, int[2][3], *3, *4, int    | 4, 0, 0      | 0, -1, 0, 0
  // int, int[2][3], *3, *4         | 4, 0, 4      | 0, -1, 0, 0, -1
  // int, int[2][3], *3             | 4, 16        | 0, -1, 0
  // int, int[2][3]                 | 48           | 3
  // int, *2, int[3]                | 52, 0        | 3, 0
  // int, *2, *3, int               | 52, 0, 0     | 3, 0, 0
  // int, *2, *3                    | 52, 0, 4     | 3, 0, 0, -1
  // int, *2                        | 52, 8        | 3, 0
  // int                            | 76           | 5
  // none                           | 80           | 5
  //
  //
  // another example:
  //
  // enum struct A {
  //   int[2][3];
  //   int[2][3];
  // }
  //
  // enum struct B {
  //   A a[3];
  // }
  // q                              | bytesStack   | iv_sizeStack
  // -------------------------------+--------------+-------------
  // B                              | 0            | 0
  // A[3]                           | 0            | 0, -1
  // *3, A                          | 0, 0         | 0, -1, 0
  // *3, int[2][3], int[2][3]       | 0, 0         | 0, -1, 0, -1
  // *3, int[2][3], *2, int[3]      | 0, 0, 0      | 0, -1, 0, -1, 0
  // *3, int[2][3], *2, *3, int     | 0, 0, 0, 0   | 0, -1, 0, -1, 0, 0
  // *3, int[2][3], *2, *3          | 0, 0, 0, 4   | 0, -1, 0, -1, 0, 0, -1
  // *3, int[2][3], *2              | 0, 0, 12     | 0, -1, 0, -1, 0
  // *3, int[2][3]                  | 0, 24        | 0, -1, 2
  // *3, *2, int[3]                 | 0, 24, 0     | 0, -1, 2, 0
  // *3, *2, *3, int                | 0, 24, 0, 0  | 0, -1, 2, 0, 0
  // *3, *2, *3                     | 0, 24, 0, 4  | 0, -1, 2, 0, 0, -1
  // *3, *2                         | 0, 24, 12    | 0, -1, 2, 0
  // *3                             | 0, 48        | 0, -1, 4
  // none                           | 144          | 15
  //
  //
  // emulating multiple dimensions with enum structs:
  //
  // // int[2][3];
  // enum struct A {
  //   int a; // 4
  //   int b; // 4
  //   int c; // 4
  // } // 12
  // enum struct B {
  //   A a; // 12
  //   A b; // 12
  // } // 24
  //
  //
  // q                              | bytesStack   | iv_sizeStack
  // -------------------------------+--------------+-------------
  // B                              | 0            | 0
  // A, A                           | 0            | 0, -1
  // A, int, int, int               | 0            | 0, -1
  // A, int, int                    | 4            | 0, -1
  // A, int                         | 8            | 0, -1
  // A                              | 12           | 0, -1
  // int, int, int                  | 12           | 0, -1
  // int, int                       | 16           | 0, -1
  // int                            | 20           | 0, -1
  // none                           | 24           | 0, -1
  //
  while (!q.empty()) {
    CCSINode n = q.popCopy();
    Type* item = n.item;

    if (!n.descend) {
      if (!item->isContiguouslyStored()) {
        assert(0); // this should never really happen
        // :TODO: proper error reporting?
        continue;
      }

      ContiguouslyStoredType* cst = item->toContiguouslyStored();


      uint64_t bytes = bytesStack.popCopy();
      if (!IsUint64MultiplySafe(bytes, getFixedLength(cst)))
        return false;
      bytes *= getFixedLength(cst);

      if (!IsUint64AddSafe(bytesStack.back(), bytes))
        return false;
      bytesStack.back() += bytes;


      bool innermostArray = false;
      IVSizeNode child_ivs = iv_sizeStack.popCopy();
      if (!child_ivs.arrayLike) {
        innermostArray = true;
        child_ivs = iv_sizeStack.popCopy();
        assert(child_ivs.arrayLike); // we collapse nested values
      }

      if (!iv_sizeStack.back().arrayLike)
        iv_sizeStack.pop();
      assert(iv_sizeStack.back().arrayLike); // we collapse nested values

      if (!IsUint64MultiplySafe(child_ivs.size, getFixedLength(cst)))
        return false;
      child_ivs.size *= getFixedLength(cst);

      if (!IsUint64AddSafe(iv_sizeStack.back().size, child_ivs.size))
        return false;
      iv_sizeStack.back().size += child_ivs.size;

      if (!innermostArray) {
        uint64_t new_ivs = getFixedLength(cst);
        if (!IsUint64MultiplySafe(new_ivs, sizeof(cell_t)))
          return false; // :TODO: is this ever going to happen?
        new_ivs *= sizeof(cell_t);

        if (!IsUint64AddSafe(iv_sizeStack.back().size, new_ivs))
          return false;
        iv_sizeStack.back().size += new_ivs;
      }


      if (bytesStack.back() > INT_MAX) {
        assert(0);
        return false;
      }
      if (iv_sizeStack.back().size > INT_MAX) {
        assert(0);
        return false;
      }

      continue;
    }

    if (!item->isContiguouslyStored()) {
      // the storage size is always cell_t
      // :TODO: not necessarily true
      if (!IsUint64AddSafe(bytesStack.back(), sizeof(cell_t)))
        return false;
      bytesStack.back() += sizeof(cell_t);

      if (iv_sizeStack.back().arrayLike)
        iv_sizeStack.append<IVSizeNode>({false, 0});

      continue;
    }

    ContiguouslyStoredType* cst = item->toContiguouslyStored();

    if (cst->isCharArray()) {
      uint64_t strCellSize = CellLengthOfString(getFixedLength(cst));
      if (!IsUint64MultiplySafe(strCellSize, sizeof(cell_t)))
        return false; // :TODO: is this ever going to happen?
      strCellSize *= sizeof(cell_t);

      bytesStack.back() += strCellSize;

      // strings also get ivs!
      // this comment is here to acknowledge what you might wanna do
      // and to tell you to not do it!
      // if (iv_sizeStack.back().arrayLike)
      //   iv_sizeStack.append<IVSizeNode>({false, 0});

      continue;
    } else if (cst->hasUniformContents()) {

      q.append<CCSINode>({false, cst});
      q.append<CCSINode>({true, getUniformSubType(cst)});

      bytesStack.append(0);
      iv_sizeStack.append<IVSizeNode>({true, 0});
    } else {
      // non-uniform things don't support slicing or dimensions
      // so they lack optimizations for those things
      for (int i = 0; i < getFixedLength(cst); ++i) {
        Type* t = getNonUniformAddressableSubType(cst, i);
        if (t == nullptr)
          continue;
        q.append<CCSINode>({true, t});
      }

      if (iv_sizeStack.back().arrayLike)
        iv_sizeStack.append<IVSizeNode>({false, 0});
    }
  }

  // char g[10] = "abcdefghi"; // needs this
  if (!iv_sizeStack.back().arrayLike) // :TODO: not sure if this is not indicative of a bug
    iv_sizeStack.pop();
  assert(iv_sizeStack.back().arrayLike); // we collapse nested values

  out->data_size = bytesStack.popCopy();
  out->iv_size = iv_sizeStack.popCopy().size;

  assert(bytesStack.empty());
  assert(iv_sizeStack.empty());

  assert(out->data_size > 0);
  assert(out->data_size >= 0); // tombstones should not leak

  out->bytes = out->data_size + out->iv_size;
  return true;
}

int32_t
OffsetOfEnumStructField(EnumStructType* t, int n)
{
  int32_t res = 0;

  ast::LayoutDecls* lds = t->decl()->body();
  int32_t totalDecls = lds->length(); // :TODO: code reuse with getFixedLength?
  int fieldN = 0;
  for (int i = 0; i < totalDecls; ++i) {
    if (!lds->at(i)->isFieldDecl())
      continue;

    if (fieldN == n)
      return res;

    Type* t = lds->at(i)->toFieldDecl()->te().resolved();
    if (t->isArray())
      res += SizeOfArrayLiteral(t->toArray());
    else
      res += sizeof(cell_t);

    ++fieldN;
  }

  return res;
}

} // namespace sp
