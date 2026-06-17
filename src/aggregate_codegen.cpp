#include "aggregate_codegen.h"

#include "error.h"
#include "types.h"

#include <cmath>

namespace far {

const char* aggLlvmType(FarTypeId id) {
  const AggregateMeta* m = aggregateMeta(id);
  return m ? m->llvm_name : "i64";
}

const char* aggScalarLlvm(FarTypeId scalar) {
  if (scalar == FarTypeId::F32)
    return "float";
  if (scalar == FarTypeId::F64)
    return "double";
  if (scalar == FarTypeId::I32)
    return "i32";
  if (scalar == FarTypeId::U8)
    return "i8";
  return "double";
}

static bool isIntegerAggScalar(FarTypeId scalar) {
  return scalar == FarTypeId::I32 || scalar == FarTypeId::U8;
}

std::string aggValueToPtr(AggCodegenCtx ctx, const std::string& val, FarTypeId type) {
  const char* st = aggLlvmType(type);
  std::string slot = ctx.fresh("agp");
  ctx.out << "  %" << slot << " = alloca " << st << "\n";
  ctx.out << "  store " << st << " " << val << ", " << st << "* %" << slot << "\n";
  return "%" + slot;
}

std::string loadAggValue(AggCodegenCtx ctx, const std::string& ptr, FarTypeId type) {
  const char* st = aggLlvmType(type);
  std::string res = ctx.fresh("agv");
  ctx.out << "  %" << res << " = load " << st << ", " << st << "* " << ptr << "\n";
  return "%" + res;
}

static std::string aggToPtr(AggCodegenCtx ctx, const std::string& val, FarTypeId type) {
  return aggValueToPtr(ctx, val, type);
}

static std::string loadFromPtr(AggCodegenCtx ctx, const std::string& ptr, FarTypeId type) {
  return loadAggValue(ctx, ptr, type);
}

static std::string emitScalarToAgg(AggCodegenCtx ctx, FarTypeId scalar, const std::string& val) {
  if (val.empty() || val[0] != '%')
    return val;
  std::string tmp = ctx.fresh("t");
  ctx.out << "  %" << tmp << " = fpext float " << val << " to double\n";
  if (scalar == FarTypeId::F32) {
    std::string f = ctx.fresh("t");
    ctx.out << "  %" << f << " = fptrunc double %" << tmp << " to float\n";
    return "%" + f;
  }
  return "%" + tmp;
}

static std::string emitToDouble(AggCodegenCtx ctx, FarTypeId ty, const std::string& val) {
  if (ty == FarTypeId::F32) {
    std::string tmp = ctx.fresh("t");
    ctx.out << "  %" << tmp << " = fpext float " << val << " to double\n";
    return "%" + tmp;
  }
  if (ty == FarTypeId::F64)
    return val;
  std::string tmp = ctx.fresh("t");
  if (isIntegerType(ty) && !typeInfo(ty).is_signed)
    ctx.out << "  %" << tmp << " = uitofp i64 " << val << " to double\n";
  else
    ctx.out << "  %" << tmp << " = sitofp i64 " << val << " to double\n";
  return "%" + tmp;
}

static std::string loadField(AggCodegenCtx ctx, const std::string& agg, FarTypeId type, int field) {
  const char* st = aggLlvmType(type);
  const char* sc = aggScalarLlvm(aggregateScalar(type));
  std::string slot = ctx.fresh("fslot");
  ctx.out << "  %" << slot << " = alloca " << st << "\n";
  ctx.out << "  store " << st << " " << agg << ", " << st << "* %" << slot << "\n";
  std::string ptr = ctx.fresh("fptr");
  ctx.out << "  %" << ptr << " = getelementptr inbounds " << st << ", " << st << "* %" << slot << ", i32 0, i32 "
          << field << "\n";
  std::string tmp = ctx.fresh("t");
  ctx.out << "  %" << tmp << " = load " << sc << ", " << sc << "* %" << ptr << "\n";
  FarTypeId scalar = aggregateScalar(type);
  if (scalar == FarTypeId::F32) {
    std::string ext = ctx.fresh("t");
    ctx.out << "  %" << ext << " = fpext float %" << tmp << " to double\n";
    return "%" + ext;
  }
  if (scalar == FarTypeId::I32) {
    std::string ext = ctx.fresh("t");
    ctx.out << "  %" << ext << " = sext i32 %" << tmp << " to i64\n";
    return "%" + ext;
  }
  if (scalar == FarTypeId::U8) {
    std::string ext = ctx.fresh("t");
    ctx.out << "  %" << ext << " = zext i8 %" << tmp << " to i64\n";
    return "%" + ext;
  }
  return "%" + tmp;
}

static std::string storeFields(AggCodegenCtx ctx, FarTypeId type,
                               const std::vector<std::string>& components) {
  const AggregateMeta* m = aggregateMeta(type);
  const char* st = m->llvm_name;
  const char* sc = aggScalarLlvm(m->scalar);
  std::string slot = ctx.fresh("aslot");
  ctx.out << "  %" << slot << " = alloca " << st << "\n";
  for (int i = 0; i < m->nfields; ++i) {
    std::string ptr = ctx.fresh("aptr");
    ctx.out << "  %" << ptr << " = getelementptr inbounds " << st << ", " << st << "* %" << slot
            << ", i32 0, i32 " << i << "\n";
    std::string comp = components[static_cast<size_t>(i)];
    if (m->scalar == FarTypeId::F32 && comp[0] == '%') {
      std::string f = ctx.fresh("t");
      ctx.out << "  %" << f << " = fptrunc double " << comp << " to float\n";
      comp = "%" + f;
    } else if (m->scalar == FarTypeId::I32 && comp[0] == '%') {
      std::string f = ctx.fresh("t");
      ctx.out << "  %" << f << " = trunc i64 " << comp << " to i32\n";
      comp = "%" + f;
    } else if (m->scalar == FarTypeId::U8 && comp[0] == '%') {
      std::string f = ctx.fresh("t");
      ctx.out << "  %" << f << " = trunc i64 " << comp << " to i8\n";
      comp = "%" + f;
    }
    ctx.out << "  store " << sc << " " << comp << ", " << sc << "* %" << ptr << "\n";
  }
  std::string res = ctx.fresh("t");
  ctx.out << "  %" << res << " = load " << st << ", " << st << "* %" << slot << "\n";
  return "%" + res;
}

std::string emitAggregateConstruct(AggCodegenCtx ctx, FarTypeId type, const std::vector<Expr*>& args) {
  std::vector<std::string> comps;
  FarTypeId sc = aggregateScalar(type);
  for (Expr* a : args) {
    if (isIntegerAggScalar(sc))
      comps.push_back(ctx.emit_as_i64(*a));
    else
      comps.push_back(ctx.emit_as_double(*a));
  }
  return storeFields(ctx, type, comps);
}

std::string emitAggregateMember(AggCodegenCtx ctx, const MemberAccess& mem, FarTypeId obj_ty) {
  std::string agg = ctx.emit_expr(*mem.object);
  int idx = lookupFieldIndex(obj_ty, mem.member);
  if (idx < 0)
    throw FarError("unknown field");
  return loadField(ctx, agg, obj_ty, idx);
}

static std::string componentWiseBinOp(AggCodegenCtx ctx, FarTypeId type, const std::string& op,
                                      const std::string& left, const std::string& right) {
  const AggregateMeta* m = aggregateMeta(type);
  std::vector<std::string> lcomps, rcomps;
  for (int i = 0; i < m->nfields; ++i) {
    lcomps.push_back(loadField(ctx, left, type, i));
    rcomps.push_back(loadField(ctx, right, type, i));
  }
  const bool as_int = isIntegerAggScalar(m->scalar);
  std::vector<std::string> out;
  for (int i = 0; i < m->nfields; ++i) {
    std::string tmp = ctx.fresh("t");
    if (as_int) {
      if (op == "/") {
        ctx.out << "  %" << tmp << " = call i64 @far_i64_div_checked(i64 "
                << lcomps[static_cast<size_t>(i)] << ", i64 " << rcomps[static_cast<size_t>(i)] << ")\n";
      } else {
        const char* sop = op == "+"   ? "add"
                          : op == "-" ? "sub"
                          : op == "*" ? "mul"
                                      : "add";
        ctx.out << "  %" << tmp << " = " << sop << " i64 " << lcomps[static_cast<size_t>(i)] << ", "
                << rcomps[static_cast<size_t>(i)] << "\n";
      }
    } else if (op == "**") {
      ctx.out << "  %" << tmp << " = call double @far_pow(double " << lcomps[static_cast<size_t>(i)]
              << ", double " << rcomps[static_cast<size_t>(i)] << ")\n";
    } else if (op == "//") {
      std::string q = ctx.fresh("t");
      ctx.out << "  %" << q << " = fdiv double " << lcomps[static_cast<size_t>(i)] << ", "
              << rcomps[static_cast<size_t>(i)] << "\n";
      ctx.out << "  %" << tmp << " = call double @far_floor(double %" << q << ")\n";
    } else {
      const char* sop = op == "+"   ? "fadd"
                        : op == "-" ? "fsub"
                        : op == "*" ? "fmul"
                        : op == "/" ? "fdiv"
                                    : "fadd";
      ctx.out << "  %" << tmp << " = " << sop << " double " << lcomps[static_cast<size_t>(i)] << ", "
              << rcomps[static_cast<size_t>(i)] << "\n";
    }
    out.push_back("%" + tmp);
  }
  return storeFields(ctx, type, out);
}

static std::string scaleAggregate(AggCodegenCtx ctx, FarTypeId type, const std::string& agg,
                                  const std::string& scalar, const std::string& op,
                                  bool scalar_on_left = false) {
  const AggregateMeta* m = aggregateMeta(type);
  std::vector<std::string> out;
  for (int i = 0; i < m->nfields; ++i) {
    std::string c = loadField(ctx, agg, type, i);
    std::string tmp = ctx.fresh("t");
    const std::string& lhs = scalar_on_left ? scalar : c;
    const std::string& rhs = scalar_on_left ? c : scalar;
    if (isIntegerAggScalar(m->scalar)) {
      if (op == "/") {
        std::string tmp = ctx.fresh("t");
        ctx.out << "  %" << tmp << " = call i64 @far_i64_div_checked(i64 " << lhs << ", i64 " << rhs
                << ")\n";
        out.push_back("%" + tmp);
        continue;
      }
      const char* sop = op == "+"   ? "add"
                        : op == "-" ? "sub"
                        : op == "*" ? "mul"
                        : op == "/" ? "sdiv"
                                    : "add";
      ctx.out << "  %" << tmp << " = " << sop << " i64 " << lhs << ", " << rhs << "\n";
    } else {
      const char* sop = op == "+"   ? "fadd"
                        : op == "-" ? "fsub"
                        : op == "*" ? "fmul"
                        : op == "/" ? "fdiv"
                                    : "fadd";
      ctx.out << "  %" << tmp << " = " << sop << " double " << lhs << ", " << rhs << "\n";
    }
    out.push_back("%" + tmp);
  }
  return storeFields(ctx, type, out);
}

static FarTypeId binOpResultType(FarTypeId lt, FarTypeId rt) {
  if (lt == rt)
    return lt;
  if (isPointFamily(lt) && isVecFamily(rt) && aggregateScalar(lt) == aggregateScalar(rt))
    return lt;
  if (isVecFamily(lt) && isPointFamily(rt) && aggregateScalar(lt) == aggregateScalar(rt))
    return rt;
  if (isPointFamily(lt) && isPointFamily(rt))
    return vecTypeForDim(aggregateScalar(lt), 2);
  return lt;
}

std::string emitAggregateBinOp(AggCodegenCtx ctx, const std::string& op, FarTypeId lt, FarTypeId rt,
                               const std::string& left, const std::string& right) {
  if (isAggregateType(lt) && isAggregateType(rt)) {
    FarTypeId res = binOpResultType(lt, rt);
    if (aggregateDim(lt) == aggregateDim(rt) && aggregateScalar(lt) == aggregateScalar(rt))
      return componentWiseBinOp(ctx, res, op, left, right);
  }

  if (isAggregateType(lt) && !isAggregateType(rt)) {
  FarTypeId sc = aggregateScalar(lt);
    std::string s = isIntegerAggScalar(sc) ? right : emitToDouble(ctx, rt, right);
    return scaleAggregate(ctx, lt, left, s, op);
  }
  if (!isAggregateType(lt) && isAggregateType(rt)) {
    FarTypeId sc = aggregateScalar(rt);
    std::string s = isIntegerAggScalar(sc) ? left : emitToDouble(ctx, lt, left);
    return scaleAggregate(ctx, rt, right, s, op, true);
  }
  throw FarError("invalid aggregate binop");
}

std::string emitUnaryAggregate(AggCodegenCtx ctx, const std::string& op, FarTypeId ty,
                               const std::string& val) {
  const AggregateMeta* m = aggregateMeta(ty);
  std::vector<std::string> out;
  for (int i = 0; i < m->nfields; ++i) {
    std::string c = loadField(ctx, val, ty, i);
    std::string tmp = ctx.fresh("t");
    if (isIntegerAggScalar(m->scalar)) {
      if (op == "-")
        ctx.out << "  %" << tmp << " = sub i64 0, " << c << "\n";
      else
        ctx.out << "  %" << tmp << " = xor i64 " << c << ", -1\n";
    } else if (op == "-") {
      ctx.out << "  %" << tmp << " = fsub double 0.0, " << c << "\n";
    } else {
      ctx.out << "  %" << tmp << " = call double @far_fabs(double " << c << ")\n";
    }
    out.push_back("%" + tmp);
  }
  return storeFields(ctx, ty, out);
}

std::string emitAggregateCompare(AggCodegenCtx ctx, const std::string& op, FarTypeId ty,
                                 const std::string& left, const std::string& right) {
  const AggregateMeta* m = aggregateMeta(ty);
  const bool as_int = isIntegerAggScalar(m->scalar);
  std::string pred = op == "==" ? (as_int ? "eq" : "oeq") : (as_int ? "ne" : "one");
  std::string acc = "1";
  for (int i = 0; i < m->nfields; ++i) {
    std::string l = loadField(ctx, left, ty, i);
    std::string r = loadField(ctx, right, ty, i);
    std::string cmp = ctx.fresh("t");
    if (as_int)
      ctx.out << "  %" << cmp << " = icmp " << pred << " i64 " << l << ", " << r << "\n";
    else
      ctx.out << "  %" << cmp << " = fcmp " << pred << " double " << l << ", " << r << "\n";
    std::string z = ctx.fresh("t");
    ctx.out << "  %" << z << " = zext i1 %" << cmp << " to i64\n";
    if (i == 0)
      acc = "%" + z;
    else {
      std::string andv = ctx.fresh("t");
      ctx.out << "  %" << andv << " = and i64 " << acc << ", %" << z << "\n";
      acc = "%" + andv;
    }
  }
  return acc;
}

static std::string rtMethod(FarTypeId type, const char* base) {
  const AggregateMeta* m = aggregateMeta(type);
  static std::string buf;
  buf = std::string("far_") + m->name + "_" + base;
  return buf;
}

static std::string emitAggregateMethodImpl(AggCodegenCtx ctx, const std::string& method, Expr& recv_expr,
                                             const std::vector<Expr*>& arg_exprs, FarTypeId obj_ty,
                                             FarTypeId ret_ty) {
  const MethodInfo* mi = lookupMethod(obj_ty, method);
  if (!mi)
    throw FarError("unknown method '" + method + "' on aggregate " + typeInfo(obj_ty).name);
  std::string recv = ctx.emit_expr(recv_expr);
  const char* st = aggLlvmType(obj_ty);
  const char* rt_st = aggLlvmType(ret_ty);

  std::string recv_ptr = aggToPtr(ctx, recv, obj_ty);

  switch (mi->id) {
    case AggMethodId::Length2: {
      std::string tmp = ctx.fresh("t");
      if (isIVecFamily(obj_ty))
        ctx.out << "  %" << tmp << " = call i64 @" << rtMethod(obj_ty, "length2") << "(" << st << "* "
                << recv_ptr << ")\n";
      else
        ctx.out << "  %" << tmp << " = call double @" << rtMethod(obj_ty, "length2") << "(" << st << "* "
                << recv_ptr << ")\n";
      return "%" + tmp;
    }
    case AggMethodId::Length: {
      std::string tmp = ctx.fresh("t");
      ctx.out << "  %" << tmp << " = call double @" << rtMethod(obj_ty, "length") << "(" << st << "* "
              << recv_ptr << ")\n";
      return "%" + tmp;
    }
    case AggMethodId::Dot: {
      std::string other = ctx.emit_expr(*arg_exprs[0]);
      std::string other_ptr = aggToPtr(ctx, other, obj_ty);
      std::string tmp = ctx.fresh("t");
      if (isIVecFamily(obj_ty))
        ctx.out << "  %" << tmp << " = call i64 @" << rtMethod(obj_ty, "dot") << "(" << st << "* " << recv_ptr
                << ", " << st << "* " << other_ptr << ")\n";
      else
        ctx.out << "  %" << tmp << " = call double @" << rtMethod(obj_ty, "dot") << "(" << st << "* " << recv_ptr
                << ", " << st << "* " << other_ptr << ")\n";
      return "%" + tmp;
    }
    case AggMethodId::Normalize: {
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca " << st << "\n";
      ctx.out << "  call void @" << rtMethod(obj_ty, "normalize") << "(" << st << "* " << recv_ptr << ", "
              << st << "* %" << out_slot << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, obj_ty);
    }
    case AggMethodId::Cross: {
      std::string other = ctx.emit_expr(*arg_exprs[0]);
      std::string other_ptr = aggToPtr(ctx, other, obj_ty);
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca " << st << "\n";
      ctx.out << "  call void @" << rtMethod(obj_ty, "cross") << "(" << st << "* " << recv_ptr << ", " << st
              << "* " << other_ptr << ", " << st << "* %" << out_slot << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, obj_ty);
    }
    case AggMethodId::Distance2:
    case AggMethodId::Distance:
    case AggMethodId::DistanceTo: {
      std::string other = ctx.emit_expr(*arg_exprs[0]);
      std::string d2 = ctx.fresh("t");
      const char* fn = mi->id == AggMethodId::Distance2 ? "distance2"
                       : mi->id == AggMethodId::DistanceTo ? "distance_to"
                                                           : "distance";
      std::string other_ptr = aggToPtr(ctx, other, obj_ty);
      if (mi->id == AggMethodId::Distance) {
        ctx.out << "  %" << d2 << " = call double @" << rtMethod(obj_ty, "distance") << "(" << st << "* "
                << recv_ptr << ", " << st << "* " << other_ptr << ")\n";
        return "%" + d2;
      }
      ctx.out << "  %" << d2 << " = call double @" << rtMethod(obj_ty, fn) << "(" << st << "* " << recv_ptr
              << ", " << st << "* " << other_ptr << ")\n";
      if (mi->id == AggMethodId::Distance2)
        return "%" + d2;
      std::string tmp = ctx.fresh("t");
      ctx.out << "  %" << tmp << " = call double @far_sqrt(double %" << d2 << ")\n";
      return "%" + tmp;
    }
    case AggMethodId::Min:
    case AggMethodId::Max:
    case AggMethodId::Clamp: {
      std::vector<std::string> args;
      args.push_back(recv);
      for (const auto& a : arg_exprs)
        args.push_back(ctx.emit_expr(*a));
      if (mi->id == AggMethodId::Min || mi->id == AggMethodId::Max) {
        std::vector<std::string> out;
        const bool as_int = isIVecFamily(obj_ty);
        const char* fn = mi->id == AggMethodId::Min ? (as_int ? "far_imin" : "far_fmin")
                                                    : (as_int ? "far_imax" : "far_fmax");
        for (int i = 0; i < aggregateDim(obj_ty); ++i) {
          std::string a = loadField(ctx, recv, obj_ty, i);
          std::string b = loadField(ctx, args[1], obj_ty, i);
          std::string tmp = ctx.fresh("t");
          if (as_int)
            ctx.out << "  %" << tmp << " = call i64 @" << fn << "(i64 " << a << ", " << b << ")\n";
          else
            ctx.out << "  %" << tmp << " = call double @" << fn << "(double " << a << ", " << b << ")\n";
          out.push_back("%" + tmp);
        }
        return storeFields(ctx, obj_ty, out);
      }
      std::string lo = loadField(ctx, args[1], obj_ty, 0);
      std::string hi = loadField(ctx, args[2], obj_ty, 0);
      std::vector<std::string> out;
      for (int i = 0; i < aggregateDim(obj_ty); ++i) {
        std::string c = loadField(ctx, recv, obj_ty, i);
        std::string cl = loadField(ctx, args[1], obj_ty, i);
        std::string ch = loadField(ctx, args[2], obj_ty, i);
        std::string mx = ctx.fresh("t");
        ctx.out << "  %" << mx << " = call double @far_fmax(double " << cl << ", " << c << ")\n";
        std::string tmp = ctx.fresh("t");
        ctx.out << "  %" << tmp << " = call double @far_fmin(double %" << mx << ", " << ch << ")\n";
        out.push_back("%" + tmp);
      }
      return storeFields(ctx, obj_ty, out);
    }
    case AggMethodId::ApproxEq: {
      std::string other = ctx.emit_expr(*arg_exprs[0]);
      std::string other_ptr = aggToPtr(ctx, other, obj_ty);
      std::string eps = ctx.emit_as_double(*arg_exprs[1]);
      std::string d2 = ctx.fresh("t");
      ctx.out << "  %" << d2 << " = call double @" << rtMethod(obj_ty, "distance2") << "(" << st << "* "
              << recv_ptr << ", " << st << "* " << other_ptr << ")\n";
      std::string eps2 = ctx.fresh("t");
      ctx.out << "  %" << eps2 << " = fmul double " << eps << ", " << eps << "\n";
      std::string cmp = ctx.fresh("t");
      ctx.out << "  %" << cmp << " = fcmp ole double %" << d2 << ", %" << eps2 << "\n";
      std::string z = ctx.fresh("t");
      ctx.out << "  %" << z << " = zext i1 %" << cmp << " to i64\n";
      return "%" + z;
    }
    case AggMethodId::Translate: {
      std::string off = ctx.emit_expr(*arg_exprs[0]);
      return componentWiseBinOp(ctx, obj_ty, "+", recv, off);
    }
    case AggMethodId::Width: {
      std::string xmax = loadField(ctx, recv, obj_ty, 2);
      std::string xmin = loadField(ctx, recv, obj_ty, 0);
      std::string tmp = ctx.fresh("t");
      ctx.out << "  %" << tmp << " = fsub double " << xmax << ", " << xmin << "\n";
      return "%" + tmp;
    }
    case AggMethodId::Height: {
      std::string ymax = loadField(ctx, recv, obj_ty, 3);
      std::string ymin = loadField(ctx, recv, obj_ty, 1);
      std::string tmp = ctx.fresh("t");
      ctx.out << "  %" << tmp << " = fsub double " << ymax << ", " << ymin << "\n";
      return "%" + tmp;
    }
    case AggMethodId::Area: {
      std::string width = ctx.fresh("t");
      std::string xmax = loadField(ctx, recv, obj_ty, 2);
      std::string xmin = loadField(ctx, recv, obj_ty, 0);
      ctx.out << "  %" << width << " = fsub double " << xmax << ", " << xmin << "\n";
      std::string ymax = loadField(ctx, recv, obj_ty, 3);
      std::string ymin = loadField(ctx, recv, obj_ty, 1);
      std::string height = ctx.fresh("t");
      ctx.out << "  %" << height << " = fsub double " << ymax << ", " << ymin << "\n";
      std::string tmp = ctx.fresh("t");
      ctx.out << "  %" << tmp << " = fmul double %" << width << ", %" << height << "\n";
      return "%" + tmp;
    }
    case AggMethodId::Center: {
      if (isBoundsFamily(obj_ty)) {
        std::string out_slot = ctx.fresh("out");
        ctx.out << "  %" << out_slot << " = alloca %FarFVec3\n";
        ctx.out << "  call void @far_bounds_center(" << st << "* " << recv_ptr << ", %FarFVec3* %" << out_slot
                << ")\n";
        return loadFromPtr(ctx, "%" + out_slot, FarTypeId::FVec3);
      }
      FarTypeId pt = pointTypeForScalar(aggregateScalar(obj_ty));
      const char* pt_st = aggLlvmType(pt);
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca " << pt_st << "\n";
      ctx.out << "  call void @" << rtMethod(obj_ty, "center") << "(" << st << "* " << recv_ptr << ", "
              << pt_st << "* %" << out_slot << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, pt);
    }
    case AggMethodId::Contains: {
      std::string p = ctx.emit_expr(*arg_exprs[0]);
      FarTypeId arg_ty = ctx.expr_type(*arg_exprs[0]);
      std::string tmp = ctx.fresh("t");
      if (isBoundsFamily(obj_ty)) {
        std::string p_ptr = aggToPtr(ctx, p, FarTypeId::FVec3);
        ctx.out << "  %" << tmp << " = call i64 @far_bounds_contains(" << st << "* " << recv_ptr
                << ", %FarFVec3* " << p_ptr << ")\n";
        return "%" + tmp;
      }
      if (isRectFamily(obj_ty) && isVecFamily(arg_ty) && aggregateScalar(obj_ty) == FarTypeId::F32) {
        std::string p_ptr = aggToPtr(ctx, p, FarTypeId::FVec2);
        ctx.out << "  %" << tmp << " = call i64 @far_frect_contains_vec(" << st << "* " << recv_ptr
                << ", %FarFVec2* " << p_ptr << ")\n";
        return "%" + tmp;
      }
      FarTypeId pt = pointTypeForScalar(aggregateScalar(obj_ty));
      std::string p_ptr = aggToPtr(ctx, p, pt);
      ctx.out << "  %" << tmp << " = call i64 @" << rtMethod(obj_ty, "contains") << "(" << st << "* "
              << recv_ptr << ", " << aggLlvmType(pt) << "* " << p_ptr << ")\n";
      return "%" + tmp;
    }
    case AggMethodId::Intersects: {
      std::string other = ctx.emit_expr(*arg_exprs[0]);
      std::string other_ptr = aggToPtr(ctx, other, obj_ty);
      std::string tmp = ctx.fresh("t");
      if (isBoundsFamily(obj_ty))
        ctx.out << "  %" << tmp << " = call i64 @far_bounds_intersects(" << st << "* " << recv_ptr << ", " << st
                << "* " << other_ptr << ")\n";
      else
        ctx.out << "  %" << tmp << " = call i64 @" << rtMethod(obj_ty, "intersects") << "(" << st << "* "
                << recv_ptr << ", " << st << "* " << other_ptr << ")\n";
      return "%" + tmp;
    }
    case AggMethodId::Expand: {
      std::string margin = isBoundsFamily(obj_ty) ? ctx.emit_as_double(*arg_exprs[0])
                                                  : ctx.emit_as_double(*arg_exprs[0]);
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca " << st << "\n";
      const char* sc = aggScalarLlvm(aggregateScalar(obj_ty));
      ctx.out << "  call void @" << rtMethod(obj_ty, "expand") << "(" << st << "* " << recv_ptr << ", " << sc
              << " " << margin << ", " << st << "* %" << out_slot << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, obj_ty);
    }
    case AggMethodId::Transpose: {
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca " << st << "\n";
      ctx.out << "  call void @" << rtMethod(obj_ty, "transpose") << "(" << st << "* " << recv_ptr << ", " << st
              << "* %" << out_slot << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, obj_ty);
    }
    case AggMethodId::Determinant: {
      std::string tmp = ctx.fresh("t");
      ctx.out << "  %" << tmp << " = call double @" << rtMethod(obj_ty, "determinant") << "(" << st << "* "
              << recv_ptr << ")\n";
      return "%" + tmp;
    }
    case AggMethodId::MatMul: {
      FarTypeId arg_ty = ctx.expr_type(*arg_exprs[0]);
      std::string other = ctx.emit_expr(*arg_exprs[0]);
      if (isMatFamily(arg_ty)) {
        std::string other_ptr = aggToPtr(ctx, other, arg_ty);
        std::string out_slot = ctx.fresh("out");
        ctx.out << "  %" << out_slot << " = alloca " << st << "\n";
        ctx.out << "  call void @" << rtMethod(obj_ty, "mul_mat") << "(" << st << "* " << recv_ptr << ", " << st
                << "* " << other_ptr << ", " << st << "* %" << out_slot << ")\n";
        return loadFromPtr(ctx, "%" + out_slot, obj_ty);
      }
      std::string vec_ty_name = aggregateMeta(vecTypeForDim(aggregateScalar(obj_ty), aggregateMatDim(obj_ty)))->llvm_name;
      std::string other_ptr = aggToPtr(ctx, other, vecTypeForDim(aggregateScalar(obj_ty), aggregateMatDim(obj_ty)));
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca " << vec_ty_name << "\n";
      ctx.out << "  call void @" << rtMethod(obj_ty, "mul_vec") << "(" << st << "* " << recv_ptr << ", "
              << vec_ty_name << "* " << other_ptr << ", " << vec_ty_name << "* %" << out_slot << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, vecTypeForDim(aggregateScalar(obj_ty), aggregateMatDim(obj_ty)));
    }
    case AggMethodId::QuatMul: {
      std::string other = ctx.emit_expr(*arg_exprs[0]);
      std::string other_ptr = aggToPtr(ctx, other, obj_ty);
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca " << st << "\n";
      ctx.out << "  call void @" << rtMethod(obj_ty, "mul") << "(" << st << "* " << recv_ptr << ", " << st
              << "* " << other_ptr << ", " << st << "* %" << out_slot << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, obj_ty);
    }
    case AggMethodId::ToColor: {
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca %FarColor\n";
      ctx.out << "  call void @far_color32_to_color(" << st << "* " << recv_ptr << ", %FarColor* %" << out_slot
              << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, FarTypeId::Color);
    }
    case AggMethodId::BoundsSize: {
      std::string out_slot = ctx.fresh("out");
      ctx.out << "  %" << out_slot << " = alloca %FarFVec3\n";
      ctx.out << "  call void @far_bounds_size(" << st << "* " << recv_ptr << ", %FarFVec3* %" << out_slot
              << ")\n";
      return loadFromPtr(ctx, "%" + out_slot, FarTypeId::FVec3);
    }
    default:
      throw FarError("unimplemented aggregate method");
  }
}

