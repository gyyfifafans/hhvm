/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-present Facebook, Inc. (http://www.facebook.com)  |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include "hphp/runtime/vm/jit/irlower-internal.h"

#include "hphp/runtime/base/rds.h"
#include "hphp/runtime/base/runtime-option.h"
#include "hphp/runtime/vm/class.h"
#include "hphp/runtime/vm/func.h"
#include "hphp/runtime/vm/preclass.h"

#include "hphp/runtime/vm/jit/types.h"
#include "hphp/runtime/vm/jit/abi.h"
#include "hphp/runtime/vm/jit/code-gen-helpers.h"
#include "hphp/runtime/vm/jit/extra-data.h"
#include "hphp/runtime/vm/jit/ir-instruction.h"
#include "hphp/runtime/vm/jit/ssa-tmp.h"
#include "hphp/runtime/vm/jit/type.h"
#include "hphp/runtime/vm/jit/vasm-gen.h"
#include "hphp/runtime/vm/jit/vasm-instr.h"
#include "hphp/runtime/vm/jit/vasm-reg.h"

#include "hphp/util/trace.h"

namespace HPHP { namespace jit { namespace irlower {

TRACE_SET_MOD(irlower);

///////////////////////////////////////////////////////////////////////////////

void cgLdClsName(IRLS& env, const IRInstruction* inst) {
  auto const dst = dstLoc(env, inst, 0).reg();
  auto const src = srcLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  auto const preclass = v.makeReg();
  v << load{src[Class::preClassOff()], preclass};
  emitLdLowPtr(v, preclass[PreClass::nameOffset()],
               dst, sizeof(LowStringPtr));
}

IMPL_OPCODE_CALL(MethodExists)

void cgLdClsMethod(IRLS& env, const IRInstruction* inst) {
  auto const dst = dstLoc(env, inst, 0).reg();
  auto const cls = srcLoc(env, inst, 0).reg();
  int32_t const mSlotVal = inst->src(1)->rawVal();

  // We could have a Cls or a Cctx.  The Cctx has the low bit set, so
  // we need to subtract one in that case.
  auto const methOff = int32_t(mSlotVal * sizeof(LowPtr<Func>)) -
    (inst->src(0)->isA(TCctx) ? ActRec::kHasClassBit : 0);
  auto& v = vmain(env);
  emitLdLowPtr(v, cls[methOff], dst, sizeof(LowPtr<Func>));
}

void cgLdIfaceMethod(IRLS& env, const IRInstruction* inst) {
  auto const extra = inst->extra<LdIfaceMethod>();
  auto const func = dstLoc(env, inst, 0).reg();
  auto const cls = srcLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  auto const vtable_vec = v.makeReg();
  auto const vtable = v.makeReg();

  emitLdLowPtr(v, cls[Class::vtableVecOff()],
               vtable_vec, sizeof(LowPtr<Class::VtableVecSlot>));
  auto const vtableOff = extra->vtableIdx * sizeof(Class::VtableVecSlot) +
                         offsetof(Class::VtableVecSlot, vtable);
  emitLdLowPtr(v, vtable_vec[vtableOff], vtable,
               sizeof(Class::VtableVecSlot::vtable));
  emitLdLowPtr(v, vtable[extra->methodIdx * sizeof(LowPtr<Func>)],
               func, sizeof(LowPtr<Func>));
}

void cgLdObjInvoke(IRLS& env, const IRInstruction* inst) {
  auto const dst = dstLoc(env, inst, 0).reg();
  auto const cls = srcLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  emitLdLowPtr(v, cls[Class::invokeOff()], dst, sizeof(LowPtr<Func>));

  auto const sf = v.makeReg();
  v << testq{dst, dst, sf};
  v << jcc{CC_Z, sf, {label(env, inst->next()), label(env, inst->taken())}};
}

IMPL_OPCODE_CALL(HasToString);

void cgLdFuncVecLen(IRLS& env, const IRInstruction* inst) {
  auto const dst = dstLoc(env, inst, 0).reg();
  auto const cls = srcLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  // A Cctx is a Cls with the bottom bit set; subtract one from the offset to
  // handle that case.
  auto const off = Class::funcVecLenOff() -
    (inst->src(0)->isA(TCctx) ? ActRec::kHasClassBit : 0);

  static_assert(sizeof(Class::veclen_t) == 2 || sizeof(Class::veclen_t) == 4,
                "Class::veclen_t must be 2 or 4 bytes wide");
  if (sizeof(Class::veclen_t) == 2) {
    auto const tmp = v.makeReg();
    v << loadw{cls[off], tmp};
    v << movzwq{tmp, dst};
  } else {
    v << loadzlq{cls[off], dst};
  }
}

///////////////////////////////////////////////////////////////////////////////

void cgLdClsInitData(IRLS& env, const IRInstruction* inst) {
  auto const dst = dstLoc(env, inst, 0).reg();
  auto const cls = srcLoc(env, inst, 0).reg();
  auto const offset = Class::propDataCacheOff() +
    rds::Link<Class::PropInitVec*, rds::Mode::Normal>::handleOff();
  auto& v = vmain(env);

  auto const handle = v.makeReg();
  auto const vec = v.makeReg();
  v << loadzlq{cls[offset], handle};
  v << load{Vreg(rvmtl())[handle], vec};
  v << load{vec[Class::PropInitVec::dataOff()], dst};
}

void cgPropTypeRedefineCheck(IRLS& env, const IRInstruction* inst) {
  auto const cls = inst->src(0)->clsVal();
  auto const slot = inst->src(1)->intVal();
  assertx(RuntimeOption::EvalCheckPropTypeHints > 0);
  assertx(cls->maybeRedefinesPropTypes());
  assertx(slot != kInvalidSlot);
  assertx(slot < cls->numDeclProperties());
  cgCallHelper(
    vmain(env),
    env,
    CallSpec::method(&Class::checkPropTypeRedefinition),
    kVoidDest,
    SyncOptions::Sync,
    argGroup(env, inst).immPtr(cls).imm(slot)
  );
}

IMPL_OPCODE_CALL(InitProps)
IMPL_OPCODE_CALL(InitSProps)

///////////////////////////////////////////////////////////////////////////////

void cgLookupSPropSlot(IRLS& env, const IRInstruction* inst) {
  cgCallHelper(
    vmain(env),
    env,
    CallSpec::method(&Class::lookupSProp),
    callDest(env, inst),
    SyncOptions::None,
    argGroup(env, inst).ssa(0).ssa(1)
  );
}

///////////////////////////////////////////////////////////////////////////////

void cgLdFuncNumParams(IRLS& env, const IRInstruction* inst) {
  auto const dst = dstLoc(env, inst, 0).reg();
  auto const func = srcLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  auto const tmp = v.makeReg();
  // See Func::finishedEmittingParams and Func::numParams.
  v << loadzlq{func[Func::paramCountsOff()], tmp};
  v << shrqi{1, tmp, dst, v.makeReg()};
}

void cgLdFuncName(IRLS& env, const IRInstruction* inst) {
  auto const dst = dstLoc(env, inst, 0).reg();
  auto const func = srcLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  v << loadzlq{func[Func::nameOff()], dst};
}

void cgFuncSupportsAsyncEagerReturn(IRLS& env, const IRInstruction* inst) {
  auto const func = srcLoc(env, inst, 0).reg();
  auto const dst = dstLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  auto const sf = v.makeReg();
  v << testlim{
    static_cast<int32_t>(AttrSupportsAsyncEagerReturn),
    func[Func::attrsOff()],
    sf
  };
  v << setcc{CC_NZ, sf, dst};
}

void cgIsFuncDynCallable(IRLS& env, const IRInstruction* inst) {
  auto const func = srcLoc(env, inst, 0).reg();
  auto const dst = dstLoc(env, inst, 0).reg();
  auto& v = vmain(env);

  auto const sf = v.makeReg();
  v << testlim{
    static_cast<int32_t>(AttrDynamicallyCallable),
    func[Func::attrsOff()],
    sf
  };
  v << setcc{CC_NZ, sf, dst};
}

///////////////////////////////////////////////////////////////////////////////

}}}
