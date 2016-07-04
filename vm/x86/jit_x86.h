// vim: set ts=8 sts=2 sw=2 tw=99 et:
//
// This file is part of SourcePawn.
// 
// SourcePawn is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// SourcePawn is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with SourcePawn.  If not, see <http://www.gnu.org/licenses/>.
#ifndef _INCLUDE_SOURCEPAWN_JIT_X86_H_
#define _INCLUDE_SOURCEPAWN_JIT_X86_H_

#include <sp_vm_types.h>
#include <sp_vm_api.h>
#include <am-vector.h>
#include "jit.h"
#include "plugin-runtime.h"
#include "plugin-context.h"
#include "compiled-function.h"
#include "opcodes.h"
#include "macro-assembler.h"

using namespace SourcePawn;

namespace sp {
class LegacyImage;
class Environment;
class CompiledFunction;
class CallThunk;

class Compiler : public CompilerBase
{
  friend class CallThunk;

 public:
  Compiler(PluginRuntime *rt, cell_t pcode_offs);
  ~Compiler();

 private:
  bool setup(cell_t pcode_offs);
  bool emitOp(sp::OPCODE op) override;

 private:
  void emitThrowPath(int err) override;
  void emitErrorHandlers() override;

  Label *labelAt(size_t offset);
  bool emitCall();
  bool emitSysreqN();
  bool emitLegacyNativeCall(uint32_t native_index, NativeEntry* native);
  bool emitSysreqC();
  bool emitSwitch();
  void emitGenArray(bool autozero);
  void emitCheckAddress(Register reg);
  void emitFloatCmp(ConditionCode cc);
  void emitCallThunk(CallThunk* thunk);
  void jumpOnError(ConditionCode cc, int err = 0);

  ExternalAddress hpAddr() {
    return ExternalAddress(context_->addressOfHp());
  }
  ExternalAddress frmAddr() {
    return ExternalAddress(context_->addressOfFrm());
  }
  ExternalAddress spAddr() {
    return ExternalAddress(context_->addressOfSp());
  }
};

const Register pri = eax;
const Register alt = edx;
const Register stk = edi;
const Register dat = esi;
const Register tmp = ecx;
const Register frm = ebx;

}

#endif //_INCLUDE_SOURCEPAWN_JIT_X86_H_