std::string emitAggregateMethod(AggCodegenCtx ctx, const MethodCall& call, FarTypeId obj_ty,
                                FarTypeId ret_ty) {
  std::vector<Expr*> arg_exprs;
  for (const auto& a : call.args)
    arg_exprs.push_back(a.get());
  return emitAggregateMethodImpl(ctx, call.method, *call.object, arg_exprs, obj_ty, ret_ty);
}

std::string emitAggregateStaticCall(AggCodegenCtx ctx, const MethodCall& call, FarTypeId obj_ty,
                                    FarTypeId ret_ty) {
  if (call.args.empty())
    throw FarError("geometry static call missing receiver argument");
  std::vector<Expr*> arg_exprs;
  for (size_t i = 1; i < call.args.size(); ++i)
    arg_exprs.push_back(call.args[i].get());
  return emitAggregateMethodImpl(ctx, call.method, *call.args[0], arg_exprs, obj_ty, ret_ty);
}

void emitAggregatePrint(AggCodegenCtx ctx, FarTypeId ty, const std::string& val) {
  const AggregateMeta* m = aggregateMeta(ty);
  if (!m || !m->print_rt)
    return;
  std::string ptr = aggToPtr(ctx, val, ty);
  ctx.out << "  call void @far_print_" << m->print_rt << "(" << m->llvm_name << "* " << ptr << ")\n";
}

}  // namespace far
