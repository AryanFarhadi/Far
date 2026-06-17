#include "codegen.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "target.h"

#include "aggregate.h"
#include "aggregate_codegen.h"
#include "builtins.h"
#include "far_io.h"
#include "far_modern.h"
#include "far_perf.h"
#include "far_security.h"
#include "far_net.h"
#include "far_science.h"
#include "far_stdlib.h"
#include "collection_codegen.h"
#include "collections.h"
#include "comptime.h"
#include "error.h"
#include "functions.h"
#include "memory.h"
#include "memory_codegen.h"
#include "concurrency.h"
#include "concurrency_codegen.h"
#include "errors.h"
#include "errors_codegen.h"
#include "generics.h"
#include "pattern.h"
#include "pattern_codegen.h"
#include "string_codegen.h"
#include "string_methods.h"
#include "object_codegen.h"
#include "object_model.h"
#include "type_desc.h"
#include "types.h"

#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace far {

static const char* I64 = "i64";
static const char* F64 = "double";
static const char* F32 = "float";
static const char* F16 = "half";

static const char* llvmAbiType(FarTypeId id) { return typeInfo(id).llvm; }

static bool isPrimTy(const TypeDesc& td, FarTypeId id) {
  return isPrimitiveDesc(td) && td.primitive == id;
}

static FarTypeId primTy(const TypeDesc& td) {
  return isPrimitiveDesc(td) ? td.primitive : FarTypeId::I64;
}

static const char* llvmAbiTypeDesc(const TypeDesc& td) {
  if (td.form == TypeForm::Range || td.form == TypeForm::Span)
    return I64;
  if (!isPrimitiveDesc(td) || td.form == TypeForm::Array || isCollectionHandle(td))
    return I64;
  if (isAggregateType(td.primitive))
    return aggLlvmType(td.primitive);
  if (td.primitive == FarTypeId::Arr)
    return I64;
  return typeInfo(td.primitive).llvm;
}

static const char* slotLlvmType(FarTypeId id) {
  if (isAggregateType(id))
    return aggLlvmType(id);
  return isFloatType(id) ? F64 : I64;
}

static const char* slotLlvmTypeDesc(const TypeDesc& td) {
  if (isAggregateDesc(td))
    return aggLlvmType(td.primitive);
  if (td.form == TypeForm::Tuple || td.form == TypeForm::User)
    return I64;
  if (isCollectionValue(td))
    return llvmAbiTypeDesc(td);
  if (isPrimitiveDesc(td) && isFloatType(td.primitive))
    return F64;
  return I64;
}

static bool usesFloatStorage(FarTypeId id) { return isFloatType(id); }



static bool isUnsignedTypeDesc(const TypeDesc& td) {
  return isPrimitiveDesc(td) && isIntegerType(td.primitive) && !typeInfo(td.primitive).is_signed;
}

static bool isSignedIntegerTypeDesc(const TypeDesc& td) {
  return isPrimitiveDesc(td) && isIntegerType(td.primitive) && typeInfo(td.primitive).is_signed;
}

static std::string intLitCodegenText(const Expr& expr) {
  if (expr.kind != Expr::Int)
    return "0";
  if (expr.int_lit.unsigned_decimal)
    return std::to_string(static_cast<uint64_t>(expr.int_lit.value));
  return std::to_string(expr.int_lit.value);
}

static bool binOpUsesUnsigned(const TypeDesc& lt, const TypeDesc& rt) {
  return isUnsignedTypeDesc(lt) || isUnsignedTypeDesc(rt);
}

static const std::unordered_map<std::string, std::string> cmpOps = {

    {"==", "eq"}, {"!=", "ne"}, {"<", "slt"}, {">", "sgt"}, {"<=", "sle"}, {">=", "sge"}};

static std::string flipOrderedCmpOp(const std::string& op) {
  if (op == "<")
    return ">";
  if (op == ">")
    return "<";
  if (op == "<=")
    return ">=";
  if (op == ">=")
    return "<=";
  return op;
}

static std::string emitMixedSignIntegerCompare(
    std::ostringstream& out,
    const std::function<std::string(const std::string&)>& fresh,
    const std::string& cmp_op,
    const std::string& unsigned_val,
    const std::string& signed_val) {
  std::string neg = fresh("mixsign");
  out << "  %" << neg << " = icmp slt " << I64 << " " << signed_val << ", 0\n";
  if (cmp_op == "==") {
    std::string not_neg = fresh("mixnn");
    out << "  %" << not_neg << " = xor i1 %" << neg << ", 1\n";
    std::string eq_bits = fresh("mixeq");
    out << "  %" << eq_bits << " = icmp eq " << I64 << " " << unsigned_val << ", " << signed_val << "\n";
    std::string both = fresh("mixboth");
    out << "  %" << both << " = and i1 %" << not_neg << ", %" << eq_bits << "\n";
    std::string z = fresh("mixz");
    out << "  %" << z << " = zext i1 %" << both << " to " << I64 << "\n";
    return "%" + z;
  }
  if (cmp_op == "!=") {
    std::string ne_bits = fresh("mixne");
    out << "  %" << ne_bits << " = icmp ne " << I64 << " " << unsigned_val << ", " << signed_val << "\n";
    std::string either = fresh("mixeither");
    out << "  %" << either << " = or i1 %" << neg << ", %" << ne_bits << "\n";
    std::string z = fresh("mixz");
    out << "  %" << z << " = zext i1 %" << either << " to " << I64 << "\n";
    return "%" + z;
  }
  std::string s_nonneg = fresh("mixsok");
  out << "  %" << s_nonneg << " = xor i1 %" << neg << ", 1\n";
  std::string bits = fresh("mixbits");
  if (cmp_op == "<")
    out << "  %" << bits << " = icmp ult " << I64 << " " << unsigned_val << ", " << signed_val << "\n";
  else if (cmp_op == ">")
    out << "  %" << bits << " = icmp ugt " << I64 << " " << unsigned_val << ", " << signed_val << "\n";
  else if (cmp_op == "<=")
    out << "  %" << bits << " = icmp ule " << I64 << " " << unsigned_val << ", " << signed_val << "\n";
  else if (cmp_op == ">=")
    out << "  %" << bits << " = icmp uge " << I64 << " " << unsigned_val << ", " << signed_val << "\n";
  else
    throw FarError("unknown mixed-sign compare op '" + cmp_op + "'");
  std::string result = fresh("mixres");
  if (cmp_op == "<" || cmp_op == "<=")
    out << "  %" << result << " = and i1 %" << s_nonneg << ", %" << bits << "\n";
  else
    out << "  %" << result << " = or i1 %" << neg << ", %" << bits << "\n";
  std::string z = fresh("mixz");
  out << "  %" << z << " = zext i1 %" << result << " to " << I64 << "\n";
  return "%" + z;
}

static bool isBoolProducingOp(const std::string& op) {
  return op == "==" || op == "!=" || op == "===" || op == "!==" || op == "<" || op == ">" ||
         op == "<=" || op == ">=" || op == "and" || op == "or" || op == "&&" || op == "||" ||
         op == "in" || op == "not in";
}

static bool exprPrintsAsBool(const Expr& e) {
  if (isPrimTy(e.type, FarTypeId::Bool))
    return true;
  switch (e.kind) {
    case Expr::Binary:
      return isBoolProducingOp(e.bin_op.op);
    case Expr::PrefixExprK:
      return e.prefix.op == "!";
    case Expr::IsExprK:
      return true;
    case Expr::CastExpr:
      return isPrimTy(e.cast.target, FarTypeId::Bool);
    default:
      return false;
  }
}

static void emitPrintBoolValue(std::ostringstream& out,
                               const std::function<std::string(const std::string&)>& fresh,
                               const std::string& value) {
  std::string str = fresh("str");
  out << "  %" << str << " = call i8* @far_bool_to_str(i64 " << value << ")\n";
  out << "  call void @far_print_str(i8* %" << str << ")\n";
}



static const std::unordered_map<std::string, std::string> arithOps = {

    {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "sdiv"}, {"%", "srem"}};



struct VarInfo {
  TypeDesc type = TypeDesc::prim(FarTypeId::I64);
  std::string slot;
  bool is_param = false;
};



struct LoopLabels {

  std::string break_label;

  std::string continue_label;

  std::string exit_label;

};



class LLVMCodegen {

public:

  explicit LLVMCodegen(const Program& program, const FarTarget& target)
      : program_(program), target_(target) {
    obj_reg_.build(const_cast<Program&>(program_), false);
    for (const auto& fn : program.functions) {
      std::string sym = fn.llvm_name.empty() ? mangleFunction(fn) : fn.llvm_name;
      fn_by_llvm_[sym] = &fn;
      fn_by_name_[fn.name].push_back(&fn);
    }
    for (const auto& fn : program.synthetic_functions) {
      std::string sym = fn.llvm_name.empty() ? mangleFunction(fn) : fn.llvm_name;
      fn_by_llvm_[sym] = &fn;
      fn_by_name_[fn.name].push_back(&fn);
    }
  }



  std::string emit() {

    out_ << "; Far -> LLVM IR\n";

    out_ << "target datalayout = \"" << target_.datalayout << "\"\n";

    out_ << "target triple = \"" << target_.triple << "\"\n\n";

    out_ << "declare void @far_args_init(i32, i8**)\n\n";

    out_ << "declare void @far_print_i64(i64)\n";

    out_ << "declare void @far_print_str(i8*)\n";

    out_ << "declare i64 @far_str_len(i8*)\n";
    out_ << "declare i64 @far_str_equal(i8*, i8*)\n";
    out_ << "declare i8* @far_str_concat(i8*, i8*)\n";
    out_ << "declare i64 @far_str_char_at(i8*, i64)\n";
    out_ << "declare i8* @far_str_slice(i8*, i64, i64)\n";

    out_ << "declare i64 @far_array_new(i64)\n";

    out_ << "declare i64 @far_array_get(i64, i64)\n";

    out_ << "declare void @far_array_set(i64, i64, i64)\n";

    out_ << "declare i64 @far_array_len(i64)\n";

    out_ << "declare i64 @far_thread_count()\n";

    out_ << "declare i64 @far_spawn(i8*, i64, i64, i64, i64, i64)\n";

    out_ << "declare i64 @far_join(i64)\n";

    out_ << "declare i64 @far_parallel(i8*, i64)\n";
    out_ << "declare i64 @far_await(i64)\n";
    out_ << "declare i64 @far_gen_next(i64)\n";
    out_ << "declare i64 @far_closure_new(i8*, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64)\n";
    out_ << "declare void @far_closure_drop(i64)\n";
    out_ << "declare i64 @far_owned_dup(i64, i64)\n";
    out_ << "declare i8* @far_str_copy(i8*)\n";
    out_ << "declare i64 @far_closure_call(i64, i64)\n";
    out_ << "declare i64 @far_tarray_contains(i64, i64)\n";

    out_ << "declare void @far_print_f32(float)\n";

    out_ << "declare void @far_print_f64(double)\n";

    out_ << "declare i8* @far_i64_to_str(i64)\n";
    out_ << "declare i8* @far_u64_to_str(i64)\n";
    out_ << "declare i64 @far_str_to_i64(i8*)\n";
    out_ << "declare double @far_str_to_f64(i8*)\n";

    out_ << "declare i8* @far_f64_to_str(double)\n";

    out_ << "declare i8* @far_bool_to_str(i64)\n";

    out_ << "declare i8* @far_char_to_str(i16)\n";

    declareBuiltins(out_);
    declareStdlibRuntime(out_);
    declareScienceRuntime(out_);
    declareNetRuntime(out_);
    declareModernRuntime(out_);
    declareSecRuntime(out_);
    declarePerfRuntime(out_);
    declareIoRuntime(out_);
    declareAggregateTypes(out_);
    declareAggregateRuntime(out_);
    declareCollectionTypes(out_);
    declareCollectionRuntime(out_);
    declareMemoryRuntime(out_);
    declareConcurrencyRuntime(out_);
    declareErrorRuntime(out_);
    declareStringRuntime(out_);
    declarePatternRuntime(out_);
    out_ << "declare i64 @far_box_alloc(i64)\n";
    out_ << "declare i64 @far_reflect_kind(i64)\n";
    out_ << "declare i64 @far_reflect_fields(i64)\n";
    out_ << "declare i64 @far_reflect_has_attr(i64, i8*)\n";
    declareUserTypes(out_, obj_reg_);

    out_ << "\n";

    std::ostringstream fn_out;
    std::ostringstream saved = std::move(out_);
    out_ = std::move(fn_out);
    bool has_user_main = false;
    for (const auto& fn : program_.functions) {
      if (!fn.type_params.empty())
        continue;
      if (fn.name == "main" && fn.params.empty())
        has_user_main = true;
      emitFunction(fn);
    }
    for (const auto& fn : program_.synthetic_functions)
      emitFunction(fn);
    if (has_user_main)
      emitCrtMainWrapper();
    fn_out = std::move(out_);
    out_ = std::move(saved);
    if (!globals_.str().empty())
      out_ << globals_.str() << "\n";
    out_ << fn_out.str();
    return out_.str();

  }



private:

  const Program& program_;
  FarTarget target_;
  ObjectRegistry obj_reg_;

  std::ostringstream out_;

  std::ostringstream globals_;

  std::ostringstream hoisted_allocas_;

  int tmp_ = 0;

  int str_id_ = 0;

  std::string pending_ret_flag_;
  std::string pending_ret_val_;
  bool pending_ret_init_ = false;
  const Return* pending_ret_deferred_ = nullptr;

  std::string pending_jump_kind_;
  bool pending_jump_init_ = false;
  std::string pending_break_label_;
  std::string pending_continue_label_;

  std::string pending_yield_flag_;
  std::string pending_yield_val_;
  bool pending_yield_init_ = false;

  std::string pending_throw_flag_;
  std::string pending_throw_tag_;
  std::string pending_throw_val_;
  bool pending_throw_init_ = false;

  std::unordered_map<std::string, const Function*> fn_by_llvm_;
  std::unordered_map<std::string, std::vector<const Function*>> fn_by_name_;

  void hoistedAlloca(const std::string& slot, const TypeDesc& type) {
    hoisted_allocas_ << "  %" << slot << " = alloca " << slotLlvmTypeDesc(type) << "\n";
  }

  void hoistedAllocaRaw(const std::string& slot, const char* llvm_ty) {
    hoisted_allocas_ << "  %" << slot << " = alloca " << llvm_ty << "\n";
  }



  std::string fresh(const std::string& prefix = "t") {

    return prefix + std::to_string(tmp_++);

  }

  static std::string formatDoubleLiteral(double value) {
    if (std::isnan(value))
      return std::signbit(value) ? "0xFFF8000000000000" : "0x7FF8000000000000";
    if (std::isinf(value))
      return value > 0.0 ? "0x7FF0000000000000" : "0xFFF0000000000000";
    std::ostringstream ss;
    ss << std::setprecision(17) << value;
    std::string lit = ss.str();
    if (lit.find('.') == std::string::npos && lit.find('e') == std::string::npos &&
        lit.find('E') == std::string::npos)
      lit += ".0";
    return lit;
  }

  std::string emitFloatConstAsDouble(double value) {
    std::string dlit = formatDoubleLiteral(value);
    std::string tmp = fresh();
    out_ << "  %" << tmp << " = fptrunc " << F64 << " " << dlit << " to " << F32 << "\n";
    std::string ext = fresh();
    out_ << "  %" << ext << " = fpext " << F32 << " %" << tmp << " to " << F64 << "\n";
    return "%" + ext;
  }

  class Ctx;

  std::string abiParamToInternal(FarTypeId ty, const std::string& abi_val) {
    const FarTypeInfo& info = typeInfo(ty);
    if (isFloatType(ty)) {
      if (ty == FarTypeId::F32) {
        std::string tmp = fresh();
        out_ << "  %" << tmp << " = fpext " << F32 << " " << abi_val << " to " << F64 << "\n";
        return "%" + tmp;
      }
      return abi_val;
    }
    if (ty == FarTypeId::Bool) {
      std::string tmp = fresh();
      out_ << "  %" << tmp << " = zext i1 " << abi_val << " to " << I64 << "\n";
      return "%" + tmp;
    }
    if (ty == FarTypeId::String) {
      std::string tmp = fresh();
      out_ << "  %" << tmp << " = ptrtoint i8* " << abi_val << " to " << I64 << "\n";
      return "%" + tmp;
    }
    if (ty == FarTypeId::I64 || ty == FarTypeId::U64 || ty == FarTypeId::Arr || isAggregateType(ty))
      return abi_val;
    std::string tmp = fresh();
    if (info.is_signed)
      out_ << "  %" << tmp << " = sext " << info.llvm << " " << abi_val << " to " << I64 << "\n";
    else
      out_ << "  %" << tmp << " = zext " << info.llvm << " " << abi_val << " to " << I64 << "\n";
    return "%" + tmp;
  }

  std::string internalToAbi(FarTypeId from, FarTypeId to, const std::string& val) {
    if (isFloatType(to)) {
      if (to == FarTypeId::F32) {
        std::string src = val;
        if (from != FarTypeId::F32 && from != FarTypeId::F64) {
          src = emitIntToF64(from, val);
        }
        std::string tmp = fresh();
        out_ << "  %" << tmp << " = fptrunc " << F64 << " " << src << " to " << F32 << "\n";
        return "%" + tmp;
      }
      if (!isFloatType(from))
        return emitIntToF64(from, val);
      return val;
    }
    if (isIntegerType(to) && isFloatType(from)) {
      std::string src = val;
      if (from == FarTypeId::F32) {
        std::string ext = fresh();
        out_ << "  %" << ext << " = fpext " << F32 << " " << val << " to " << F64 << "\n";
        src = "%" + ext;
      }
      std::string tmp = fresh();
      if (isIntegerType(to) && !typeInfo(to).is_signed)
        out_ << "  %" << tmp << " = fptoui " << F64 << " " << src << " to " << I64 << "\n";
      else
        out_ << "  %" << tmp << " = fptosi " << F64 << " " << src << " to " << I64 << "\n";
      return "%" + tmp;
    }
    if (to == FarTypeId::Bool) {
      std::string tmp = fresh();
      out_ << "  %" << tmp << " = icmp ne " << I64 << " " << val << ", 0\n";
      return "%" + tmp;
    }
    if (isAggregateType(to))
      return val;
    if (to == FarTypeId::String) {
      std::string tmp = fresh();
      out_ << "  %" << tmp << " = inttoptr " << I64 << " " << val << " to i8*\n";
      return "%" + tmp;
    }
    const FarTypeInfo& info = typeInfo(to);
    if (std::strcmp(info.llvm, I64) == 0)
      return val;
    std::string tmp = fresh();
    out_ << "  %" << tmp << " = trunc " << I64 << " " << val << " to " << info.llvm << "\n";
    return "%" + tmp;
  }

  std::string emitIntToF64(FarTypeId from, const std::string& val) {
    std::string tmp = fresh();
    if (isIntegerType(from) && !typeInfo(from).is_signed)
      out_ << "  %" << tmp << " = uitofp " << I64 << " " << val << " to " << F64 << "\n";
    else
      out_ << "  %" << tmp << " = sitofp " << I64 << " " << val << " to " << F64 << "\n";
    return "%" + tmp;
  }

  std::string emitIntToF64Desc(const TypeDesc& from, const std::string& val) {
    if (isPrimitiveDesc(from))
      return emitIntToF64(from.primitive, val);
    return emitIntToF64(FarTypeId::I64, val);
  }

  std::string emitF64ToInt(FarTypeId to, const std::string& val) {
    std::string tmp = fresh();
    if (isIntegerType(to) && !typeInfo(to).is_signed)
      out_ << "  %" << tmp << " = fptoui " << F64 << " " << val << " to " << I64 << "\n";
    else
      out_ << "  %" << tmp << " = fptosi " << F64 << " " << val << " to " << I64 << "\n";
    return "%" + tmp;
  }

  std::string emitCastValue(FarTypeId from, FarTypeId to, const std::string& val) {
    if (from == to)
      return val;
    if (isFloatType(to)) {
      std::string src = val;
      if (!isFloatType(from))
        src = emitIntToF64(from, val);
      else if (from == FarTypeId::F32) {
        if (!val.empty() && val[0] == '%')
          src = val;
        else {
          std::string tmp = fresh();
          out_ << "  %" << tmp << " = fpext " << F32 << " " << val << " to " << F64 << "\n";
          src = "%" + tmp;
        }
      } else if (from == FarTypeId::F16) {
        if (!val.empty() && val[0] == '%')
          src = val;
        else {
          std::string tmp = fresh();
          out_ << "  %" << tmp << " = fpext " << F16 << " " << val << " to " << F64 << "\n";
          src = "%" + tmp;
        }
      }
      if (to == FarTypeId::F16) {
        std::string tmp = fresh();
        out_ << "  %" << tmp << " = fptrunc " << F64 << " " << src << " to " << F16 << "\n";
        std::string as_d = fresh();
        out_ << "  %" << as_d << " = fpext " << F16 << " %" << tmp << " to " << F64 << "\n";
        return "%" + as_d;
      }
      if (to == FarTypeId::F32) {
        std::string tmp = fresh();
        out_ << "  %" << tmp << " = fptrunc " << F64 << " " << src << " to " << F32 << "\n";
        std::string as_d = fresh();
        out_ << "  %" << as_d << " = fpext " << F32 << " %" << tmp << " to " << F64 << "\n";
        return "%" + as_d;
      }
      return src;
    }
    if (isFloatType(from)) {
      std::string src = val;
      if (from == FarTypeId::F32) {
        if (!val.empty() && val[0] == '%')
          src = val;
        else {
          std::string tmp = fresh();
          out_ << "  %" << tmp << " = fpext " << F32 << " " << val << " to " << F64 << "\n";
          src = "%" + tmp;
        }
      } else if (from == FarTypeId::F16) {
        if (!val.empty() && val[0] == '%')
          src = val;
        else {
          std::string tmp = fresh();
          out_ << "  %" << tmp << " = fpext " << F16 << " " << val << " to " << F64 << "\n";
          src = "%" + tmp;
        }
      }
      std::string as_i = emitF64ToInt(to, src);
      return emitCastValue(FarTypeId::I64, to, as_i);
    }
    if (to == FarTypeId::Bool) {
      std::string tmp = fresh();
      out_ << "  %" << tmp << " = icmp ne " << I64 << " " << val << ", 0\n";
      std::string as_i = fresh();
      out_ << "  %" << as_i << " = zext i1 %" << tmp << " to " << I64 << "\n";
      return "%" + as_i;
    }
    const FarTypeInfo& info = typeInfo(to);
    if (info.bits < 64) {
      std::string trunc = fresh();
      out_ << "  %" << trunc << " = trunc " << I64 << " " << val << " to " << info.llvm << "\n";
      std::string wide = fresh();
      if (info.is_signed)
        out_ << "  %" << wide << " = sext " << info.llvm << " %" << trunc << " to " << I64 << "\n";
      else
        out_ << "  %" << wide << " = zext " << info.llvm << " %" << trunc << " to " << I64 << "\n";
      return "%" + wide;
    }
    return val;
  }

  std::string abiParamToInternalDesc(const TypeDesc& ty, const std::string& abi_val) {
    if (isAggregateDesc(ty) || !isPrimitiveDesc(ty) || isCollectionHandle(ty) || ty.form == TypeForm::Array)
      return abi_val;
    return abiParamToInternal(ty.primitive, abi_val);
  }

  std::string internalToAbiDesc(const TypeDesc& from, const TypeDesc& to, const std::string& val) {
    if (!isPrimitiveDesc(to) || isCollectionHandle(to) || to.form == TypeForm::Array)
      return val;
    if (!isPrimitiveDesc(from))
      return val;
    return internalToAbi(from.primitive, to.primitive, val);
  }

  std::string emitNumericToString(const TypeDesc& from, const std::string& val) {
    std::string str = fresh("str");
    if (isPrimTy(from, FarTypeId::Char)) {
      std::string ch = fresh("ch");
      out_ << "  %" << ch << " = trunc " << I64 << " " << val << " to i16\n";
      out_ << "  %" << str << " = call i8* @far_char_to_str(i16 %" << ch << ")\n";
    } else if (isPrimTy(from, FarTypeId::Bool)) {
      out_ << "  %" << str << " = call i8* @far_bool_to_str(" << I64 << " " << val << ")\n";
    } else if (isPrimTy(from, FarTypeId::F32)) {
      std::string fval = fresh();
      out_ << "  %" << fval << " = fptrunc " << F64 << " " << val << " to " << F32 << "\n";
      std::string promoted = fresh();
      out_ << "  %" << promoted << " = fpext " << F32 << " %" << fval << " to " << F64 << "\n";
      out_ << "  %" << str << " = call i8* @far_f64_to_str(double %" << promoted << ")\n";
    } else if (isPrimTy(from, FarTypeId::F64)) {
      out_ << "  %" << str << " = call i8* @far_f64_to_str(double " << val << ")\n";
    } else if (isPrimitiveDesc(from) && isIntegerType(from.primitive) && !typeInfo(from.primitive).is_signed) {
      out_ << "  %" << str << " = call i8* @far_u64_to_str(" << I64 << " " << val << ")\n";
    } else {
      out_ << "  %" << str << " = call i8* @far_i64_to_str(" << I64 << " " << val << ")\n";
    }
    std::string as_i64 = fresh();
    out_ << "  %" << as_i64 << " = ptrtoint i8* %" << str << " to " << I64 << "\n";
    return "%" + as_i64;
  }

  std::string emitCastValueDesc(const TypeDesc& from, const TypeDesc& to, const std::string& val) {
    if (isPrimTy(to, FarTypeId::String)) {
      if (isPrimTy(from, FarTypeId::String) || isPrimTy(from, FarTypeId::RawString))
        return val;
      if (isPrimitiveDesc(from))
        return emitNumericToString(from, val);
    }
    if ((isPrimTy(from, FarTypeId::String) || isPrimTy(from, FarTypeId::RawString)) && isPrimitiveDesc(to)) {
      std::string ptr = fresh("sp");
      out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";
      if (isFloatType(to.primitive)) {
        std::string tmp = fresh("parsed");
        out_ << "  %" << tmp << " = call double @far_str_to_f64(i8* %" << ptr << ")\n";
        return emitCastValue(FarTypeId::F64, to.primitive, "%" + tmp);
      }
      std::string tmp = fresh("parsed");
      out_ << "  %" << tmp << " = call " << I64 << " @far_str_to_i64(i8* %" << ptr << ")\n";
      return emitCastValue(FarTypeId::I64, to.primitive, "%" + tmp);
    }
    if (!isPrimitiveDesc(from) || !isPrimitiveDesc(to))
      return val;
    return emitCastValue(from.primitive, to.primitive, val);
  }

  void emitCrtMainWrapper() {
    out_ << "define i32 @main(i32 %argc, i8** %argv) {\nentry:\n";
    out_ << "  call void @far_args_init(i32 %argc, i8** %argv)\n";
    out_ << "  %ret = call i64 @far_user_main()\n";
    out_ << "  %exit = trunc i64 %ret to i32\n";
    out_ << "  ret i32 %exit\n";
    out_ << "}\n\n";
  }

  void emitFunction(const Function& fn) {
    const char* ret_ty = llvmAbiTypeDesc(fn.return_type);
    std::string sym = fn.llvm_name.empty() ? mangleFunction(fn) : fn.llvm_name;
    if (fn.name == "main" && fn.params.empty())
      sym = "far_user_main";
    out_ << "define " << ret_ty << " @" << sym << "(";
    for (size_t i = 0; i < fn.params.size(); ++i) {
      if (i > 0)
        out_ << ", ";
      out_ << llvmAbiTypeDesc(fn.params[i].type) << " %" << fn.params[i].name;
    }
    out_ << ") {\nentry:\n";

    hoisted_allocas_.str("");
    hoisted_allocas_.clear();

    std::unordered_map<std::string, VarInfo> env;
    for (const auto& p : fn.params) {
      std::string slot = fresh("slot");
      const char* st = slotLlvmTypeDesc(p.type);
      out_ << "  %" << slot << " = alloca " << st << "\n";
      std::string internal = abiParamToInternalDesc(p.type, "%" + p.name);
      out_ << "  store " << st << " " << internal << ", " << st << "* %" << slot << "\n";
      VarInfo vi{p.type, slot, true};
      env[p.name] = vi;
    }

    std::ostringstream body;
    std::ostringstream saved = std::move(out_);
    out_ = std::move(body);

    Ctx ctx(this, env, fn.return_type);
    ctx.resetPendingReturn();
    const auto& body_stmts = fn.body_source     ? fn.body_source->body
                             : fn.shared_body ? *fn.shared_body
                                              : fn.body;
    ctx.runBody(body_stmts);
    if (!ctx.terminated) {
      std::string zero = isAggregateDesc(fn.return_type)
                             ? "zeroinitializer"
                             : (isPrimTy(fn.return_type, FarTypeId::F32) ? "0.0"
                                                                         : (isPrimitiveDesc(fn.return_type) &&
                                                                                    isFloatType(fn.return_type.primitive)
                                                                                ? "0.0"
                                                                                : "0"));
      out_ << "  ret " << ret_ty << " " << zero << "\n";
    }

    body = std::move(out_);
    out_ = std::move(saved);
    out_ << hoisted_allocas_.str();
    out_ << body.str();
    out_ << "}\n\n";
  }



  class Ctx {

  public:

    Ctx(LLVMCodegen* gen, std::unordered_map<std::string, VarInfo> env,
        TypeDesc return_type = TypeDesc::prim(FarTypeId::I64))
        : gen_(gen), env_(std::move(env)), return_type_(std::move(return_type)) {}



    bool terminated = false;
    bool exits_loop_not_scope_ = false;
    bool switch_break_arm_ = false;

    std::unordered_set<std::string> assigned_;



    void emitStmt(const Stmt& stmt) {

      if (terminated && !exits_loop_not_scope_) return;

      if (++stmt_depth_ > 512)
        throw FarError("codegen statement depth exceeded");
      struct StmtDepthGuard {
        Ctx* self;
        explicit StmtDepthGuard(Ctx* c) : self(c) {}
        ~StmtDepthGuard() {
          if (self && self->stmt_depth_ > 0)
            --self->stmt_depth_;
        }
      } depth_guard(this);

      switch (stmt.kind) {

        case Stmt::LetStmt: emitLet(stmt.let); break;

        case Stmt::ReturnStmt: emitReturn(stmt.ret); break;

        case Stmt::YieldStmtK: emitYield(stmt.yield); break;

        case Stmt::ExprStmtK: emitExpr(*stmt.expr_stmt.expr); break;

        case Stmt::PrintStmt: emitPrint(stmt.print); break;

        case Stmt::IfStmt: emitIf(stmt.if_stmt); break;

        case Stmt::WhileStmt: emitWhile(stmt.while_stmt); break;

        case Stmt::ForStmt: emitFor(stmt.for_stmt); break;

        case Stmt::BreakStmt: emitBreak(); break;

        case Stmt::ContinueStmt: emitContinue(); break;

        case Stmt::DeferStmtK:
          defer_stack_.push_back(stmt.defer.expr.get());
          break;

        case Stmt::UnsafeStmtK:
          pushScope();
          for (const auto& s : stmt.unsafe.body)
            emitStmt(*s);
          popScope();
          break;

        case Stmt::TryStmtK:
          emitTry(stmt.try_stmt);
          break;

        case Stmt::ThrowStmtK:
          emitThrowStmt(stmt.throw_stmt);
          break;

        case Stmt::MatchStmtK:
          emitMatch(stmt.match_stmt);
          break;

        case Stmt::ComptimeBlockK:
        case Stmt::CodegenBlockK:
          break;

      }

    }



    Ctx fork() const {
      Ctx c(gen_, env_, return_type_);
      c.try_stack_ = try_stack_;
      c.defer_stack_ = defer_stack_;
      c.defer_scope_base_ = defer_stack_.size();
      c.loop_stack_ = loop_stack_;
      c.switch_break_stack_ = switch_break_stack_;
      c.autodrop_frames_ = autodrop_frames_;
      c.autodrop_scope_base_ =
          c.autodrop_frames_.empty() ? 0 : c.autodrop_frames_.back().size();
      c.terminated = terminated;
      c.exits_loop_not_scope_ = exits_loop_not_scope_;
      c.switch_break_arm_ = switch_break_arm_;
      c.stmt_depth_ = stmt_depth_;
      c.emit_depth_ = emit_depth_;
      c.emitting_finally_for_ = emitting_finally_for_;
      c.emitting_catch_for_ = emitting_catch_for_;
      c.const_int_locals_ = const_int_locals_;
      c.const_float_locals_ = const_float_locals_;
      c.constexpr_empty_locals_ = constexpr_empty_locals_;
      c.explicit_typed_ = explicit_typed_;
      return c;
    }

    void injectConstexprGlobalsFromStmts(const std::vector<std::unique_ptr<Stmt>>& stmts) {
      for (const auto& stmt : stmts) {
        if (stmt->kind == Stmt::LetStmt && stmt->let.is_constexpr && stmt->let.value)
          emitLet(stmt->let);
        else if (stmt->kind == Stmt::ComptimeBlockK)
          injectConstexprGlobalsFromStmts(stmt->comptime_block);
      }
    }

    void registerOwnedAutoDrop(const std::string& name, const TypeDesc& ty) {
      if (autodrop_frames_.empty())
        return;
      auto& frame = autodrop_frames_.back();
      for (const auto& ad : frame) {
        if (ad.name == name)
          return;
      }
      if (isMemoryHandleDesc(ty) || isConcurrencyHandleDesc(ty))
        frame.push_back({name, ty.form});
      else if (isOwnedHeapCollectionDesc(ty))
        frame.push_back({name, autodropFormFor(ty)});
      else if (isFunctionDesc(ty))
        frame.push_back({name, TypeForm::Function});
      else if (isUserDesc(ty)) {
        const UserTypeDef* ut = gen_->obj_reg_.lookup(ty.user_name);
        if (ut && ut->kind == UserTypeKind::Actor)
          frame.push_back({name, TypeForm::User, true});
        else if (ut && ut->kind == UserTypeKind::Union)
          frame.push_back({name, TypeForm::User, false, true});
      }
    }

    struct ClosureCap {
      int64_t form = -1;
      std::string value = "0";
    };

    static constexpr int64_t kCapFormString = 1000;

    ClosureCap prepareClosureCapture(const std::string& cap_name) {
      ClosureCap cap;
      const TypeDesc& ty = env_.at(cap_name).type;
      std::string val = loadVar(cap_name);
      if (isPrimTy(ty, FarTypeId::String) || isPrimTy(ty, FarTypeId::RawString)) {
        cap.form = kCapFormString;
        std::string ptr = gen_->fresh("csp");
        gen_->out_ << "  %" << ptr << " = inttoptr i64 " << val << " to i8*\n";
        std::string dup = gen_->fresh("csd");
        gen_->out_ << "  %" << dup << " = call i8* @far_str_copy(i8* %" << ptr << ")\n";
        std::string slot = gen_->fresh("csi");
        gen_->out_ << "  %" << slot << " = ptrtoint i8* %" << dup << " to i64\n";
        cap.value = "%" + slot;
        return cap;
      }
      if (isOwnedHeapCollectionDesc(ty)) {
        cap.form = static_cast<int64_t>(autodropFormFor(ty));
        std::string dup = gen_->fresh("odup");
        gen_->out_ << "  %" << dup << " = call i64 @far_owned_dup(i64 " << cap.form << ", i64 " << val
                   << ")\n";
        cap.value = "%" + dup;
        return cap;
      }
      cap.form = -1;
      cap.value = val;
      return cap;
    }

    std::string emitClosureNew(const Function* lf, const std::vector<std::string>& captures) {
      std::string ptr = fnPointerBitcastLlvm(lf->llvm_name, lf);
      int64_t ncaps = static_cast<int64_t>(captures.size());
      if (captures.size() > 4)
        throw FarError("closure captures at most 4 variables");
      ClosureCap caps[4];
      for (size_t i = 0; i < captures.size(); ++i)
        caps[i] = prepareClosureCapture(captures[i]);
      std::string tmp = gen_->fresh("fnval");
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_closure_new(i8* " << ptr << ", " << I64
                 << " " << ncaps;
      for (int i = 0; i < 4; ++i)
        gen_->out_ << ", " << I64 << " " << caps[i].form << ", " << I64 << " " << caps[i].value;
      gen_->out_ << ")\n";
      return "%" + tmp;
    }

    void runBody(const std::vector<std::unique_ptr<Stmt>>& body) {
      pushScope();
      for (const auto& kv : env_) {
        if (kv.second.is_param)
          continue;
        registerOwnedAutoDrop(kv.first, kv.second.type);
      }
      injectConstexprGlobalsFromStmts(gen_->program_.comptime_stmts);
      for (const auto& stmt : body) {
        emitStmt(*stmt);
        if (terminated || stmtAbortsRestOfBlock(*stmt)) {
          terminated = true;
          break;
        }
      }
      if (!terminated) {
        flushDefers();
        flushAllAutoDrops();
      }
    }

    void resetPendingReturn() {
      gen_->pending_ret_init_ = false;
      gen_->pending_ret_flag_.clear();
      gen_->pending_ret_val_.clear();
      gen_->pending_jump_init_ = false;
      gen_->pending_jump_kind_.clear();
      gen_->pending_break_label_.clear();
      gen_->pending_continue_label_.clear();
      gen_->pending_yield_init_ = false;
      gen_->pending_yield_flag_.clear();
      gen_->pending_yield_val_.clear();
      gen_->pending_throw_init_ = false;
      gen_->pending_throw_flag_.clear();
      gen_->pending_throw_tag_.clear();
      gen_->pending_throw_val_.clear();
      gen_->pending_ret_deferred_ = nullptr;
    }

  private:

    LLVMCodegen* gen_;

    std::unordered_map<std::string, VarInfo> env_;

    std::unordered_map<std::string, int64_t> const_int_locals_;
    std::unordered_map<std::string, double> const_float_locals_;
    std::unordered_set<std::string> explicit_typed_;
    std::unordered_set<std::string> constexpr_empty_locals_;

    std::vector<LoopLabels> loop_stack_;
    std::vector<std::string> switch_break_stack_;

    TypeDesc return_type_ = TypeDesc::prim(FarTypeId::I64);

    std::vector<const Expr*> defer_stack_;
    size_t defer_scope_base_ = 0;
    struct AutoDrop {
      std::string name;
      TypeForm form;
      bool actor = false;
      bool union_handle = false;
    };
    std::vector<std::vector<AutoDrop>> autodrop_frames_;
    size_t autodrop_scope_base_ = 0;

    struct TryLabels {
      std::string resume_label;
      std::string body_label;
      std::string catch_label;
      std::string finally_label;
      std::string after_label;
      std::string continue_label;
      std::string unwind_slot;
      std::string catch_var;
      TypeDesc catch_type = TypeDesc::prim(FarTypeId::I64);
      int64_t catch_type_tag = 0;
      bool catch_type_explicit = false;
      bool has_catch = false;
      bool has_finally = false;
      size_t stack_index = 0;
      int contained_in_finally_of = -1;
      bool break_finishes_try = false;
      bool has_code_after_loop = false;
    };
    std::vector<TryLabels> try_stack_;
    int emitting_finally_for_ = -1;
    int emitting_catch_for_ = -1;

    int emit_depth_ = 0;
    int stmt_depth_ = 0;

    void ensurePendingReturn() {
      if (gen_->pending_ret_init_)
        return;
      gen_->pending_ret_flag_ = gen_->fresh("pretflag");
      gen_->pending_ret_val_ = gen_->fresh("pretval");
      const char* ret_ty = llvmAbiTypeDesc(return_type_);
      gen_->hoisted_allocas_ << "  %" << gen_->pending_ret_flag_ << " = alloca i1\n";
      gen_->hoisted_allocas_ << "  %" << gen_->pending_ret_val_ << " = alloca " << ret_ty << "\n";
      gen_->out_ << "  store i1 false, i1* %" << gen_->pending_ret_flag_ << "\n";
      gen_->pending_ret_init_ = true;
    }

    static TypeForm autodropFormFor(const TypeDesc& ty) {
      if (isPrimitiveDesc(ty) && ty.primitive == FarTypeId::Arr)
        return TypeForm::Array;
      return ty.form;
    }

    void emitOwnedCollectionDrop(TypeForm form, const std::string& val) {
      AutoDrop ad;
      ad.form = form;
      emitAutoDrop(ad, val);
    }

    void emitAutoDrop(const AutoDrop& ad, const std::string& val) {
      if (ad.actor) {
        gen_->out_ << "  call void @far_actor_stop(i64 " << val << ")\n";
      } else if (ad.union_handle) {
        gen_->out_ << "  call void @far_union_drop(i64 " << val << ")\n";
      } else if (ad.form == TypeForm::Dict) {
        gen_->out_ << "  call void @far_dict_drop(i64 " << val << ")\n";
      } else if (ad.form == TypeForm::Set) {
        gen_->out_ << "  call void @far_set_drop(i64 " << val << ")\n";
      } else if (ad.form == TypeForm::LinkedList) {
        gen_->out_ << "  call void @far_llist_drop(i64 " << val << ")\n";
      } else if (ad.form == TypeForm::List || ad.form == TypeForm::Queue ||
                 ad.form == TypeForm::Stack) {
        gen_->out_ << "  call void @far_list_drop(i64 " << val << ")\n";
      } else if (ad.form == TypeForm::Array || ad.form == TypeForm::Slice ||
                 ad.form == TypeForm::Tuple) {
        gen_->out_ << "  call void @far_tarray_drop(i64 " << val << ")\n";
      } else if (ad.form == TypeForm::Range) {
        gen_->out_ << "  call void @far_range_drop(i64 " << val << ")\n";
      } else if (ad.form == TypeForm::Function) {
        gen_->out_ << "  call void @far_closure_drop(i64 " << val << ")\n";
      } else {
        TypeDesc drop_ty;
        drop_ty.form = ad.form;
        if (isMemoryHandleDesc(drop_ty))
          emitMemDrop(memCtx(), ad.form, val);
        else if (isConcurrencyHandleDesc(drop_ty))
          emitConcDrop(concCtx(), ad.form, val);
      }
    }

    void flushBranchAutoDrops(Ctx& child, const std::unordered_map<std::string, VarInfo>& pre) {
      if (child.autodrop_frames_.empty())
        return;
      const auto& added = child.autodrop_frames_.back();
      for (size_t i = child.autodrop_scope_base_; i < added.size(); ++i) {
        if (pre.count(added[i].name))
          continue;
        if (child.env_.find(added[i].name) == child.env_.end())
          continue;
        std::string val = child.loadVar(added[i].name);
        emitAutoDrop(added[i], val);
        child.storeSlotValueNoDrop(added[i].name, "0");
      }
    }

    void spliceChildDefers(Ctx& child) {
      for (size_t i = child.defer_scope_base_; i < child.defer_stack_.size(); ++i)
        defer_stack_.push_back(child.defer_stack_[i]);
    }

    void mergeChild(Ctx& child, const std::unordered_map<std::string, VarInfo>* pre = nullptr) {
      if (pre)
        flushBranchAutoDrops(child, *pre);
      if (pre) {
        auto merged = child.env_;
        env_ = *pre;
        for (const auto& name : child.assigned_) {
          if (pre->count(name))
            env_[name] = merged.at(name);
        }
      } else {
        env_ = child.env_;
        const_int_locals_ = child.const_int_locals_;
        const_float_locals_ = child.const_float_locals_;
      }
      for (const auto& name : child.assigned_) {
        const_int_locals_.erase(name);
        const_float_locals_.erase(name);
      }
      spliceChildDefers(child);
      if (!autodrop_frames_.empty() && !child.autodrop_frames_.empty()) {
        const auto& added = child.autodrop_frames_.back();
        for (size_t i = child.autodrop_scope_base_; i < added.size(); ++i) {
          if (!pre || pre->count(added[i].name))
            autodrop_frames_.back().push_back(added[i]);
        }
      }
    }

    void removeAutoDrop(const std::string& name) {
      if (autodrop_frames_.empty())
        return;
      auto& frame = autodrop_frames_.back();
      frame.erase(std::remove_if(frame.begin(), frame.end(),
                                 [&](const AutoDrop& ad) { return ad.name == name; }),
                  frame.end());
    }

    std::string innermostFinallyLabel() const {
      for (auto it = try_stack_.rbegin(); it != try_stack_.rend(); ++it) {
        if (!it->has_finally)
          continue;
        if (emitting_finally_for_ >= 0 &&
            it->stack_index == static_cast<size_t>(emitting_finally_for_))
          continue;
        return it->finally_label;
      }
      return {};
    }

    void setPendingReturn(const Return& ret, bool defer_eval = false) {
      ensurePendingReturn();
      gen_->out_ << "  store i1 true, i1* %" << gen_->pending_ret_flag_ << "\n";
      if (defer_eval) {
        gen_->pending_ret_deferred_ = &ret;
        return;
      }
      gen_->pending_ret_deferred_ = nullptr;
      const char* ret_ty = llvmAbiTypeDesc(return_type_);
      if (!ret.has_value) {
        std::string zero =
            (isPrimitiveDesc(return_type_) && isFloatType(return_type_.primitive)) ? "0.0" : "0";
        gen_->out_ << "  store " << ret_ty << " " << zero << ", " << ret_ty << "* %"
                   << gen_->pending_ret_val_ << "\n";
        return;
      }
      std::string value = emitExpr(*ret.value);
      std::string abi = gen_->internalToAbiDesc(ret.value->type, return_type_, value);
      gen_->out_ << "  store " << ret_ty << " " << abi << ", " << ret_ty << "* %"
                 << gen_->pending_ret_val_ << "\n";
    }

    void emitReturnFromPending() {
      ensurePendingReturn();
      const char* ret_ty = llvmAbiTypeDesc(return_type_);
      flushDefers();
      if (gen_->pending_ret_deferred_) {
        const Return& ret = *gen_->pending_ret_deferred_;
        gen_->pending_ret_deferred_ = nullptr;
        if (!ret.has_value) {
          flushAllAutoDrops();
          std::string zero =
              (isPrimitiveDesc(return_type_) && isFloatType(return_type_.primitive)) ? "0.0" : "0";
          gen_->out_ << "  ret " << ret_ty << " " << zero << "\n";
        } else {
          std::string value = emitExpr(*ret.value);
          std::string abi = gen_->internalToAbiDesc(ret.value->type, return_type_, value);
          flushAllAutoDrops();
          gen_->out_ << "  ret " << ret_ty << " " << abi << "\n";
        }
        terminated = true;
        return;
      }
      std::string val = gen_->fresh("pretload");
      gen_->out_ << "  %" << val << " = load " << ret_ty << ", " << ret_ty << "* %"
                 << gen_->pending_ret_val_ << "\n";
      flushAllAutoDrops();
      gen_->out_ << "  ret " << ret_ty << " %" << val << "\n";
      terminated = true;
    }

    int findTryForFinallyLabel(const std::string& finally) const {
      for (int i = static_cast<int>(try_stack_.size()) - 1; i >= 0; --i) {
        if (try_stack_[i].finally_label == finally)
          return i;
      }
      return -1;
    }

    bool routePendingControlThroughOuterFinally(const TryLabels& labels) {
      for (size_t i = labels.stack_index; i > 0;) {
        --i;
        if (!try_stack_[i].has_finally)
          continue;
        if (labels.contained_in_finally_of == static_cast<int>(i)) {
          if (!labels.continue_label.empty()) {
            gen_->out_ << "  br label %" << labels.continue_label << "\n";
            return true;
          }
          continue;
        }
        gen_->out_ << "  br label %" << try_stack_[i].finally_label << "\n";
        return true;
      }
      return false;
    }

    void emitDispatchPendingThrowToCatch(const TryLabels& labels) {
      std::string tag = gen_->fresh("pthtagc");
      std::string val = gen_->fresh("pthvalc");
      gen_->out_ << "  %" << tag << " = load " << I64 << ", " << I64 << "* %" << gen_->pending_throw_tag_
                 << "\n";
      gen_->out_ << "  %" << val << " = load " << I64 << ", " << I64 << "* %" << gen_->pending_throw_val_
                 << "\n";
      gen_->out_ << "  call void @far_store_caught(i64 %" << tag << ", i64 %" << val << ")\n";
      gen_->out_ << "  br label %" << labels.resume_label << "\n";
      terminated = true;
    }

    void emitFinallyEpilogue(const TryLabels& labels) {
      if (gen_->pending_throw_init_) {
        std::string flag = gen_->fresh("pthf");
        gen_->out_ << "  %" << flag << " = load i1, i1* %" << gen_->pending_throw_flag_ << "\n";
        std::string throw_path = gen_->fresh("pthrow");
        std::string nxt = gen_->fresh("pthnext");
        gen_->out_ << "  br i1 %" << flag << ", label %" << throw_path << ", label %" << nxt << "\n";
        gen_->out_ << throw_path << ":\n";
        if (labels.has_catch) {
          emitDispatchPendingThrowToCatch(labels);
        } else {
          flushDefers();
          flushAllAutoDrops();
          emitTrySuccess(errCtx());
          emitRoutePendingThrowOutward(true);
        }
        gen_->out_ << nxt << ":\n";
        if (!gen_->pending_jump_init_ && !gen_->pending_ret_init_ && !gen_->pending_yield_init_) {
          gen_->out_ << "  br label %" << labels.after_label << "\n";
          return;
        }
      }
      if (gen_->pending_jump_init_) {
        std::string kind = gen_->fresh("pjload");
        gen_->out_ << "  %" << kind << " = load i8, i8* %" << gen_->pending_jump_kind_ << "\n";
        const bool has_break = !gen_->pending_break_label_.empty();
        const bool has_continue = !gen_->pending_continue_label_.empty();
        if (has_break && has_continue) {
          std::string brk = gen_->fresh("pjbrk");
          std::string cont = gen_->fresh("pjcont");
          std::string nxt = gen_->fresh("pjnext");
          gen_->out_ << "  switch i8 %" << kind << ", label %" << nxt << " [ i8 2, label %" << brk
                     << " i8 3, label %" << cont << " ]\n";
          gen_->out_ << brk << ":\n";
          gen_->out_ << "  store i8 0, i8* %" << gen_->pending_jump_kind_ << "\n";
          if (labels.break_finishes_try)
            gen_->out_ << "  br label %" << labels.after_label << "\n";
          else
            gen_->out_ << "  br label %" << gen_->pending_break_label_ << "\n";
          gen_->out_ << cont << ":\n";
          gen_->out_ << "  store i8 0, i8* %" << gen_->pending_jump_kind_ << "\n";
          gen_->out_ << "  br label %" << gen_->pending_continue_label_ << "\n";
          gen_->out_ << nxt << ":\n";
        } else if (has_break) {
          std::string brk = gen_->fresh("pjbrk");
          std::string nxt = gen_->fresh("pjnext");
          gen_->out_ << "  switch i8 %" << kind << ", label %" << nxt << " [ i8 2, label %" << brk
                     << " ]\n";
          gen_->out_ << brk << ":\n";
          gen_->out_ << "  store i8 0, i8* %" << gen_->pending_jump_kind_ << "\n";
          if (labels.break_finishes_try)
            gen_->out_ << "  br label %" << labels.after_label << "\n";
          else
            gen_->out_ << "  br label %" << gen_->pending_break_label_ << "\n";
          gen_->out_ << nxt << ":\n";
        } else if (has_continue) {
          std::string cont = gen_->fresh("pjcont");
          std::string nxt = gen_->fresh("pjnext");
          gen_->out_ << "  switch i8 %" << kind << ", label %" << nxt << " [ i8 3, label %" << cont
                     << " ]\n";
          gen_->out_ << cont << ":\n";
          gen_->out_ << "  store i8 0, i8* %" << gen_->pending_jump_kind_ << "\n";
          gen_->out_ << "  br label %" << gen_->pending_continue_label_ << "\n";
          gen_->out_ << nxt << ":\n";
        }
        if (!gen_->pending_ret_init_ && !gen_->pending_yield_init_) {
          gen_->out_ << "  br label %" << labels.after_label << "\n";
          return;
        }
      }
      if (gen_->pending_yield_init_) {
        std::string flag = gen_->fresh("pyf");
        gen_->out_ << "  %" << flag << " = load i1, i1* %" << gen_->pending_yield_flag_ << "\n";
        std::string yield_path = gen_->fresh("pyield");
        std::string done = labels.after_label;
        gen_->out_ << "  br i1 %" << flag << ", label %" << yield_path << ", label %" << done << "\n";
        gen_->out_ << yield_path << ":\n";
        if (labels.contained_in_finally_of >= 0 && !labels.continue_label.empty()) {
          gen_->out_ << "  br label %" << labels.continue_label << "\n";
          return;
        }
        for (size_t i = labels.stack_index; i > 0;) {
          --i;
          if (try_stack_[i].has_finally) {
            if (labels.contained_in_finally_of == static_cast<int>(i)) {
              if (!labels.continue_label.empty()) {
                gen_->out_ << "  br label %" << labels.continue_label << "\n";
                return;
              }
              continue;
            }
            gen_->out_ << "  br label %" << try_stack_[i].finally_label << "\n";
            return;
          }
        }
        emitYieldFromPending();
        return;
      }
      if (!gen_->pending_ret_init_) {
        if (!terminated)
          gen_->out_ << "  br label %" << labels.after_label << "\n";
        else
          gen_->out_ << "  unreachable\n";
        return;
      }
      std::string flag = gen_->fresh("pfl");
      gen_->out_ << "  %" << flag << " = load i1, i1* %" << gen_->pending_ret_flag_ << "\n";
      std::string ret_path = gen_->fresh("tryret");
      std::string done = labels.after_label;
      gen_->out_ << "  br i1 %" << flag << ", label %" << ret_path << ", label %" << done << "\n";
      gen_->out_ << ret_path << ":\n";
      if (labels.contained_in_finally_of >= 0 && !labels.continue_label.empty()) {
        gen_->out_ << "  br label %" << labels.continue_label << "\n";
        return;
      }
      for (size_t i = labels.stack_index; i > 0;) {
        --i;
        if (try_stack_[i].has_finally) {
          if (labels.contained_in_finally_of == static_cast<int>(i)) {
            if (!labels.continue_label.empty()) {
              gen_->out_ << "  br label %" << labels.continue_label << "\n";
              return;
            }
            continue;
          }
          gen_->out_ << "  br label %" << try_stack_[i].finally_label << "\n";
          return;
        }
      }
      emitReturnFromPending();
    }

    void emitRethrowCaught() {
      emitTrySuccess(errCtx());
      std::string tag = emitCaughtTagGlobal(errCtx());
      std::string val = emitCaughtValueGlobal(errCtx());
      gen_->out_ << "  call void @far_store_caught(i64 " << tag << ", i64 " << val << ")\n";
      setPendingThrowValues(tag, val);
      gen_->out_ << "  call void @far_mark_pending_throw()\n";
      if (!try_stack_.empty()) {
        emitRoutePendingThrowOutward(true);
        return;
      }
      gen_->out_ << "  call void @far_throw(i64 " << tag << ", i64 " << val << ")\n";
      gen_->out_ << "  unreachable\n";
    }

    void emitUncaughtThrowFromPending() {
      flushDefers();
      flushAllAutoDrops();
      emitTrySuccess(errCtx());
      emitThrowFromPending();
    }

    void emitUncaughtRethrow() { emitRethrowCaught(); }

    void pushScope() { autodrop_frames_.push_back({}); }

    void popScope() {
      if (autodrop_frames_.empty())
        return;
      for (const auto& ad : autodrop_frames_.back()) {
        std::string val = loadVar(ad.name);
        emitAutoDrop(ad, val);
        storeSlotValueNoDrop(ad.name, "0");
      }
      autodrop_frames_.pop_back();
    }

    void flushAllAutoDrops() {
      while (!autodrop_frames_.empty())
        popScope();
    }

    void flushDefersFrom(size_t base) {
      while (defer_stack_.size() > base) {
        const Expr* e = defer_stack_.back();
        defer_stack_.pop_back();
        emitExpr(*e);
      }
    }

    void flushScopeDefers() { flushDefersFrom(defer_scope_base_); }

    void flushDefers() { flushDefersFrom(0); }

    MemCodegenCtx memCtx() {
      return MemCodegenCtx{gen_->out_, [&](const std::string& p) { return gen_->fresh(p); },
                           [&](const Expr& e) { return emitExpr(e); },
                           [&](const Expr& e) { return exprType(e); }};
    }

    ConcCodegenCtx concCtx() {
      return ConcCodegenCtx{
          gen_->out_, [&](const std::string& p) { return gen_->fresh(p); },
          [&](const Expr& e) { return emitExpr(e); }, [&](const Expr& e) { return exprType(e); },
          [&](const std::string& sym, const Function* fn) { return fnPointerBitcastLlvm(sym, fn); }};
    }

    ErrCodegenCtx errCtx() {
      return ErrCodegenCtx{gen_->out_, [&](const std::string& p) { return gen_->fresh(p); },
                           [&](const Expr& e) { return emitExpr(e); }};
    }

    StrCodegenCtx strCtx() {
      return StrCodegenCtx{gen_->out_, [&](Expr& e) { return emitExpr(e); },
                           [&](const std::string& p) { return gen_->fresh(p); }};
    }

    std::string slotType(const VarInfo& v) const { return slotLlvmTypeDesc(v.type); }

    void ensureSlot(const std::string& name, const TypeDesc& type) {
      if (env_.find(name) == env_.end()) {
        std::string slot = gen_->fresh("slot");
        gen_->hoistedAlloca(slot, type);
        env_[name] = {type, slot};
      }
    }

    std::string loadVar(const std::string& name) {
      auto it = env_.find(name);
      if (it == env_.end())
        throw FarError("undefined variable '" + name + "'");
      const char* st = slotLlvmTypeDesc(it->second.type);
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = load " << st << ", " << st << "* %" << it->second.slot << "\n";
      return "%" + tmp;
    }

    void dropAutodropTargetIfAny(const std::string& name) {
      if (env_.find(name) == env_.end())
        return;
      for (auto rit = autodrop_frames_.rbegin(); rit != autodrop_frames_.rend(); ++rit) {
        for (const auto& ad : *rit) {
          if (ad.name != name)
            continue;
          std::string old = loadVar(name);
          emitAutoDrop(ad, old);
          return;
        }
      }
    }

    void storeSlotValueNoDrop(const std::string& name, const std::string& value) {
      auto it = env_.find(name);
      if (it == env_.end())
        throw FarError("undefined variable '" + name + "'");
      const char* st = slotLlvmTypeDesc(it->second.type);
      gen_->out_ << "  store " << st << " " << value << ", " << st << "* %" << it->second.slot << "\n";
    }

    void storeSlotValue(const std::string& name, const std::string& value) {
      dropAutodropTargetIfAny(name);
      storeSlotValueNoDrop(name, value);
    }

    void storeVar(const std::string& name, const std::string& value, const TypeDesc& type) {
      ensureSlot(name, type);
      env_[name].type = type;
      storeSlotValue(name, value);
      assigned_.insert(name);
      if (env_[name].is_param)
        registerOwnedAutoDrop(name, type);
    }

    std::string narrowIntToDesc(const TypeDesc& target_ty, const std::string& val) {
      if (isPrimitiveDesc(target_ty) && isIntegerType(target_ty.primitive) &&
          typeInfo(target_ty.primitive).bits < 64)
        return gen_->emitCastValueDesc(TypeDesc::prim(FarTypeId::I64), target_ty, val);
      return val;
    }



    void emitLet(const Let& let) {

      std::string value = emitExpr(*let.value);

      TypeDesc ty;
      auto existing = env_.find(let.name);
      if (existing != env_.end() && !let.explicit_type && explicit_typed_.count(let.name))
        ty = existing->second.type;
      else
        ty = let.explicit_type ? let.type : (let.value ? let.value->type : let.type);
      if (let.value)
        value = coerceToType(*let.value, ty, value);
      value = narrowIntToDesc(ty, value);
      storeVar(let.name, value, ty);
      if (let.explicit_type)
        explicit_typed_.insert(let.name);
      if (let.is_constexpr && let.value)
        trackConstLocal(let.name, *let.value);
      if (let.is_constexpr && let.value && let.value->kind == Expr::String &&
          let.value->string_lit.value.empty())
        constexpr_empty_locals_.insert(let.name);
      else if (let.is_constexpr && let.value && let.value->kind == Expr::ArrayLitExpr &&
               let.value->array_lit.elements.empty())
        constexpr_empty_locals_.insert(let.name);
      registerOwnedAutoDrop(let.name, ty);

    }



    void completeTryForControlToFinally(const std::string& /*finally_label*/) {
      // try depth is decremented once at try.after, not when entering finally
    }

    void ensurePendingJump() {
      if (gen_->pending_jump_init_)
        return;
      gen_->pending_jump_kind_ = gen_->fresh("pjump");
      gen_->hoisted_allocas_ << "  %" << gen_->pending_jump_kind_ << " = alloca i8\n";
      gen_->pending_jump_init_ = true;
    }

    void setPendingBreak(const std::string& label) {
      ensurePendingJump();
      gen_->pending_break_label_ = label;
      gen_->out_ << "  store i8 2, i8* %" << gen_->pending_jump_kind_ << "\n";
    }

    void setPendingContinue(const std::string& label) {
      ensurePendingJump();
      gen_->pending_continue_label_ = label;
      gen_->out_ << "  store i8 3, i8* %" << gen_->pending_jump_kind_ << "\n";
    }

    void ensurePendingYield() {
      if (gen_->pending_yield_init_)
        return;
      gen_->pending_yield_flag_ = gen_->fresh("pyflag");
      gen_->pending_yield_val_ = gen_->fresh("pyval");
      gen_->hoisted_allocas_ << "  %" << gen_->pending_yield_flag_ << " = alloca i1\n";
      gen_->hoisted_allocas_ << "  %" << gen_->pending_yield_val_ << " = alloca " << I64 << "\n";
      gen_->pending_yield_init_ = true;
    }

    void setPendingYield(const std::string& value) {
      ensurePendingYield();
      gen_->out_ << "  store i1 true, i1* %" << gen_->pending_yield_flag_ << "\n";
      gen_->out_ << "  store " << I64 << " " << value << ", " << I64 << "* %" << gen_->pending_yield_val_
                 << "\n";
    }

    void emitYieldFromPending() {
      ensurePendingYield();
      flushScopeDefers();
      std::string val = gen_->fresh("pyload");
      gen_->out_ << "  %" << val << " = load " << I64 << ", " << I64 << "* %" << gen_->pending_yield_val_
                 << "\n";
      gen_->out_ << "  ret " << I64 << " %" << val << "\n";
      terminated = true;
    }

    void ensurePendingThrow() {
      if (gen_->pending_throw_init_)
        return;
      gen_->pending_throw_flag_ = gen_->fresh("pthrowflag");
      gen_->pending_throw_tag_ = gen_->fresh("pthrowtag");
      gen_->pending_throw_val_ = gen_->fresh("pthrowval");
      gen_->hoisted_allocas_ << "  %" << gen_->pending_throw_flag_ << " = alloca i1\n";
      gen_->hoisted_allocas_ << "  %" << gen_->pending_throw_tag_ << " = alloca " << I64 << "\n";
      gen_->hoisted_allocas_ << "  %" << gen_->pending_throw_val_ << " = alloca " << I64 << "\n";
      gen_->pending_throw_init_ = true;
    }

    void setPendingThrow(int64_t tag, const std::string& val) {
      ensurePendingThrow();
      gen_->out_ << "  store i1 true, i1* %" << gen_->pending_throw_flag_ << "\n";
      gen_->out_ << "  store " << I64 << " " << tag << ", " << I64 << "* %" << gen_->pending_throw_tag_
                 << "\n";
      gen_->out_ << "  store " << I64 << " " << val << ", " << I64 << "* %" << gen_->pending_throw_val_
                 << "\n";
    }

    void setPendingThrowValues(const std::string& tag_val, const std::string& val_val) {
      ensurePendingThrow();
      gen_->out_ << "  store i1 true, i1* %" << gen_->pending_throw_flag_ << "\n";
      gen_->out_ << "  store " << I64 << " " << tag_val << ", " << I64 << "* %" << gen_->pending_throw_tag_
                 << "\n";
      gen_->out_ << "  store " << I64 << " " << val_val << ", " << I64 << "* %" << gen_->pending_throw_val_
                 << "\n";
    }

    void emitThrowFromPending() {
      ensurePendingThrow();
      std::string tag = gen_->fresh("pthtag");
      std::string val = gen_->fresh("pthval");
      gen_->out_ << "  %" << tag << " = load " << I64 << ", " << I64 << "* %" << gen_->pending_throw_tag_
                 << "\n";
      gen_->out_ << "  %" << val << " = load " << I64 << ", " << I64 << "* %" << gen_->pending_throw_val_
                 << "\n";
      gen_->out_ << "  call void @far_store_caught(i64 %" << tag << ", i64 %" << val << ")\n";
      gen_->out_ << "  call void @far_mark_pending_throw()\n";
      emitRoutePendingThrowOutward(false);
    }

    void emitRoutePendingThrowOutward(bool popped_current) {
      size_t start = try_stack_.size();
      if (popped_current && start > 0)
        --start;
      for (size_t i = start; i > 0; --i) {
        size_t idx = i - 1;
        if (emitting_catch_for_ >= 0 && static_cast<size_t>(emitting_catch_for_) == idx)
          continue;
        const TryLabels& labels = try_stack_[idx];
        if (labels.has_catch) {
          if (!labels.unwind_slot.empty())
            gen_->out_ << "  store i1 true, i1* %" << labels.unwind_slot << "\n";
          std::string tag = gen_->fresh("pthtago");
          std::string val = gen_->fresh("pthvalo");
          gen_->out_ << "  %" << tag << " = load " << I64 << ", " << I64 << "* %"
                     << gen_->pending_throw_tag_ << "\n";
          gen_->out_ << "  %" << val << " = load " << I64 << ", " << I64 << "* %"
                     << gen_->pending_throw_val_ << "\n";
          gen_->out_ << "  call void @far_store_caught(i64 %" << tag << ", i64 %" << val << ")\n";
          gen_->out_ << "  br label %" << labels.resume_label << "\n";
          terminated = true;
          return;
        }
        if (labels.has_finally) {
          // A try nested inside this finally must resume at that inner try's continue
          // label (finally epilogue), not re-enter the finally body.
          size_t nested_resume = try_stack_.size();
          for (size_t j = idx + 1; j < try_stack_.size(); ++j) {
            if (try_stack_[j].contained_in_finally_of == static_cast<int>(idx))
              nested_resume = j;
          }
          if (nested_resume < try_stack_.size() &&
              !try_stack_[nested_resume].continue_label.empty()) {
            gen_->out_ << "  br label %" << try_stack_[nested_resume].continue_label << "\n";
            terminated = true;
            return;
          }
          gen_->out_ << "  br label %" << labels.finally_label << "\n";
          terminated = true;
          return;
        }
      }
      std::string tag = gen_->fresh("pthtagu");
      std::string val = gen_->fresh("pthvalu");
      gen_->out_ << "  %" << tag << " = load " << I64 << ", " << I64 << "* %" << gen_->pending_throw_tag_
                 << "\n";
      gen_->out_ << "  %" << val << " = load " << I64 << ", " << I64 << "* %" << gen_->pending_throw_val_
                 << "\n";
      gen_->out_ << "  call void @far_throw(i64 %" << tag << ", i64 %" << val << ")\n";
      gen_->out_ << "  unreachable\n";
      terminated = true;
    }

    void emitReturnAfterPendingThrow() {
      std::string pending = gen_->fresh("pthret");
      gen_->out_ << "  %" << pending << " = call i32 @far_pending_throw_active()\n";
      std::string cmp = gen_->fresh("pthretcmp");
      std::string ret_path = gen_->fresh("pthretpath");
      std::string cont = gen_->fresh("pthretcont");
      gen_->out_ << "  %" << cmp << " = icmp ne i32 %" << pending << ", 0\n";
      gen_->out_ << "  br i1 %" << cmp << ", label %" << ret_path << ", label %" << cont << "\n";
      gen_->out_ << ret_path << ":\n";
      const char* ret_ty = llvmAbiTypeDesc(return_type_);
      std::string zero =
          (isPrimitiveDesc(return_type_) && isFloatType(return_type_.primitive)) ? "0.0" : "0";
      gen_->out_ << "  ret " << ret_ty << " " << zero << "\n";
      gen_->out_ << cont << ":\n";
    }

    void emitPendingThrowResumeCheck() {
      if (try_stack_.empty())
        return;
      const TryLabels& labels = try_stack_.back();
      std::string pending = gen_->fresh("pthchk");
      gen_->out_ << "  %" << pending << " = call i32 @far_pending_throw_active()\n";
      std::string cmp = gen_->fresh("pthchkcmp");
      std::string catch_path = gen_->fresh("pthcatch");
      std::string cont = gen_->fresh("pthcont");
      gen_->out_ << "  %" << cmp << " = icmp ne i32 %" << pending << ", 0\n";
      gen_->out_ << "  br i1 %" << cmp << ", label %" << catch_path << ", label %" << cont << "\n";
      gen_->out_ << catch_path << ":\n";
      gen_->out_ << "  call void @far_clear_pending_throw()\n";
      if (!labels.unwind_slot.empty())
        gen_->out_ << "  store i1 true, i1* %" << labels.unwind_slot << "\n";
      gen_->out_ << "  br label %" << labels.resume_label << "\n";
      gen_->out_ << cont << ":\n";
    }

    void completeTryForReturnToFinally(const std::string& finally_label) {
      completeTryForControlToFinally(finally_label);
    }

    void emitReturn(const Return& ret) {
      std::string finally = innermostFinallyLabel();
      if (!finally.empty()) {
        completeTryForReturnToFinally(finally);
        setPendingReturn(ret, true);
        flushScopeDefers();
        gen_->out_ << "  br label %" << finally << "\n";
        terminated = true;
        return;
      }
      const char* ret_ty = llvmAbiTypeDesc(return_type_);
      if (!ret.has_value) {
        flushDefers();
        flushAllAutoDrops();
        std::string zero = (isPrimitiveDesc(return_type_) && isFloatType(return_type_.primitive)) ? "0.0" : "0";
        gen_->out_ << "  ret " << ret_ty << " " << zero << "\n";
      } else {
        flushDefers();
        std::string value = emitExpr(*ret.value);
        std::string abi = gen_->internalToAbiDesc(ret.value->type, return_type_, value);
        flushAllAutoDrops();
        gen_->out_ << "  ret " << ret_ty << " " << abi << "\n";
      }
      terminated = true;
    }

    void emitYield(const YieldStmt& y) {
      std::string finally = innermostFinallyLabel();
      if (!finally.empty()) {
        completeTryForControlToFinally(finally);
        flushScopeDefers();
        std::string value = y.has_value ? emitExpr(*y.value) : std::string("0");
        setPendingYield(value);
        gen_->out_ << "  br label %" << finally << "\n";
        terminated = true;
        return;
      }
      flushScopeDefers();
      std::string value = y.has_value ? emitExpr(*y.value) : std::string("0");
      gen_->out_ << "  ret " << I64 << " " << value << "\n";
      terminated = true;
    }

    TypeDesc exprType(const Expr& expr) const {
      if (expr.kind == Expr::Variable) {
        auto it = env_.find(expr.var.name);
        if (it != env_.end())
          return it->second.type;
      }
      if (expr.kind == Expr::Binary && expr.bin_op.op == "+") {
        if (isPrimTy(exprType(*expr.bin_op.left), FarTypeId::String) ||
            isPrimTy(exprType(*expr.bin_op.right), FarTypeId::String))
          return TypeDesc::prim(FarTypeId::String);
      }
      return expr.type;
    }

    AggCodegenCtx aggCtx() {
      AggCodegenCtx ctx{gen_->out_, [&](const std::string& p) { return gen_->fresh(p); },
                        [&](const Expr& e) { return emitExpr(e); },
                        [&](const Expr& e) { return emitExprAsDouble(e); },
                        [&](const Expr& e) { return emitExpr(e); },
                        [&](const Expr& e) { return primTy(exprType(e)); }};
      return ctx;
    }

    CollCodegenCtx collCtx() {
      return CollCodegenCtx{gen_->out_, [&](const std::string& p) { return gen_->fresh(p); },
                            [&](const Expr& e) { return emitExpr(e); },
                            [&](const Expr& e) { return exprType(e); }};
    }

    ObjCodegenCtx userCtx() {
      return ObjCodegenCtx{gen_->out_, [&](const std::string& p) { return gen_->fresh(p); }};
    }



    std::string emitExprAsDouble(const Expr& expr) {
      TypeDesc ty = exprType(expr);
      std::string value = emitExpr(expr);
      if (isPrimitiveDesc(ty) && isFloatType(ty.primitive))
        return value;
      return gen_->emitIntToF64Desc(ty, value);
    }

    std::string emitExprAsString(const Expr& expr) {
      TypeDesc ty = exprType(expr);
      if (isPrimTy(ty, FarTypeId::String))
        return emitExpr(expr);
      std::string value = emitExpr(expr);
      std::string str = gen_->fresh("str");
      if (isPrimTy(ty, FarTypeId::Char)) {
        std::string ch = gen_->fresh("ch");
        gen_->out_ << "  %" << ch << " = trunc " << I64 << " " << value << " to i16\n";
        gen_->out_ << "  %" << str << " = call i8* @far_char_to_str(i16 %" << ch << ")\n";
      } else if (isPrimTy(ty, FarTypeId::Bool) || exprPrintsAsBool(expr)) {
        gen_->out_ << "  %" << str << " = call i8* @far_bool_to_str(" << I64 << " " << value << ")\n";
      } else if (isPrimTy(ty, FarTypeId::F32)) {
        std::string fval = gen_->fresh();
        gen_->out_ << "  %" << fval << " = fptrunc " << F64 << " " << value << " to " << F32 << "\n";
        std::string promoted = gen_->fresh();
        gen_->out_ << "  %" << promoted << " = fpext " << F32 << " %" << fval << " to " << F64 << "\n";
        gen_->out_ << "  %" << str << " = call i8* @far_f64_to_str(double %" << promoted << ")\n";
      } else if (isPrimTy(ty, FarTypeId::F64)) {
        gen_->out_ << "  %" << str << " = call i8* @far_f64_to_str(double " << value << ")\n";
      } else if (isPrimitiveDesc(ty)) {
        return gen_->emitNumericToString(ty, value);
      } else {
        gen_->out_ << "  %" << str << " = call i8* @far_i64_to_str(" << I64 << " " << value << ")\n";
      }
      std::string as_i64 = gen_->fresh();
      gen_->out_ << "  %" << as_i64 << " = ptrtoint i8* %" << str << " to " << I64 << "\n";
      return "%" + as_i64;
    }

    void emitPrint(const Print& print) {
      std::string value = emitExpr(*print.value);
      TypeDesc ty = exprType(*print.value);
      if (isPrimTy(ty, FarTypeId::String)) {
        std::string ptr = gen_->fresh("sp");
        gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << value << " to i8*\n";
        gen_->out_ << "  call void @far_print_str(i8* %" << ptr << ")\n";
      } else if (isPrimTy(ty, FarTypeId::Char)) {
        std::string ch = gen_->fresh("ch");
        gen_->out_ << "  %" << ch << " = trunc " << I64 << " " << value << " to i16\n";
        std::string str = gen_->fresh("str");
        gen_->out_ << "  %" << str << " = call i8* @far_char_to_str(i16 %" << ch << ")\n";
        gen_->out_ << "  call void @far_print_str(i8* %" << str << ")\n";
      } else if (ty.form == TypeForm::Array || ty.form == TypeForm::List || ty.form == TypeForm::Slice ||
                 isPrimTy(ty, FarTypeId::Arr)) {
        emitCollectionPrint(collCtx(), ty, value);
      } else if (isPrimTy(ty, FarTypeId::F32)) {
        std::string fval = gen_->fresh();
        gen_->out_ << "  %" << fval << " = fptrunc " << F64 << " " << value << " to " << F32 << "\n";
        gen_->out_ << "  call void @far_print_f32(float %" << fval << ")\n";
      } else if (isPrimTy(ty, FarTypeId::F16)) {
        std::string fval = gen_->fresh();
        gen_->out_ << "  %" << fval << " = fptrunc " << F64 << " " << value << " to " << F32 << "\n";
        gen_->out_ << "  call void @far_print_f32(float %" << fval << ")\n";
      } else if (isPrimTy(ty, FarTypeId::F64)) {
        gen_->out_ << "  call void @far_print_f64(double " << value << ")\n";
      } else if (exprPrintsAsBool(*print.value)) {
        emitPrintBoolValue(gen_->out_, [&](const std::string& p) { return gen_->fresh(p); }, value);
      } else if (isAggregateDesc(ty)) {
        emitAggregatePrint(aggCtx(), aggregateDescId(ty), value);
      } else if (isPrimitiveDesc(ty) && isIntegerType(ty.primitive) && !typeInfo(ty.primitive).is_signed) {
        std::string str = gen_->emitNumericToString(ty, value);
        std::string ptr = gen_->fresh("sp");
        gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << str << " to i8*\n";
        gen_->out_ << "  call void @far_print_str(i8* %" << ptr << ")\n";
      } else {
        gen_->out_ << "  call void @far_print_i64(" << I64 << " " << value << ")\n";
      }
    }



    std::string emitIsNullish(const Expr& expr, const std::string& val) {
      std::string is_n = gen_->fresh();
      if (isPrimitiveDesc(exprType(expr)) && isFloatType(exprType(expr).primitive)) {
        std::string is_nan = gen_->fresh();
        gen_->out_ << "  %" << is_nan << " = fcmp uno " << F64 << " " << val << ", " << val << "\n";
        std::string is_zero = gen_->fresh();
        gen_->out_ << "  %" << is_zero << " = fcmp oeq " << F64 << " " << val << ", 0.0\n";
        gen_->out_ << "  %" << is_n << " = or i1 %" << is_nan << ", %" << is_zero << "\n";
      } else {
        gen_->out_ << "  %" << is_n << " = icmp eq " << I64 << " " << val << ", 0\n";
      }
      return "%" + is_n;
    }

    std::string emitBranchCond(const Expr& expr) {
      std::string value = emitExpr(expr);
      return emitTruthyCond(exprType(expr), value);
    }

    std::string emitTruthyCond(const TypeDesc& ty, const std::string& val) {
      std::string cond = gen_->fresh("cond");
      if (isPrimitiveDesc(ty) && isFloatType(ty.primitive)) {
        std::string cmp_val = val;
        if (isPrimTy(ty, FarTypeId::F32)) {
          std::string ext = gen_->fresh("f32td");
          gen_->out_ << "  %" << ext << " = fpext " << F32 << " " << val << " to " << F64 << "\n";
          cmp_val = "%" + ext;
        }
        std::string is_nan = gen_->fresh();
        gen_->out_ << "  %" << is_nan << " = fcmp uno " << F64 << " " << cmp_val << ", " << cmp_val << "\n";
        std::string is_zero = gen_->fresh();
        gen_->out_ << "  %" << is_zero << " = fcmp oeq " << F64 << " " << cmp_val << ", 0.0\n";
        std::string nullish = gen_->fresh();
        gen_->out_ << "  %" << nullish << " = or i1 %" << is_nan << ", %" << is_zero << "\n";
        gen_->out_ << "  %" << cond << " = xor i1 %" << nullish << ", true\n";
      } else {
        gen_->out_ << "  %" << cond << " = icmp ne " << I64 << " " << val << ", 0\n";
      }
      return "%" + cond;
    }



    void mergeBranches(const std::unordered_map<std::string, VarInfo>& pre,

                       Ctx& then_ctx, const std::string& then_label,

                       Ctx& else_ctx, const std::string& else_label) {

      std::unordered_set<std::string> names;

      for (const auto& [k, _] : pre) names.insert(k);

      for (const auto& [k, _] : then_ctx.env_) names.insert(k);

      for (const auto& [k, _] : else_ctx.env_) names.insert(k);

      for (const auto& n : then_ctx.assigned_) names.insert(n);

      for (const auto& n : else_ctx.assigned_) names.insert(n);



      for (const auto& name : names) {

        bool in_then = then_ctx.assigned_.count(name) > 0;

        bool in_else = else_ctx.assigned_.count(name) > 0;

        if (!in_then && !in_else) continue;



        TypeDesc merged_ty = TypeDesc::prim(FarTypeId::I64);
        if (then_ctx.env_.count(name))
          merged_ty = then_ctx.env_.at(name).type;
        else if (else_ctx.env_.count(name))
          merged_ty = else_ctx.env_.at(name).type;
        else if (pre.count(name))
          merged_ty = pre.at(name).type;

        ensureSlot(name, merged_ty);
        env_[name].type = merged_ty;

        std::string slot = env_[name].slot;



        auto pre_val = [&](const std::string& nm) -> std::string {

          auto it = pre.find(nm);

          if (it != pre.end()) {

            std::string tmp = gen_->fresh();

            const char* st = slotLlvmTypeDesc(it->second.type);

            gen_->out_ << "  %" << tmp << " = load " << st << ", " << st << "* %" << it->second.slot << "\n";

            return "%" + tmp;

          }

          return "0";

        };



        std::string v_then = in_then ? then_ctx.loadVar(name) : pre_val(name);

        std::string v_else = in_else ? else_ctx.loadVar(name) : pre_val(name);



        std::string phi = gen_->fresh("phi");

        const char* st = slotLlvmTypeDesc(env_[name].type);

        gen_->out_ << "  %" << phi << " = phi " << st << " [ " << v_then << ", %" << then_label

                   << " ], [ " << v_else << ", %" << else_label << " ]\n";

        gen_->out_ << "  store " << st << " %" << phi << ", " << st << "* %" << slot << "\n";

        if (then_ctx.env_.count(name)) env_[name].type = then_ctx.env_[name].type;

        else if (else_ctx.env_.count(name)) env_[name].type = else_ctx.env_[name].type;

      }

      for (const auto& [k, v] : then_ctx.env_) env_[k] = v;

      for (const auto& [k, v] : else_ctx.env_) env_[k] = v;

      for (const auto& n : then_ctx.assigned_) {
        const_int_locals_.erase(n);
        const_float_locals_.erase(n);
      }
      for (const auto& n : else_ctx.assigned_) {
        const_int_locals_.erase(n);
        const_float_locals_.erase(n);
      }

    }



    enum class ConstBool { Unknown, False, True };

    void trackConstLocal(const std::string& name, const Expr& value) {
      if (value.kind == Expr::Int) {
        const_int_locals_[name] = value.int_lit.value;
        const_float_locals_.erase(name);
      } else if (value.kind == Expr::Float && !std::isnan(value.float_lit.value)) {
        const_float_locals_[name] = value.float_lit.value;
        const_int_locals_.erase(name);
      } else if (value.kind == Expr::EnumVariantExprK) {
        const_int_locals_[name] = value.enum_variant.value;
        const_float_locals_.erase(name);
      } else {
        const_int_locals_.erase(name);
        const_float_locals_.erase(name);
      }
    }

    ConstBool constBoolFromExpr(const Expr& e) const {
      if (e.kind == Expr::Int)
        return e.int_lit.value != 0 ? ConstBool::True : ConstBool::False;
      if (e.kind == Expr::Float) {
        if (std::isnan(e.float_lit.value))
          return ConstBool::False;
        return e.float_lit.value != 0.0 ? ConstBool::True : ConstBool::False;
      }
      if (e.kind == Expr::String)
        return e.string_lit.value.empty() ? ConstBool::False : ConstBool::True;
      if (e.kind == Expr::Variable) {
        auto fit = const_float_locals_.find(e.var.name);
        if (fit != const_float_locals_.end()) {
          if (std::isnan(fit->second))
            return ConstBool::False;
          return fit->second != 0.0 ? ConstBool::True : ConstBool::False;
        }
        auto it = const_int_locals_.find(e.var.name);
        if (it != const_int_locals_.end())
          return it->second != 0 ? ConstBool::True : ConstBool::False;
        return ConstBool::Unknown;
      }
      if (e.kind == Expr::Binary && e.bin_op.op == "!" && e.bin_op.left->kind == Expr::Int &&
          e.bin_op.left->int_lit.value == 0) {
        ConstBool inner = constBoolFromExpr(*e.bin_op.right);
        if (inner == ConstBool::Unknown)
          return ConstBool::Unknown;
        return inner == ConstBool::True ? ConstBool::False : ConstBool::True;
      }
      if (e.kind == Expr::Binary && (e.bin_op.op == "and" || e.bin_op.op == "&&")) {
        ConstBool l = constBoolFromExpr(*e.bin_op.left);
        if (l == ConstBool::False)
          return ConstBool::False;
        if (l == ConstBool::True) {
          ConstBool r = constBoolFromExpr(*e.bin_op.right);
          if (r == ConstBool::Unknown)
            return ConstBool::True;
          return r;
        }
        return ConstBool::Unknown;
      }
      if (e.kind == Expr::Binary && (e.bin_op.op == "or" || e.bin_op.op == "||")) {
        ConstBool l = constBoolFromExpr(*e.bin_op.left);
        if (l == ConstBool::True)
          return ConstBool::True;
        if (l == ConstBool::False) {
          ConstBool r = constBoolFromExpr(*e.bin_op.right);
          if (r == ConstBool::Unknown)
            return ConstBool::False;
          return r;
        }
        return ConstBool::Unknown;
      }
      return ConstBool::Unknown;
    }

    std::optional<double> constFloatFromExpr(const Expr& e) const {
      if (e.kind == Expr::Float)
        return e.float_lit.value;
      if (e.kind == Expr::Variable) {
        auto it = const_float_locals_.find(e.var.name);
        if (it != const_float_locals_.end())
          return it->second;
      }
      return std::nullopt;
    }

    bool patternCertainlyFailsConstFloat(const Pattern& pat, double v) const {
      if (pat.kind != PatKind::Literal)
        return false;
      if (pat.literal_is_float) {
        if (std::isnan(v))
          return !std::isnan(pat.float_literal);
        if (std::isnan(pat.float_literal))
          return true;
        return pat.float_literal != v;
      }
      if (std::isnan(v) || std::isinf(v))
        return true;
      return static_cast<double>(pat.literal) != v;
    }

    bool patternMatchesConstFloatSpecific(const Pattern& pat, double v) const {
      if (pat.kind != PatKind::Literal)
        return false;
      if (pat.literal_is_float) {
        if (std::isnan(pat.float_literal))
          return std::isnan(v);
        return pat.float_literal == v;
      }
      if (std::isnan(v) || std::isinf(v))
        return false;
      return static_cast<double>(pat.literal) == v;
    }

    bool patternMatchesConstFloat(const Pattern& pat, double v) const {
      if (patternMatchesConstFloatSpecific(pat, v))
        return true;
      if (isCatchAllPattern(pat))
        return true;
      return false;
    }

    std::optional<int64_t> constIntFromExpr(const Expr& e) const {
      if (e.kind == Expr::Int)
        return e.int_lit.value;
      if (e.kind == Expr::Char)
        return static_cast<int64_t>(e.char_lit.value);
      if (e.kind == Expr::Variable) {
        auto it = const_int_locals_.find(e.var.name);
        if (it != const_int_locals_.end())
          return it->second;
      }
      return std::nullopt;
    }

    bool patternCertainlyFailsConstInt(const Pattern& pat, int64_t v) const {
      if (pat.kind == PatKind::Literal) {
        if (pat.literal_is_float)
          return false;
        return pat.literal != v;
      }
      if (pat.kind == PatKind::EnumVariant) {
        int64_t pv = pat.variant_value;
        if (pv < 0)
          pv = gen_->obj_reg_.enumVariantValue(pat.type_name, pat.variant);
        if (pv < 0)
          return false;
        return pv != v;
      }
      return false;
    }

    static bool isCatchAllPattern(const Pattern& pat) {
      return pat.kind == PatKind::Wildcard || pat.kind == PatKind::Bind;
    }

    static bool isMatchWildcardPattern(const Pattern& pat) {
      return pat.kind == PatKind::Wildcard;
    }

    bool patternMatchesConstIntSpecific(const Pattern& pat, int64_t v) const {
      if (pat.kind == PatKind::Literal) {
        if (pat.literal_is_float)
          return false;
        return pat.literal == v;
      }
      if (pat.kind == PatKind::EnumVariant) {
        int64_t pv = pat.variant_value;
        if (pv < 0)
          pv = gen_->obj_reg_.enumVariantValue(pat.type_name, pat.variant);
        if (pv < 0)
          return false;
        return pv == v;
      }
      return false;
    }

    bool patternMatchesConstInt(const Pattern& pat, int64_t v) const {
      if (patternMatchesConstIntSpecific(pat, v))
        return true;
      if (isCatchAllPattern(pat))
        return true;
      return false;
    }

    static std::vector<size_t> matchArmOrder(const MatchStmt& m) {
      std::vector<size_t> order;
      for (size_t i = 0; i < m.arms.size(); ++i) {
        if (m.arms[i].pat && !isMatchWildcardPattern(*m.arms[i].pat))
          order.push_back(i);
      }
      for (size_t i = 0; i < m.arms.size(); ++i) {
        if (m.arms[i].pat && isMatchWildcardPattern(*m.arms[i].pat))
          order.push_back(i);
      }
      return order;
    }

    void emitMatchFromArm(const std::vector<size_t>& order, size_t start, const MatchStmt& m, const std::string& scrut, const TypeDesc& sty,
                          const std::unordered_map<std::string, VarInfo>& pre, const std::string& done_label,
                          const std::string& fail_label) {
      std::string next_label;
      for (size_t oi = start; oi < order.size(); ++oi) {
        const auto& arm = m.arms[order[oi]];
        std::string body_label = gen_->fresh("matchbody");
        next_label = (oi + 1 < order.size()) ? gen_->fresh("matchnext") : fail_label;
        PatTestResult tr = emitPatternTest(patCtx(), *arm.pat, scrut, sty);
        if (!tr.always)
          gen_->out_ << "  br i1 " << tr.cond << ", label %" << body_label << ", label %" << next_label << "\n";
        else
          gen_->out_ << "  br label %" << body_label << "\n";
        gen_->out_ << body_label << ":\n";
        Ctx body_ctx = fork();
        body_ctx.env_ = pre;
        for (const auto& b : tr.binds)
          body_ctx.storeVar(b.name, b.value, b.type);
        for (const auto& s : arm.body)
          body_ctx.emitStmt(*s);
        mergeChild(body_ctx, &pre);
        if (!body_ctx.terminated)
          gen_->out_ << "  br label %" << done_label << "\n";
        if (next_label != fail_label)
          gen_->out_ << next_label << ":\n";
      }
      gen_->out_ << fail_label << ":\n";
      gen_->out_ << "  call void @far_panic(i64 0)\n";
      gen_->out_ << "  unreachable\n";
    }

    void emitMatch(const MatchStmt& m) {
      std::optional<int64_t> const_scrut = constIntFromExpr(*m.scrutinee);
      std::optional<double> const_scrut_f = const_scrut ? std::nullopt : constFloatFromExpr(*m.scrutinee);
      std::string scrut;
      if (const_scrut)
        scrut = std::to_string(*const_scrut);
      else if (const_scrut_f)
        scrut = gen_->formatDoubleLiteral(*const_scrut_f);
      else
        scrut = emitExpr(*m.scrutinee);
      TypeDesc sty = exprType(*m.scrutinee);
      std::string done_label = gen_->fresh("matchdone");
      std::string fail_label = gen_->fresh("matchfail");
      struct SwitchBreakGuard {
        Ctx* ctx;
        bool active;
        explicit SwitchBreakGuard(Ctx* c, bool on, const std::string& label) : ctx(c), active(on) {
          if (active)
            ctx->switch_break_stack_.push_back(label);
        }
        ~SwitchBreakGuard() {
          if (active)
            ctx->switch_break_stack_.pop_back();
        }
      } switch_guard(this, m.is_switch, done_label);
      auto pre = env_;

      if (const_scrut_f) {
        for (size_t i = 0; i < m.arms.size(); ++i) {
          const auto& arm = m.arms[i];
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            continue;
          if (arm.pat && patternCertainlyFailsConstFloat(*arm.pat, *const_scrut_f))
            continue;
          if (arm.pat && patternMatchesConstFloat(*arm.pat, *const_scrut_f)) {
            PatTestResult tr = emitPatternTest(patCtx(), *arm.pat, scrut, sty);
            Ctx body_ctx = fork();
            body_ctx.env_ = pre;
            for (const auto& b : tr.binds)
              body_ctx.storeVar(b.name, b.value, b.type);
            for (const auto& s : arm.body)
              body_ctx.emitStmt(*s);
            mergeChild(body_ctx, &pre);
            if (body_ctx.terminated && !body_ctx.switch_break_arm_)
              terminated = true;
            if (!body_ctx.terminated) {
              gen_->out_ << "  br label %" << done_label << "\n";
              gen_->out_ << done_label << ":\n";
            } else if (body_ctx.switch_break_arm_) {
              gen_->out_ << done_label << ":\n";
            }
            return;
          }
        }
        for (size_t i = 0; i < m.arms.size(); ++i) {
          const auto& arm = m.arms[i];
          if (!arm.pat || !isMatchWildcardPattern(*arm.pat))
            continue;
          PatTestResult tr = emitPatternTest(patCtx(), *arm.pat, scrut, sty);
          Ctx body_ctx = fork();
          body_ctx.env_ = pre;
          for (const auto& b : tr.binds)
            body_ctx.storeVar(b.name, b.value, b.type);
          for (const auto& s : arm.body)
            body_ctx.emitStmt(*s);
          mergeChild(body_ctx, &pre);
          if (body_ctx.terminated && !body_ctx.switch_break_arm_)
            terminated = true;
          if (!body_ctx.terminated) {
            gen_->out_ << "  br label %" << done_label << "\n";
            gen_->out_ << done_label << ":\n";
          } else if (body_ctx.switch_break_arm_) {
            gen_->out_ << done_label << ":\n";
          }
          return;
        }
        gen_->out_ << "  call void @far_panic(i64 0)\n";
        gen_->out_ << "  unreachable\n";
        gen_->out_ << done_label << ":\n";
        return;
      }

      if (const_scrut) {
        for (size_t i = 0; i < m.arms.size(); ++i) {
          const auto& arm = m.arms[i];
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            continue;
          if (arm.pat && patternCertainlyFailsConstInt(*arm.pat, *const_scrut))
            continue;
          if (arm.pat && patternMatchesConstInt(*arm.pat, *const_scrut)) {
            PatTestResult tr = emitPatternTest(patCtx(), *arm.pat, scrut, sty);
            Ctx body_ctx = fork();
            body_ctx.env_ = pre;
            for (const auto& b : tr.binds)
              body_ctx.storeVar(b.name, b.value, b.type);
            for (const auto& s : arm.body)
              body_ctx.emitStmt(*s);
            mergeChild(body_ctx, &pre);
            if (body_ctx.terminated && !body_ctx.switch_break_arm_)
              terminated = true;
            if (!body_ctx.terminated) {
              gen_->out_ << "  br label %" << done_label << "\n";
              gen_->out_ << done_label << ":\n";
            } else if (body_ctx.switch_break_arm_) {
              gen_->out_ << done_label << ":\n";
            }
            return;
          }
        }
        for (size_t i = 0; i < m.arms.size(); ++i) {
          const auto& arm = m.arms[i];
          if (!arm.pat || !isMatchWildcardPattern(*arm.pat))
            continue;
          PatTestResult tr = emitPatternTest(patCtx(), *arm.pat, scrut, sty);
          Ctx body_ctx = fork();
          body_ctx.env_ = pre;
          for (const auto& b : tr.binds)
            body_ctx.storeVar(b.name, b.value, b.type);
          for (const auto& s : arm.body)
            body_ctx.emitStmt(*s);
          mergeChild(body_ctx, &pre);
          if (body_ctx.terminated && !body_ctx.switch_break_arm_)
            terminated = true;
          if (!body_ctx.terminated) {
            gen_->out_ << "  br label %" << done_label << "\n";
            gen_->out_ << done_label << ":\n";
          } else if (body_ctx.switch_break_arm_) {
            gen_->out_ << done_label << ":\n";
          }
          return;
        }
        gen_->out_ << "  call void @far_panic(i64 0)\n";
        gen_->out_ << "  unreachable\n";
        gen_->out_ << done_label << ":\n";
        return;
      }

      auto arm_order = matchArmOrder(m);
      emitMatchFromArm(arm_order, 0, m, scrut, sty, pre, done_label, fail_label);
      gen_->out_ << done_label << ":\n";
    }

    static bool rangeForConstEmpty(int64_t start, int64_t end, bool exclusive) {
      return exclusive ? (start >= end) : (start > end);
    }

    bool exprConstNonNullish(const Expr& e) const {
      if (e.kind == Expr::Int)
        return e.int_lit.value != 0;
      if (e.kind == Expr::Float)
        return e.float_lit.value != 0.0 && !std::isnan(e.float_lit.value);
      if (e.kind == Expr::String)
        return !e.string_lit.value.empty();
      if (e.kind == Expr::Variable) {
        auto fit = const_float_locals_.find(e.var.name);
        if (fit != const_float_locals_.end())
          return fit->second != 0.0 && !std::isnan(fit->second);
        auto it = const_int_locals_.find(e.var.name);
        return it != const_int_locals_.end() && it->second != 0;
      }
      return false;
    }

    bool foreachConstEmpty(const Expr& iter) {
      if (iter.kind == Expr::ArrayLitExpr)
        return iter.array_lit.elements.empty();
      if (iter.kind == Expr::String)
        return iter.string_lit.value.empty();
      if (iter.kind == Expr::Variable) {
        if (constexpr_empty_locals_.count(iter.var.name))
          return true;
        for (const auto& stmt : gen_->program_.comptime_stmts) {
          if (stmt->kind != Stmt::LetStmt || !stmt->let.is_constexpr || stmt->let.name != iter.var.name ||
              !stmt->let.value)
            continue;
          if (stmt->let.value->kind == Expr::String)
            return stmt->let.value->string_lit.value.empty();
          if (stmt->let.value->kind == Expr::ArrayLitExpr)
            return stmt->let.value->array_lit.elements.empty();
        }
      }
      return false;
    }

    bool emitIfFromClause(size_t start, const If& ifs, const std::unordered_map<std::string, VarInfo>& pre,
                          const std::string& end_label) {
      // Without else, the false branch always joins at end_label.
      bool needs_join = ifs.else_body.empty();
      std::string else_label = ifs.else_body.empty() ? "" : gen_->fresh("if.else");
      std::string next_label;
      for (size_t i = start; i < ifs.clauses.size(); ++i) {
        const auto& clause = ifs.clauses[i];
        std::string cond = emitBranchCond(*clause.condition);
        std::string body_label = gen_->fresh("if.body");
        if (i + 1 < ifs.clauses.size())
          next_label = gen_->fresh("if.next");
        else if (!ifs.else_body.empty())
          next_label = else_label;
        else
          next_label = end_label;
        gen_->out_ << "  br i1 " << cond << ", label %" << body_label << ", label %" << next_label << "\n";
        gen_->out_ << body_label << ":\n";
        Ctx body_ctx = fork();
        body_ctx.env_ = pre;
        for (const auto& s : clause.body)
          body_ctx.emitStmt(*s);
        mergeChild(body_ctx, &pre);
        if (!body_ctx.terminated)
          needs_join = true;
        if (!body_ctx.terminated)
          gen_->out_ << "  br label %" << end_label << "\n";
        if (next_label != end_label && next_label != else_label)
          gen_->out_ << next_label << ":\n";
      }
      if (!ifs.else_body.empty()) {
        gen_->out_ << else_label << ":\n";
        Ctx else_ctx = fork();
        else_ctx.env_ = pre;
        for (const auto& s : ifs.else_body)
          else_ctx.emitStmt(*s);
        mergeChild(else_ctx, &pre);
        if (!else_ctx.terminated)
          needs_join = true;
        if (!else_ctx.terminated)
          gen_->out_ << "  br label %" << end_label << "\n";
      }
      return needs_join;
    }

    void emitIf(const If& ifs) {

      if (ifs.clauses.empty()) return;

      auto pre = env_;

      std::string end_label = gen_->fresh("endif");

      for (size_t i = 0; i < ifs.clauses.size(); ++i) {
        ConstBool cb = constBoolFromExpr(*ifs.clauses[i].condition);
        if (cb == ConstBool::False)
          continue;
        if (cb == ConstBool::True) {
          Ctx body_ctx = fork();
          body_ctx.env_ = pre;
          for (const auto& s : ifs.clauses[i].body)
            body_ctx.emitStmt(*s);
          mergeChild(body_ctx, &pre);
          if (body_ctx.terminated)
            terminated = true;
          if (!body_ctx.terminated) {
            gen_->out_ << "  br label %" << end_label << "\n";
            gen_->out_ << end_label << ":\n";
          }
          return;
        }
        bool needs_join = emitIfFromClause(i, ifs, pre, end_label);
        if (needs_join)
          gen_->out_ << end_label << ":\n";
        return;
      }

      if (!ifs.else_body.empty()) {
        Ctx else_ctx = fork();
        else_ctx.env_ = pre;
        for (const auto& s : ifs.else_body)
          else_ctx.emitStmt(*s);
        mergeChild(else_ctx, &pre);
        if (!else_ctx.terminated) {
          gen_->out_ << "  br label %" << end_label << "\n";
          gen_->out_ << end_label << ":\n";
        }
        return;
      }

    }



    std::string emitLoopExitTrampoline(const std::string& end_label, const std::string& exit_label) {
      gen_->out_ << end_label << ":\n";
      gen_->out_ << "  br label %" << exit_label << "\n";
      gen_->out_ << exit_label << ":\n";
      return exit_label;
    }

    void emitWhile(const While& wh) {

      if (constBoolFromExpr(*wh.condition) == ConstBool::False)
        return;

      std::string cond_label = gen_->fresh("while.cond");

      std::string body_label = gen_->fresh("while.body");

      std::string end_label = gen_->fresh("while.end");
      std::string exit_label = gen_->fresh("loop.out");

      loop_stack_.push_back({end_label, cond_label, exit_label});



      gen_->out_ << "  br label %" << cond_label << "\n";

      gen_->out_ << cond_label << ":\n";

      std::string cond = emitBranchCond(*wh.condition);

      gen_->out_ << "  br i1 " << cond << ", label %" << body_label << ", label %" << end_label << "\n";



      gen_->out_ << body_label << ":\n";

      size_t iter_defers = defer_stack_.size();
      Ctx body_ctx = fork();

      for (const auto& s : wh.body)

        body_ctx.emitStmt(*s);

      mergeChild(body_ctx);

      if (!body_ctx.terminated) {
        flushDefersFrom(iter_defers);
        gen_->out_ << "  br label %" << cond_label << "\n";
      }



      std::string exit = emitLoopExitTrampoline(end_label, loop_stack_.back().exit_label);
      (void)exit;
      loop_stack_.pop_back();

    }



    static bool containsThrowStmt(const Stmt& stmt) {
      switch (stmt.kind) {
        case Stmt::ThrowStmtK:
          return true;
        case Stmt::IfStmt:
          for (const auto& c : stmt.if_stmt.clauses)
            for (const auto& s : c.body)
              if (containsThrowStmt(*s))
                return true;
          for (const auto& s : stmt.if_stmt.else_body)
            if (containsThrowStmt(*s))
              return true;
          return false;
        case Stmt::WhileStmt:
          for (const auto& s : stmt.while_stmt.body)
            if (containsThrowStmt(*s))
              return true;
          return false;
        case Stmt::ForStmt:
          for (const auto& s : stmt.for_stmt.body)
            if (containsThrowStmt(*s))
              return true;
          return false;
        case Stmt::TryStmtK:
          for (const auto& s : stmt.try_stmt.try_body)
            if (containsThrowStmt(*s))
              return true;
          for (const auto& s : stmt.try_stmt.catch_body)
            if (containsThrowStmt(*s))
              return true;
          for (const auto& s : stmt.try_stmt.finally_body)
            if (containsThrowStmt(*s))
              return true;
          return false;
        case Stmt::MatchStmtK:
          for (const auto& arm : stmt.match_stmt.arms)
            for (const auto& s : arm.body)
              if (containsThrowStmt(*s))
                return true;
          return false;
        case Stmt::UnsafeStmtK:
          for (const auto& s : stmt.unsafe.body)
            if (containsThrowStmt(*s))
              return true;
          return false;
        default:
          return false;
      }
    }

    bool stmtAlwaysReturns(const Stmt& stmt) const {
      switch (stmt.kind) {
        case Stmt::ReturnStmt:
          return true;
        case Stmt::IfStmt: {
          bool chain_matched = false;
          for (const auto& c : stmt.if_stmt.clauses) {
            if (chain_matched)
              break;
            ConstBool cb = constBoolFromExpr(*c.condition);
            if (cb == ConstBool::False)
              continue;
            if (!stmtBlockAlwaysReturns(c.body))
              return false;
            if (cb == ConstBool::True) {
              chain_matched = true;
              return true;
            }
          }
          if (chain_matched)
            return true;
          if (stmt.if_stmt.else_body.empty())
            return false;
          return stmtBlockAlwaysReturns(stmt.if_stmt.else_body);
        }
        case Stmt::MatchStmtK: {
          std::optional<int64_t> const_scrut = constIntFromExpr(*stmt.match_stmt.scrutinee);
          if (!const_scrut)
            return false;
          for (const auto& arm : stmt.match_stmt.arms) {
            if (arm.pat && isMatchWildcardPattern(*arm.pat))
              continue;
            if (arm.pat && patternCertainlyFailsConstInt(*arm.pat, *const_scrut))
              continue;
            if (arm.pat && patternMatchesConstInt(*arm.pat, *const_scrut))
              return stmtBlockAlwaysReturns(arm.body);
          }
          for (const auto& arm : stmt.match_stmt.arms) {
            if (arm.pat && isMatchWildcardPattern(*arm.pat))
              return stmtBlockAlwaysReturns(arm.body);
          }
          return false;
        }
        default:
          return false;
      }
    }

    bool stmtBlockAlwaysReturns(const std::vector<std::unique_ptr<Stmt>>& stmts) const {
      for (const auto& s : stmts) {
        if (stmtAlwaysReturns(*s))
          return true;
      }
      return false;
    }

    bool stmtAbortsRestOfBlock(const Stmt& stmt) const {
      if (stmt.kind == Stmt::ReturnStmt || stmt.kind == Stmt::ThrowStmtK)
        return true;
      if (stmt.kind == Stmt::IfStmt) {
        bool chain_matched = false;
        for (const auto& c : stmt.if_stmt.clauses) {
          if (chain_matched)
            break;
          ConstBool cb = constBoolFromExpr(*c.condition);
          if (cb == ConstBool::False)
            continue;
          if (cb == ConstBool::True) {
            chain_matched = true;
            return stmtBlockAlwaysReturns(c.body);
          }
        }
        if (chain_matched)
          return true;
        if (stmt.if_stmt.else_body.empty())
          return false;
        return stmtBlockAlwaysReturns(stmt.if_stmt.else_body);
      }
      if (stmt.kind == Stmt::TryStmtK)
        return tryCatchUnreachable(stmt.try_stmt.try_body);
      if (stmt.kind == Stmt::MatchStmtK) {
        std::optional<double> const_scrut_f = constFloatFromExpr(*stmt.match_stmt.scrutinee);
        if (const_scrut_f) {
          for (const auto& arm : stmt.match_stmt.arms) {
            if (arm.pat && isMatchWildcardPattern(*arm.pat))
              continue;
            if (arm.pat && patternCertainlyFailsConstFloat(*arm.pat, *const_scrut_f))
              continue;
            if (arm.pat && patternMatchesConstFloat(*arm.pat, *const_scrut_f))
              return stmtBlockAlwaysReturns(arm.body);
          }
          for (const auto& arm : stmt.match_stmt.arms) {
            if (arm.pat && isMatchWildcardPattern(*arm.pat))
              return stmtBlockAlwaysReturns(arm.body);
          }
          return false;
        }
        std::optional<int64_t> const_scrut = constIntFromExpr(*stmt.match_stmt.scrutinee);
        if (!const_scrut)
          return false;
        for (const auto& arm : stmt.match_stmt.arms) {
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            continue;
          if (arm.pat && patternCertainlyFailsConstInt(*arm.pat, *const_scrut))
            continue;
          if (arm.pat && patternMatchesConstInt(*arm.pat, *const_scrut))
            return stmtBlockAlwaysReturns(arm.body);
        }
        for (const auto& arm : stmt.match_stmt.arms) {
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            return stmtBlockAlwaysReturns(arm.body);
        }
      }
      return false;
    }

    bool tryCatchUnreachable(const std::vector<std::unique_ptr<Stmt>>& try_body) const {
      for (const auto& s : try_body) {
        if (containsThrowStmt(*s))
          return false;
      }
      return stmtBlockAlwaysReturns(try_body);
    }

    void emitTry(const TryStmt& tr) {
      bool emit_catch = tr.has_catch && !tryCatchUnreachable(tr.try_body);

      TryLabels labels;
      labels.resume_label = gen_->fresh("try.resume");
      labels.body_label = gen_->fresh("try.body");
      labels.catch_label = gen_->fresh("try.catch");
      labels.finally_label = gen_->fresh("try.finally");
      labels.after_label = gen_->fresh("try.after");
      labels.catch_var = tr.catch_var;
      labels.catch_type = tr.catch_type;
      labels.catch_type_tag = tr.catch_type_tag;
      labels.catch_type_explicit = tr.catch_type_explicit;
      labels.has_catch = emit_catch;
      labels.has_finally = tr.has_finally;
      labels.stack_index = try_stack_.size();
      if (emitting_finally_for_ >= 0)
        labels.contained_in_finally_of = emitting_finally_for_;
      if (emitting_finally_for_ >= 0)
        labels.continue_label = gen_->fresh("try.cont");
      if (!tr.try_body.empty()) {
        Stmt::Kind last = tr.try_body.back()->kind;
        labels.break_finishes_try = last == Stmt::ForStmt || last == Stmt::WhileStmt;
        for (size_t i = 0; i + 1 < tr.try_body.size(); ++i) {
          Stmt::Kind k = tr.try_body[i]->kind;
          if (k == Stmt::ForStmt || k == Stmt::WhileStmt)
            labels.has_code_after_loop = true;
        }
      }
      if (emit_catch)
        ensureSlot(tr.catch_var, tr.catch_type);

      std::string unwind_slot = gen_->fresh("tryunw");
      labels.unwind_slot = unwind_slot;
      gen_->hoistedAllocaRaw(unwind_slot, "i1");
      gen_->out_ << "  store i1 false, i1* %" << unwind_slot << "\n";

      try_stack_.push_back(labels);

      emitTryEnter(errCtx(), labels.body_label);

      gen_->out_ << labels.body_label << ":\n";
      if (gen_->pending_ret_init_)
        gen_->out_ << "  store i1 false, i1* %" << gen_->pending_ret_flag_ << "\n";
      if (gen_->pending_throw_init_)
        gen_->out_ << "  store i1 false, i1* %" << gen_->pending_throw_flag_ << "\n";
      if (gen_->pending_jump_init_)
        gen_->out_ << "  store i8 0, i8* %" << gen_->pending_jump_kind_ << "\n";
      if (gen_->pending_yield_init_)
        gen_->out_ << "  store i1 false, i1* %" << gen_->pending_yield_flag_ << "\n";
      bool try_body_terminated = false;
      size_t try_defer_base = defer_stack_.size();
      {
        Ctx body_ctx = fork();
        // Nested try inside catch/finally must not inherit outer catch/finally throw routing.
        body_ctx.emitting_catch_for_ = -1;
        body_ctx.emitting_finally_for_ = -1;
        for (const auto& s : tr.try_body) {
          body_ctx.emitStmt(*s);
          if (body_ctx.exits_loop_not_scope_) {
            body_ctx.exits_loop_not_scope_ = false;
            body_ctx.terminated = false;
          }
        }
        try_body_terminated = body_ctx.terminated;
        mergeChild(body_ctx);
        if (try_body_terminated && !emit_catch && !tr.has_finally)
          terminated = true;
        if (!body_ctx.terminated) {
          flushDefersFrom(try_defer_base);
          gen_->out_ << "  br label %"
                     << (tr.has_finally ? labels.finally_label : labels.after_label) << "\n";
        }
      }

      gen_->out_ << labels.resume_label << ":\n";
      gen_->out_ << "  call void @far_clear_pending_throw()\n";
      gen_->out_ << "  store i1 true, i1* %" << unwind_slot << "\n";
      if (gen_->pending_throw_init_)
        gen_->out_ << "  store i1 false, i1* %" << gen_->pending_throw_flag_ << "\n";
      if (emit_catch) {
        std::string caught = emitCaughtValueGlobal(errCtx());
        if (tr.catch_type_explicit && tr.catch_type_tag != 0) {
          std::string ok = gen_->fresh("tagok");
          std::string mismatch = gen_->fresh("tagbad");
          std::string cmp = emitCaughtMatches(errCtx(), tr.catch_type_tag);
          gen_->out_ << "  br i1 " << cmp << ", label %" << ok << ", label %" << mismatch << "\n";
          gen_->out_ << mismatch << ":\n";
          if (tr.has_finally) {
            std::string tag = emitCaughtTagGlobal(errCtx());
            std::string val = emitCaughtValueGlobal(errCtx());
            setPendingThrowValues(tag, val);
            gen_->out_ << "  br label %" << labels.finally_label << "\n";
          } else {
            emitRethrowCaught();
          }
          gen_->out_ << ok << ":\n";
        }
        storeVar(tr.catch_var, caught, tr.catch_type);
        Ctx catch_ctx = fork();
        catch_ctx.terminated = false;
        catch_ctx.emitting_catch_for_ = static_cast<int>(labels.stack_index);
        for (const auto& s : tr.catch_body)
          catch_ctx.emitStmt(*s);
        mergeChild(catch_ctx);
        if (!catch_ctx.terminated) {
          gen_->out_ << "  br label %"
                     << (tr.has_finally ? labels.finally_label : labels.after_label) << "\n";
        }
      } else if (tr.has_finally) {
        gen_->out_ << "  br label %" << labels.finally_label << "\n";
      } else {
        emitUncaughtRethrow();
      }

      if (tr.has_finally) {
        gen_->out_ << labels.finally_label << ":\n";
        Ctx fin_ctx = fork();
        fin_ctx.emitting_finally_for_ = static_cast<int>(labels.stack_index);
        for (const auto& s : tr.finally_body)
          fin_ctx.emitStmt(*s);
        mergeChild(fin_ctx);
        bool pending_ctrl = gen_->pending_ret_init_ || gen_->pending_throw_init_ ||
                            gen_->pending_jump_init_ || gen_->pending_yield_init_;
        if (emit_catch) {
          if (!fin_ctx.terminated || pending_ctrl)
            emitFinallyEpilogue(labels);
        } else if (pending_ctrl) {
          emitFinallyEpilogue(labels);
        } else if (!fin_ctx.terminated) {
          std::string unw = gen_->fresh("tryunl");
          gen_->out_ << "  %" << unw << " = load i1, i1* %" << unwind_slot << "\n";
          std::string rethrow_path = gen_->fresh("tryrethrow");
          std::string done = labels.after_label;
          gen_->out_ << "  br i1 %" << unw << ", label %" << rethrow_path << ", label %" << done
                     << "\n";
          gen_->out_ << rethrow_path << ":\n";
          emitUncaughtRethrow();
        }
      }

      gen_->out_ << labels.after_label << ":\n";
      emitTrySuccess(errCtx());
      if (try_body_terminated && !emit_catch && !tr.has_finally) {
        gen_->out_ << "  unreachable\n";
      } else if (terminated && !tr.has_finally && !emit_catch) {
        gen_->out_ << "  unreachable\n";
      } else if (labels.contained_in_finally_of >= 0) {
        gen_->out_ << "  br label %" << labels.continue_label << "\n";
      } else if (try_stack_.size() > 1 && !tr.has_finally) {
        const TryLabels& outer = try_stack_[try_stack_.size() - 2];
        std::string next = outer.has_finally ? outer.finally_label : outer.after_label;
        gen_->out_ << "  br label %" << next << "\n";
      } else if (tr.has_finally && !emit_catch && labels.contained_in_finally_of < 0 &&
                 try_stack_.size() <= 1 && try_body_terminated) {
        gen_->out_ << "  unreachable\n";
      }
      if (tr.has_finally || emit_catch)
        terminated = false;
      if (labels.contained_in_finally_of >= 0)
        gen_->out_ << labels.continue_label << ":\n";
      try_stack_.pop_back();
    }

    PatCodegenCtx patCtx() {
      return PatCodegenCtx{
          gen_->out_, [&](const std::string& hint) { return gen_->fresh(hint); },
          [&](const Expr& e) { return emitExpr(e); }, &gen_->obj_reg_};
    }

    void emitThrowStmt(const ThrowStmt& th) {
      int64_t tag = 0;
      TypeDesc val_ty = th.value->type;
      if (isUserDesc(val_ty)) {
        const UserTypeDef* ut = gen_->obj_reg_.lookup(val_ty.user_name);
        if (ut && ut->kind == UserTypeKind::Exception)
          tag = ut->type_tag;
      }
      if (emitting_finally_for_ >= 0) {
        flushScopeDefers();
        std::string val = emitExpr(*th.value);
        setPendingThrow(tag, val);
        terminated = true;
        return;
      }
      if (!try_stack_.empty()) {
        flushScopeDefers();
        std::string val = emitExpr(*th.value);
        setPendingThrow(tag, val);
        if (emitting_catch_for_ >= 0) {
          emitTrySuccess(errCtx());
          emitRoutePendingThrowOutward(true);
        } else if (try_stack_.back().has_catch) {
          emitDispatchPendingThrowToCatch(try_stack_.back());
        } else {
          std::string finally = innermostFinallyLabel();
          if (!finally.empty()) {
            completeTryForControlToFinally(finally);
            gen_->out_ << "  br label %" << finally << "\n";
          } else {
            emitRoutePendingThrowOutward(false);
          }
        }
        terminated = true;
        return;
      }
      flushScopeDefers();
      std::string val = emitExpr(*th.value);
      emitThrow(errCtx(), tag, val);
      emitReturnAfterPendingThrow();
      gen_->out_ << "  unreachable\n";
      terminated = true;
    }

    void emitForEach(const For& fo) {
      if (foreachConstEmpty(*fo.foreach_iter))
        return;

      TypeDesc coll_ty = exprType(*fo.foreach_iter);
      TypeDesc elem_ty = elemTypeOf(coll_ty);
      std::string coll = emitExpr(*fo.foreach_iter);
      std::string len = emitCollectionLen(collCtx(), coll_ty, coll);

      std::string idx_slot = gen_->fresh("fidx");
      gen_->out_ << "  %" << idx_slot << " = alloca " << I64 << "\n";
      gen_->out_ << "  store " << I64 << " 0, " << I64 << "* %" << idx_slot << "\n";

      std::string cond_label = gen_->fresh("for.cond");
      std::string body_label = gen_->fresh("for.body");
      std::string step_label = gen_->fresh("for.step");
      std::string end_label = gen_->fresh("for.end");
      std::string exit_label = gen_->fresh("loop.out");

      loop_stack_.push_back({end_label, step_label, exit_label});

      gen_->out_ << "  br label %" << cond_label << "\n";

      gen_->out_ << cond_label << ":\n";
      std::string idx = gen_->fresh("fi");
      gen_->out_ << "  %" << idx << " = load " << I64 << ", " << I64 << "* %" << idx_slot << "\n";
      std::string cmp = gen_->fresh("fcmp");
      gen_->out_ << "  %" << cmp << " = icmp slt " << I64 << " %" << idx << ", " << len << "\n";
      gen_->out_ << "  br i1 %" << cmp << ", label %" << body_label << ", label %" << end_label << "\n";

      gen_->out_ << body_label << ":\n";
      std::string idx_body = gen_->fresh("fi");
      gen_->out_ << "  %" << idx_body << " = load " << I64 << ", " << I64 << "* %" << idx_slot << "\n";
      std::string elem = emitCollectionIndex(collCtx(), coll_ty, coll, "%" + idx_body);
      storeVar(fo.foreach_var, elem, elem_ty);

      size_t iter_defers = defer_stack_.size();
      Ctx body_ctx = fork();
      for (const auto& s : fo.body)
        body_ctx.emitStmt(*s);
      mergeChild(body_ctx);
      if (!body_ctx.terminated) {
        flushDefersFrom(iter_defers);
        gen_->out_ << "  br label %" << step_label << "\n";
      }

      gen_->out_ << step_label << ":\n";
      std::string idx_step = gen_->fresh("fi");
      gen_->out_ << "  %" << idx_step << " = load " << I64 << ", " << I64 << "* %" << idx_slot << "\n";
      std::string idx_next = gen_->fresh("fin");
      gen_->out_ << "  %" << idx_next << " = add " << I64 << " %" << idx_step << ", 1\n";
      gen_->out_ << "  store " << I64 << " %" << idx_next << ", " << I64 << "* %" << idx_slot << "\n";
      if (!terminated)
        gen_->out_ << "  br label %" << cond_label << "\n";

      std::string exit = emitLoopExitTrampoline(end_label, loop_stack_.back().exit_label);
      (void)exit;
      loop_stack_.pop_back();
    }

    void emitRangeFor(const For& fo) {
      std::optional<int64_t> cs = constIntFromExpr(*fo.range_start);
      std::optional<int64_t> ce = constIntFromExpr(*fo.range_end);
      if (cs && ce && rangeForConstEmpty(*cs, *ce, fo.range_exclusive))
        return;

      std::string start = emitExpr(*fo.range_start);
      std::string end = emitExpr(*fo.range_end);
      storeVar(fo.range_var, start, defaultIntType());

      std::string asc_label = gen_->fresh("rng.asc");
      std::string desc_label = gen_->fresh("rng.desc");
      std::string asc_cond = gen_->fresh("rng.acond");
      std::string asc_body = gen_->fresh("rng.abody");
      std::string asc_step = gen_->fresh("rng.astep");
      std::string desc_cond = gen_->fresh("rng.dcond");
      std::string desc_body = gen_->fresh("rng.dbody");
      std::string desc_step = gen_->fresh("rng.dstep");
      std::string end_label = gen_->fresh("rng.end");
      std::string exit_label = gen_->fresh("loop.out");
      std::string asc_flag = gen_->fresh("rng.ascf");
      std::string cont_dispatch = gen_->fresh("rng.ccont");
      gen_->hoisted_allocas_ << "  %" << asc_flag << " = alloca i1\n";

      std::string cmp = gen_->fresh("rngcmp");
      gen_->out_ << "  %" << cmp << " = icmp sle " << I64 << " " << start << ", " << end << "\n";
      gen_->out_ << "  br i1 %" << cmp << ", label %" << asc_label << ", label %" << desc_label << "\n";

      loop_stack_.push_back({end_label, cont_dispatch, exit_label});
      gen_->out_ << asc_label << ":\n";
      gen_->out_ << "  store i1 true, i1* %" << asc_flag << "\n";
      gen_->out_ << "  br label %" << asc_cond << "\n";
      gen_->out_ << asc_cond << ":\n";
      std::string iv = loadVar(fo.range_var);
      std::string acmp = gen_->fresh("rngac");
      if (fo.range_exclusive)
        gen_->out_ << "  %" << acmp << " = icmp slt " << I64 << " " << iv << ", " << end << "\n";
      else
        gen_->out_ << "  %" << acmp << " = icmp sle " << I64 << " " << iv << ", " << end << "\n";
      gen_->out_ << "  br i1 %" << acmp << ", label %" << asc_body << ", label %" << end_label << "\n";
      gen_->out_ << asc_body << ":\n";
      {
        size_t iter_defers = defer_stack_.size();
        Ctx body_ctx = fork();
        for (const auto& s : fo.body)
          body_ctx.emitStmt(*s);
        mergeChild(body_ctx);
        if (!body_ctx.terminated) {
          flushDefersFrom(iter_defers);
          gen_->out_ << "  br label %" << asc_step << "\n";
        }
      }
      gen_->out_ << asc_step << ":\n";
      std::string cur = loadVar(fo.range_var);
      std::string nxt = gen_->fresh("rngan");
      gen_->out_ << "  %" << nxt << " = add " << I64 << " " << cur << ", 1\n";
      storeVar(fo.range_var, "%" + nxt, defaultIntType());
      if (!terminated)
        gen_->out_ << "  br label %" << asc_cond << "\n";

      gen_->out_ << desc_label << ":\n";
      gen_->out_ << "  store i1 false, i1* %" << asc_flag << "\n";
      gen_->out_ << "  br label %" << desc_cond << "\n";
      gen_->out_ << desc_cond << ":\n";
      std::string dv = loadVar(fo.range_var);
      std::string dcmp = gen_->fresh("rngdc");
      if (fo.range_exclusive)
        gen_->out_ << "  %" << dcmp << " = icmp sgt " << I64 << " " << dv << ", " << end << "\n";
      else
        gen_->out_ << "  %" << dcmp << " = icmp sge " << I64 << " " << dv << ", " << end << "\n";
      gen_->out_ << "  br i1 %" << dcmp << ", label %" << desc_body << ", label %" << end_label << "\n";
      gen_->out_ << desc_body << ":\n";
      {
        size_t iter_defers = defer_stack_.size();
        Ctx body_ctx = fork();
        for (const auto& s : fo.body)
          body_ctx.emitStmt(*s);
        mergeChild(body_ctx);
        if (!body_ctx.terminated) {
          flushDefersFrom(iter_defers);
          gen_->out_ << "  br label %" << desc_step << "\n";
        }
      }
      gen_->out_ << desc_step << ":\n";
      std::string dcur = loadVar(fo.range_var);
      std::string dnxt = gen_->fresh("rngdn");
      gen_->out_ << "  %" << dnxt << " = sub " << I64 << " " << dcur << ", 1\n";
      storeVar(fo.range_var, "%" + dnxt, defaultIntType());
      if (!terminated)
        gen_->out_ << "  br label %" << desc_cond << "\n";

      gen_->out_ << cont_dispatch << ":\n";
      std::string asc_fl = gen_->fresh("rngcfl");
      gen_->out_ << "  %" << asc_fl << " = load i1, i1* %" << asc_flag << "\n";
      gen_->out_ << "  br i1 %" << asc_fl << ", label %" << asc_step << ", label %" << desc_step << "\n";

      std::string exit = emitLoopExitTrampoline(end_label, loop_stack_.back().exit_label);
      (void)exit;
      loop_stack_.pop_back();
    }

    void emitFor(const For& fo) {

      if (fo.is_parallel) {
        std::optional<int64_t> cs = constIntFromExpr(*fo.range_start);
        std::optional<int64_t> ce = constIntFromExpr(*fo.range_end);
        if (cs && ce && rangeForConstEmpty(*cs, *ce, fo.range_exclusive))
          return;

        std::string start = emitExpr(*fo.range_start);
        std::string end = emitExpr(*fo.range_end);
        if (!fo.range_exclusive) {
          std::string adj = gen_->fresh("pend");
          gen_->out_ << "  %" << adj << " = add " << I64 << " " << end << ", 1\n";
          end = "%" + adj;
        }
        std::string closure;
        if (!fo.parallel_captures.empty()) {
          auto it = gen_->fn_by_llvm_.find(fo.parallel_fn);
          if (it == gen_->fn_by_llvm_.end() || !it->second)
            throw FarError("internal: missing parallel-for worker");
          closure = emitClosureNew(it->second, fo.parallel_captures);
        }
        emitParallelFor(concCtx(), fo.parallel_fn, start, end, closure);
        return;
      }

      if (fo.is_range) {
        emitRangeFor(fo);
        return;
      }

      if (fo.is_foreach) {
        emitForEach(fo);
        return;
      }

      if (fo.init) emitStmt(*fo.init);

      std::string cond_label = gen_->fresh("for.cond");

      std::string body_label = gen_->fresh("for.body");

      std::string step_label = gen_->fresh("for.step");

      std::string end_label = gen_->fresh("for.end");
      std::string exit_label = gen_->fresh("loop.out");

      if (fo.cond && constBoolFromExpr(*fo.cond) == ConstBool::False) {
        gen_->out_ << "  br label %" << end_label << "\n";
        gen_->out_ << end_label << ":\n";
        return;
      }

      loop_stack_.push_back({end_label, step_label, exit_label});



      gen_->out_ << "  br label %" << cond_label << "\n";

      gen_->out_ << cond_label << ":\n";

      if (fo.cond) {

        std::string cond = emitBranchCond(*fo.cond);

        gen_->out_ << "  br i1 " << cond << ", label %" << body_label << ", label %" << end_label << "\n";

      } else {

        gen_->out_ << "  br label %" << body_label << "\n";

      }



      gen_->out_ << body_label << ":\n";

      size_t iter_defers = defer_stack_.size();
      Ctx body_ctx = fork();

      for (const auto& s : fo.body)

        body_ctx.emitStmt(*s);

      mergeChild(body_ctx);

      if (!body_ctx.terminated) {
        flushDefersFrom(iter_defers);
        gen_->out_ << "  br label %" << step_label << "\n";
      }

      gen_->out_ << step_label << ":\n";

      bool step_terminated = false;
      if (fo.step) {

        Ctx step_ctx = fork();

        step_ctx.emitStmt(*fo.step);

        mergeChild(step_ctx);
        step_terminated = step_ctx.terminated;

      }

      if (!step_terminated)

        gen_->out_ << "  br label %" << cond_label << "\n";



      std::string exit = emitLoopExitTrampoline(end_label, loop_stack_.back().exit_label);
      (void)exit;
      loop_stack_.pop_back();

    }



    void emitBreak() {

      if (!switch_break_stack_.empty()) {
        flushScopeDefers();
        gen_->out_ << "  br label %" << switch_break_stack_.back() << "\n";
        terminated = true;
        switch_break_arm_ = true;
        return;
      }

      if (loop_stack_.empty())

        throw FarError("break outside loop");

      flushScopeDefers();

      std::string finally = innermostFinallyLabel();
      if (!finally.empty()) {
        int ti = findTryForFinallyLabel(finally);
        if (ti >= 0 && !try_stack_[static_cast<size_t>(ti)].has_code_after_loop) {
          completeTryForControlToFinally(finally);
          setPendingBreak(loop_stack_.back().exit_label);
          gen_->out_ << "  br label %" << finally << "\n";
          terminated = true;
          exits_loop_not_scope_ = true;
          return;
        }
      }

      gen_->out_ << "  br label %" << loop_stack_.back().exit_label << "\n";

      terminated = true;
      exits_loop_not_scope_ = true;

    }



    void emitContinue() {

      if (loop_stack_.empty())

        throw FarError("continue outside loop");

      flushScopeDefers();

      std::string finally = innermostFinallyLabel();
      if (!finally.empty()) {
        int ti = findTryForFinallyLabel(finally);
        if (ti >= 0 && !try_stack_[static_cast<size_t>(ti)].has_code_after_loop) {
          completeTryForControlToFinally(finally);
          setPendingContinue(loop_stack_.back().continue_label);
          gen_->out_ << "  br label %" << finally << "\n";
          terminated = true;
          exits_loop_not_scope_ = true;
          return;
        }
      }

      gen_->out_ << "  br label %" << loop_stack_.back().continue_label << "\n";

      terminated = true;

    }



    std::string emitStringLit(const std::string& s) {

      int id = gen_->str_id_++;

      std::string escaped;

      for (char c : s) {

        if (c == '\\') escaped += "\\5C";

        else if (c == '"') escaped += "\\22";

        else if (c == '\r') escaped += "\\0D";

        else if (c == '\n') escaped += "\\0A";

        else escaped += c;

      }

      size_t len = s.size() + 1;

      gen_->globals_ << "@.str." << id << " = private unnamed_addr constant [" << len << " x i8] c\""

                     << escaped << "\\00\"\n";

      std::string gep = gen_->fresh("strgep");

      gen_->out_ << "  %" << gep << " = getelementptr inbounds [" << len << " x i8], [" << len

                 << " x i8]* @.str." << id << ", i64 0, i64 0\n";

      std::string as_i64 = gen_->fresh();

      gen_->out_ << "  %" << as_i64 << " = ptrtoint i8* %" << gep << " to " << I64 << "\n";

      return "%" + as_i64;

    }

    struct AssignTargetLoc {
      enum class Kind { Variable, Index, Member, Deref } kind;
      TypeDesc ty;
      std::string var_name;
      TypeDesc arr_ty;
      std::string arr;
      std::string idx;
      TypeDesc obj_ty;
      std::string obj;
      Expr* target_expr = nullptr;
      TypeDesc ptr_ty;
      std::string ptr;
    };

    AssignTargetLoc resolveAssignTarget(Expr& target) {
      AssignTargetLoc loc;
      loc.ty = exprType(target);
      if (target.kind == Expr::Variable) {
        loc.kind = AssignTargetLoc::Kind::Variable;
        loc.var_name = target.var.name;
        return loc;
      }
      if (target.kind == Expr::IndexExpr) {
        loc.kind = AssignTargetLoc::Kind::Index;
        loc.arr_ty = exprType(*target.index.array);
        loc.arr = emitExpr(*target.index.array);
        loc.idx = emitExpr(*target.index.index);
        return loc;
      }
      if (target.kind == Expr::MemberExpr) {
        loc.kind = AssignTargetLoc::Kind::Member;
        loc.obj_ty = exprType(*target.member.object);
        loc.obj = emitExpr(*target.member.object);
        loc.target_expr = &target;
        return loc;
      }
      if (target.kind == Expr::PrefixExprK && target.prefix.op == "*") {
        loc.kind = AssignTargetLoc::Kind::Deref;
        loc.ptr_ty = exprType(*target.prefix.operand);
        loc.ptr = emitExpr(*target.prefix.operand);
        return loc;
      }
      throw FarError("invalid assignment target");
    }

    std::string loadAssignTarget(const AssignTargetLoc& loc) {
      switch (loc.kind) {
        case AssignTargetLoc::Kind::Variable:
          return loadVar(loc.var_name);
        case AssignTargetLoc::Kind::Index: {
          if (isUserDesc(loc.arr_ty)) {
            std::string sym = userMangleMethod(userTypeKey(loc.arr_ty), "__index_get");
            std::string tmp = gen_->fresh("idx");
            gen_->out_ << "  %" << tmp << " = call i64 @" << sym << "(i64 " << loc.arr << ", i64 "
                       << loc.idx << ")\n";
            return "%" + tmp;
          }
          if (loc.arr_ty.form == TypeForm::Array || loc.arr_ty.form == TypeForm::List ||
              loc.arr_ty.form == TypeForm::Dict || loc.arr_ty.form == TypeForm::Slice ||
              isPrimTy(loc.arr_ty, FarTypeId::Arr))
            return emitCollectionIndex(collCtx(), loc.arr_ty, loc.arr, loc.idx);
          std::string tmp = gen_->fresh();
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_array_get(" << I64 << " " << loc.arr
                     << ", " << I64 << " " << loc.idx << ")\n";
          return "%" + tmp;
        }
        case AssignTargetLoc::Kind::Member: {
          if (isUserDesc(loc.obj_ty)) {
            std::string getter = "__prop_get_" + loc.target_expr->member.member;
            if (gen_->obj_reg_.lookupMethod(loc.obj_ty, getter)) {
              std::string sym = userMangleMethod(userTypeKey(loc.obj_ty), getter);
              std::string tmp = gen_->fresh("pget");
              gen_->out_ << "  %" << tmp << " = call i64 @" << sym << "(i64 " << loc.obj << ")\n";
              return "%" + tmp;
            }
            return emitUserMember(userCtx(), gen_->obj_reg_, loc.target_expr->member, loc.obj_ty, loc.obj);
          }
          return emitAggregateMember(aggCtx(), loc.target_expr->member, aggregateDescId(loc.obj_ty));
        }
        case AssignTargetLoc::Kind::Deref:
          return emitPtrDeref(memCtx(), loc.ptr_ty, loc.ptr);
      }
      throw FarError("invalid assignment target");
    }

    void storeAssignTarget(const AssignTargetLoc& loc, const std::string& value) {
      switch (loc.kind) {
        case AssignTargetLoc::Kind::Variable:
          storeVar(loc.var_name, value, loc.ty);
          return;
        case AssignTargetLoc::Kind::Index: {
          if (isPrimTy(loc.arr_ty, FarTypeId::String) || isPrimTy(loc.arr_ty, FarTypeId::RawString))
            throw FarError("cannot assign to string index: strings are immutable");
          if (isCollectionDesc(loc.arr_ty) || isPrimTy(loc.arr_ty, FarTypeId::Arr)) {
            emitCollectionStore(collCtx(), loc.arr_ty, loc.arr, loc.idx, value);
            return;
          }
          if (isUserDesc(loc.arr_ty) && gen_->obj_reg_.lookupMethod(loc.arr_ty, "__index_set")) {
            std::string sym = userMangleMethod(userTypeKey(loc.arr_ty), "__index_set");
            gen_->out_ << "  call i64 @" << sym << "(i64 " << loc.arr << ", i64 " << loc.idx << ", i64 "
                       << value << ")\n";
            return;
          }
          gen_->out_ << "  call void @far_array_set(i64 " << loc.arr << ", i64 " << loc.idx << ", i64 "
                     << value << ")\n";
          return;
        }
        case AssignTargetLoc::Kind::Member: {
          if (isUserDesc(loc.obj_ty)) {
            std::string setter = "__prop_set_" + loc.target_expr->member.member;
            if (gen_->obj_reg_.lookupMethod(loc.obj_ty, setter)) {
              std::string sym = userMangleMethod(userTypeKey(loc.obj_ty), setter);
              gen_->out_ << "  call i64 @" << sym << "(i64 " << loc.obj << ", i64 " << value << ")\n";
              return;
            }
            emitUserMemberStore(userCtx(), gen_->obj_reg_, loc.target_expr->member, loc.obj_ty, loc.obj,
                                value);
            return;
          }
          throw FarError("invalid assignment target");
        }
        case AssignTargetLoc::Kind::Deref:
          emitPtrStore(memCtx(), loc.ptr_ty, loc.ptr, value);
          return;
      }
      throw FarError("invalid assignment target");
    }

    bool isComplexAssignTarget(const Expr& target) const {
      return target.kind == Expr::IndexExpr || target.kind == Expr::MemberExpr ||
             (target.kind == Expr::PrefixExprK && target.prefix.op == "*");
    }

    std::string emitStoreTarget(Expr& target, const std::string& value, const TypeDesc& ty) {
      if (target.kind == Expr::Variable) {
        storeVar(target.var.name, value, ty);
        return value;
      }
      if (target.kind == Expr::IndexExpr) {
        std::string arr = emitExpr(*target.index.array);
        std::string idx = emitExpr(*target.index.index);
        TypeDesc arr_ty = exprType(*target.index.array);
        if (isPrimTy(arr_ty, FarTypeId::String) || isPrimTy(arr_ty, FarTypeId::RawString))
          throw FarError("cannot assign to string index: strings are immutable");
        if (isCollectionDesc(arr_ty) || isPrimTy(arr_ty, FarTypeId::Arr)) {
          emitCollectionStore(collCtx(), arr_ty, arr, idx, value);
          return value;
        }
        if (isUserDesc(arr_ty) && gen_->obj_reg_.lookupMethod(arr_ty, "__index_set")) {
          std::string sym = userMangleMethod(userTypeKey(arr_ty), "__index_set");
          gen_->out_ << "  call i64 @" << sym << "(i64 " << arr << ", i64 " << idx << ", i64 " << value
                     << ")\n";
          return value;
        }
        gen_->out_ << "  call void @far_array_set(i64 " << arr << ", i64 " << idx << ", i64 " << value
                   << ")\n";
        return value;
      }
      if (target.kind == Expr::MemberExpr) {
        TypeDesc obj_ty = exprType(*target.member.object);
        if (isUserDesc(obj_ty)) {
          std::string setter = "__prop_set_" + target.member.member;
          if (gen_->obj_reg_.lookupMethod(obj_ty, setter)) {
            std::string obj_val = emitExpr(*target.member.object);
            std::string sym = userMangleMethod(userTypeKey(obj_ty), setter);
            gen_->out_ << "  call i64 @" << sym << "(i64 " << obj_val << ", i64 " << value << ")\n";
            return value;
          }
          std::string obj_val = emitExpr(*target.member.object);
          emitUserMemberStore(userCtx(), gen_->obj_reg_, target.member, obj_ty, obj_val, value);
          return value;
        }
      }
      if (target.kind == Expr::PrefixExprK && target.prefix.op == "*") {
        TypeDesc ptr_ty = exprType(*target.prefix.operand);
        std::string ptr_val = emitExpr(*target.prefix.operand);
        emitPtrStore(memCtx(), ptr_ty, ptr_val, value);
        return value;
      }
      throw FarError("invalid assignment target");
    }

    std::string emitAssignExpr(const AssignExpr& a) {
      static const std::unordered_map<std::string, std::string> compound = {
          {"+=", "+"}, {"-=", "-"},  {"*=", "*"},    {"/=", "/"},     {"%=", "%"},
          {"**=", "**"}, {"//=", "//"}, {"&=", "&"}, {"|=", "|"},   {"^=", "^"},
          {"<<=", "<<"}, {">>=", ">>"}};
      TypeDesc target_ty = exprType(*a.target);
      const bool complex = isComplexAssignTarget(*a.target);
      std::optional<AssignTargetLoc> loc;
      if (complex)
        loc = resolveAssignTarget(*a.target);
      auto loadTarget = [&]() -> std::string {
        if (complex)
          return loadAssignTarget(*loc);
        return emitExpr(*a.target);
      };
      auto storeTarget = [&](const std::string& val) {
        if (complex)
          storeAssignTarget(*loc, val);
        else
          emitStoreTarget(*a.target, val, target_ty);
      };
      if (a.op == "\?\?=") {
        std::string cur = loadTarget();
        std::string is_n = emitIsNullish(*a.target, cur);
        std::string rhs_label = gen_->fresh("nass");
        std::string end_label = gen_->fresh("naend");
        std::string skip_label = gen_->fresh("nask");
        gen_->out_ << "  br i1 " << is_n << ", label %" << rhs_label << ", label %" << skip_label << "\n";
        gen_->out_ << rhs_label << ":\n";
        std::string val = coerceToType(*a.value, target_ty, emitExpr(*a.value));
        storeTarget(val);
        gen_->out_ << "  br label %" << end_label << "\n";
        gen_->out_ << skip_label << ":\n";
        gen_->out_ << "  br label %" << end_label << "\n";
        gen_->out_ << end_label << ":\n";
        return loadTarget();
      }
      std::string rhs;
      if (a.op == "=") {
        rhs = emitExpr(*a.value);
      } else {
        auto it = compound.find(a.op);
        if (it == compound.end())
          throw FarError("unknown assignment operator '" + a.op + "'");
        std::string cur = loadTarget();
        std::string right = emitExpr(*a.value);
        std::string tmp = gen_->fresh();
        const std::string& cop = it->second;
        bool rhs_ready = false;
        TypeDesc value_ty = a.value ? exprType(*a.value) : TypeDesc::prim(FarTypeId::I64);
        bool use_float = (isPrimitiveDesc(target_ty) && isFloatType(target_ty.primitive)) ||
                         (isPrimitiveDesc(value_ty) && isFloatType(value_ty.primitive));
        if (use_float && (cop == "+" || cop == "-" || cop == "*" || cop == "/" || cop == "%")) {
          std::string l = cur;
          std::string r = right;
          if (!(isPrimitiveDesc(target_ty) && isFloatType(target_ty.primitive)))
            l = gen_->emitIntToF64Desc(target_ty, cur);
          if (!(isPrimitiveDesc(value_ty) && isFloatType(value_ty.primitive)))
            r = gen_->emitIntToF64Desc(value_ty, right);
          static const std::unordered_map<std::string, std::string> fops = {
              {"+", "fadd"}, {"-", "fsub"}, {"*", "fmul"}};
          if (cop == "/") {
            std::string res = gen_->fresh();
            gen_->out_ << "  %" << res << " = call " << F64 << " @far_f64_div_checked(" << F64 << " " << l
                       << ", " << F64 << " " << r << ")\n";
            rhs = "%" + res;
          } else if (cop == "%") {
            std::string res = gen_->fresh();
            gen_->out_ << "  %" << res << " = call " << F64 << " @far_f64_rem_checked(" << F64 << " " << l
                       << ", " << F64 << " " << r << ")\n";
            rhs = "%" + res;
          } else {
            gen_->out_ << "  %" << tmp << " = " << fops.at(cop) << " " << F64 << " " << l << ", " << r << "\n";
            rhs = "%" + tmp;
          }
          rhs_ready = true;
        } else if (cop == "+") {
          if (isPrimTy(target_ty, FarTypeId::String) || isPrimTy(value_ty, FarTypeId::String) ||
              isPrimTy(target_ty, FarTypeId::Char) || isPrimTy(value_ty, FarTypeId::Char)) {
            std::string l = (complex && isPrimTy(target_ty, FarTypeId::String)) ? cur : emitExprAsString(*a.target);
            std::string r = emitExprAsString(*a.value);
            std::string lp = gen_->fresh("sp");
            std::string rp = gen_->fresh("sp");
            gen_->out_ << "  %" << lp << " = inttoptr " << I64 << " " << l << " to i8*\n";
            gen_->out_ << "  %" << rp << " = inttoptr " << I64 << " " << r << " to i8*\n";
            std::string res = gen_->fresh();
            gen_->out_ << "  %" << res << " = call i8* @far_str_concat(i8* %" << lp << ", i8* %" << rp
                       << ")\n";
            std::string as_i64 = gen_->fresh();
            gen_->out_ << "  %" << as_i64 << " = ptrtoint i8* %" << res << " to " << I64 << "\n";
            rhs = "%" + as_i64;
            rhs_ready = true;
          } else if (!isUnsignedTypeDesc(target_ty)) {
            rhs = emitGuardedSignedAdd(cur, right);
            rhs_ready = true;
          } else {
            gen_->out_ << "  %" << tmp << " = add " << I64 << " " << cur << ", " << right << "\n";
          }
        } else if (cop == "-") {
          if (!isUnsignedTypeDesc(target_ty)) {
            rhs = emitGuardedSignedSub(cur, right);
            rhs_ready = true;
          } else {
            gen_->out_ << "  %" << tmp << " = sub " << I64 << " " << cur << ", " << right << "\n";
          }
        } else if (cop == "*") {
          if (!isUnsignedTypeDesc(target_ty)) {
            rhs = emitGuardedSignedMul(cur, right);
            rhs_ready = true;
          } else {
            gen_->out_ << "  %" << tmp << " = mul " << I64 << " " << cur << ", " << right << "\n";
          }
        } else if (cop == "/") {
          if (isUnsignedTypeDesc(target_ty))
            rhs = emitGuardedUdiv(cur, right);
          else
            rhs = emitGuardedSdiv(cur, right);
          rhs_ready = true;
        } else if (cop == "//") {
          if (isUnsignedTypeDesc(target_ty)) {
            rhs = emitGuardedUdiv(cur, right);
            rhs_ready = true;
          } else {
          std::string q = emitGuardedSdiv(cur, right);
          std::string rem = emitGuardedSrem(cur, right);
          std::string r_ne = gen_->fresh();
          gen_->out_ << "  %" << r_ne << " = icmp ne " << I64 << " " << rem << ", 0\n";
          std::string a_neg = gen_->fresh();
          std::string b_neg = gen_->fresh();
          gen_->out_ << "  %" << a_neg << " = icmp slt " << I64 << " " << cur << ", 0\n";
          gen_->out_ << "  %" << b_neg << " = icmp slt " << I64 << " " << right << ", 0\n";
          std::string signs = gen_->fresh();
          gen_->out_ << "  %" << signs << " = xor i1 %" << a_neg << ", %" << b_neg << "\n";
          std::string need = gen_->fresh();
          gen_->out_ << "  %" << need << " = and i1 %" << r_ne << ", %" << signs << "\n";
          std::string q_adj = gen_->fresh();
          gen_->out_ << "  %" << q_adj << " = sub " << I64 << " " << q << ", 1\n";
          gen_->out_ << "  %" << tmp << " = select i1 %" << need << ", " << I64 << " %" << q_adj << ", " << I64
                     << " " << q << "\n";
          }
        } else if (cop == "%") {
          if (isUnsignedTypeDesc(target_ty))
            rhs = emitGuardedUrem(cur, right);
          else
            rhs = emitGuardedSrem(cur, right);
          rhs_ready = true;
        } else if (cop == "**")
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_ipow(" << I64 << " " << cur << ", " << I64
                     << " " << right << ")\n";
        else if (cop == "&")
          gen_->out_ << "  %" << tmp << " = and " << I64 << " " << cur << ", " << right << "\n";
        else if (cop == "|")
          gen_->out_ << "  %" << tmp << " = or " << I64 << " " << cur << ", " << right << "\n";
        else if (cop == "^")
          gen_->out_ << "  %" << tmp << " = xor " << I64 << " " << cur << ", " << right << "\n";
        else if (cop == "<<") {
          rhs = emitGuardedShl(cur, right);
          rhs_ready = true;
        }
        else if (cop == ">>") {
          TypeDesc tgt_ty = exprType(*a.target);
          rhs = emitGuardedShr(cur, right, isUnsignedTypeDesc(tgt_ty));
          rhs_ready = true;
        }
        else
          throw FarError("unsupported compound assignment");
        if (!rhs_ready)
          rhs = "%" + tmp;
        if (a.op != "=" && use_float) {
          if (isPrimitiveDesc(target_ty) &&
              (target_ty.primitive == FarTypeId::F32 || target_ty.primitive == FarTypeId::F16))
            rhs = gen_->emitCastValueDesc(TypeDesc::prim(FarTypeId::F64), target_ty, rhs);
          else if (!(isPrimitiveDesc(target_ty) && isFloatType(target_ty.primitive)))
            rhs = gen_->emitCastValueDesc(TypeDesc::prim(FarTypeId::F64), target_ty, rhs);
        }
      }
      if (a.op == "=")
        rhs = coerceToType(*a.value, target_ty, rhs);
      rhs = narrowIntToDesc(target_ty, rhs);
      storeTarget(rhs);
      if (a.target->kind == Expr::Variable) {
        if (a.op == "=")
          trackConstLocal(a.target->var.name, *a.value);
        else {
          const_int_locals_.erase(a.target->var.name);
          const_float_locals_.erase(a.target->var.name);
        }
      }
      return rhs;
    }

    std::string emitPrefixExpr(const PrefixExpr& p) {
      if (p.op == "++" || p.op == "--") {
        if (p.operand->kind != Expr::Variable)
          throw FarError("increment requires variable operand");
        TypeDesc op_ty = exprType(*p.operand);
        const char* llvm_ty = slotLlvmTypeDesc(op_ty);
        bool is_float = isPrimitiveDesc(op_ty) && isFloatType(op_ty.primitive);
        std::string cur = loadVar(p.operand->var.name);
        std::string result;
        if (is_float) {
          std::string tmp = gen_->fresh();
          if (p.op == "++")
            gen_->out_ << "  %" << tmp << " = fadd " << F64 << " " << cur << ", 1.0\n";
          else
            gen_->out_ << "  %" << tmp << " = fsub " << F64 << " " << cur << ", 1.0\n";
          result = "%" + tmp;
          if (isPrimitiveDesc(op_ty) &&
              (op_ty.primitive == FarTypeId::F32 || op_ty.primitive == FarTypeId::F16))
            result = gen_->emitCastValueDesc(TypeDesc::prim(FarTypeId::F64), op_ty, result);
        } else if (isUnsignedTypeDesc(op_ty)) {
          std::string tmp = gen_->fresh();
          if (p.op == "++")
            gen_->out_ << "  %" << tmp << " = add " << I64 << " " << cur << ", 1\n";
          else
            gen_->out_ << "  %" << tmp << " = sub " << I64 << " " << cur << ", 1\n";
          result = "%" + tmp;
        } else {
          result = (p.op == "++") ? emitGuardedSignedAdd(cur, "1") : emitGuardedSignedSub(cur, "1");
        }
        (void)llvm_ty;
        storeVar(p.operand->var.name, narrowIntToDesc(op_ty, result), op_ty);
        assigned_.insert(p.operand->var.name);
        const_int_locals_.erase(p.operand->var.name);
        const_float_locals_.erase(p.operand->var.name);
        return result;
      }
      if (p.op == "~") {
        std::string val = emitExpr(*p.operand);
        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = xor " << I64 << " " << val << ", -1\n";
        return "%" + tmp;
      }
      if (p.op == "*") {
        TypeDesc ptr_ty = exprType(*p.operand);
        std::string val = emitExpr(*p.operand);
        return emitPtrDeref(memCtx(), ptr_ty, val);
      }
      if (p.op == "&") {
        if (p.operand->kind != Expr::Variable)
          throw FarError("address-of requires variable operand");
        auto it = env_.find(p.operand->var.name);
        if (it == env_.end())
          throw FarError("undefined variable '" + p.operand->var.name + "'");
        return emitAddressOf(memCtx(), it->second.slot, slotLlvmTypeDesc(it->second.type));
      }
      throw FarError("unknown prefix operator '" + p.op + "'");
    }

    std::string emitPostfixExpr(const PostfixExpr& p) {
      if (p.op == "++" || p.op == "--") {
        if (p.operand->kind != Expr::Variable)
          throw FarError("increment requires variable operand");
        TypeDesc op_ty = exprType(*p.operand);
        bool is_float = isPrimitiveDesc(op_ty) && isFloatType(op_ty.primitive);
        std::string cur = loadVar(p.operand->var.name);
        std::string result;
        if (is_float) {
          std::string tmp = gen_->fresh();
          if (p.op == "++")
            gen_->out_ << "  %" << tmp << " = fadd " << F64 << " " << cur << ", 1.0\n";
          else
            gen_->out_ << "  %" << tmp << " = fsub " << F64 << " " << cur << ", 1.0\n";
          result = "%" + tmp;
          if (isPrimitiveDesc(op_ty) &&
              (op_ty.primitive == FarTypeId::F32 || op_ty.primitive == FarTypeId::F16))
            result = gen_->emitCastValueDesc(TypeDesc::prim(FarTypeId::F64), op_ty, result);
        } else if (isUnsignedTypeDesc(op_ty)) {
          std::string tmp = gen_->fresh();
          if (p.op == "++")
            gen_->out_ << "  %" << tmp << " = add " << I64 << " " << cur << ", 1\n";
          else
            gen_->out_ << "  %" << tmp << " = sub " << I64 << " " << cur << ", 1\n";
          result = "%" + tmp;
        } else {
          result = (p.op == "++") ? emitGuardedSignedAdd(cur, "1") : emitGuardedSignedSub(cur, "1");
        }
        storeVar(p.operand->var.name, narrowIntToDesc(op_ty, result), op_ty);
        assigned_.insert(p.operand->var.name);
        const_int_locals_.erase(p.operand->var.name);
        const_float_locals_.erase(p.operand->var.name);
        return cur;
      }
      if (p.op == "!?") {
        std::string val = emitExpr(*p.operand);
        TypeDesc ty = exprType(*p.operand);
        if (isOptionDesc(ty)) {
          std::string tmp = gen_->fresh("unwrap");
          gen_->out_ << "  %" << tmp << " = call i64 @far_option_unwrap(i64 " << val << ")\n";
          emitPendingThrowResumeCheck();
          return "%" + tmp;
        }
        std::string is_n = gen_->fresh("nulchk");
        gen_->out_ << "  %" << is_n << " = icmp eq " << I64 << " " << val << ", 0\n";
        std::string ok = gen_->fresh("nulok");
        std::string bad = gen_->fresh("nulbad");
        gen_->out_ << "  br i1 %" << is_n << ", label %" << bad << ", label %" << ok << "\n";
        gen_->out_ << bad << ":\n";
        gen_->out_ << "  call void @far_panic(i64 0)\n";
        gen_->out_ << "  unreachable\n";
        gen_->out_ << ok << ":\n";
        return val;
      }
      throw FarError("unknown postfix operator '" + p.op + "'");
    }

    std::string coerceToType(const Expr& expr, const TypeDesc& target, const std::string& val) {
      TypeDesc from = exprType(expr);
      if (typeDescEquals(from, target))
        return val;
      if (canAssignTypes(from, target))
        return gen_->emitCastValueDesc(from, target, val);
      throw FarError("cannot coerce " + typeDescName(from) + " to " + typeDescName(target));
    }

    void emitExprToSlot(const Expr& expr, const TypeDesc& result_ty, const std::string& slot, const char* st,
                        const std::string& done) {
      if (expr.kind == Expr::TernaryExprK) {
        emitTernaryToSlot(expr.ternary, result_ty, slot, st, done);
        return;
      }
      std::string v = coerceToType(expr, result_ty, emitExpr(expr));
      gen_->out_ << "  store " << st << " " << v << ", " << st << "* %" << slot << "\n";
      gen_->out_ << "  br label %" << done << "\n";
    }

    void emitTernaryToSlot(const TernaryExpr& t, const TypeDesc& result_ty, const std::string& slot, const char* st,
                           const std::string& done) {
      ConstBool cb = constBoolFromExpr(*t.cond);
      if (cb == ConstBool::True) {
        emitExprToSlot(*t.then_br, result_ty, slot, st, done);
        return;
      }
      if (cb == ConstBool::False) {
        emitExprToSlot(*t.else_br, result_ty, slot, st, done);
        return;
      }
      std::string cond = emitBranchCond(*t.cond);
      std::string then_label = gen_->fresh("then");
      std::string else_label = gen_->fresh("else");
      std::string join = gen_->fresh("tjoin");
      gen_->out_ << "  br i1 " << cond << ", label %" << then_label << ", label %" << else_label << "\n";
      gen_->out_ << then_label << ":\n";
      emitExprToSlot(*t.then_br, result_ty, slot, st, join);
      gen_->out_ << else_label << ":\n";
      emitExprToSlot(*t.else_br, result_ty, slot, st, join);
      gen_->out_ << join << ":\n";
      gen_->out_ << "  br label %" << done << "\n";
    }

    std::string emitTernaryExpr(const TernaryExpr& t, const TypeDesc& result_ty) {
      std::string slot = gen_->fresh("tslot");
      const char* st = slotLlvmTypeDesc(result_ty);
      gen_->hoisted_allocas_ << "  %" << slot << " = alloca " << st << "\n";
      std::string done = gen_->fresh("tdone");
      emitTernaryToSlot(t, result_ty, slot, st, done);
      gen_->out_ << done << ":\n";
      std::string loaded = gen_->fresh("tld");
      gen_->out_ << "  %" << loaded << " = load " << st << ", " << st << "* %" << slot << "\n";
      return "%" + loaded;
    }

    std::string compileTimeTypeTag(const TypeDesc& td) {
      if (isUserDesc(td)) {
        const UserTypeDef* ut =
            resolveUserType(td, gen_->obj_reg_, const_cast<Program&>(gen_->program_));
        if (ut)
          return std::to_string(ut->type_tag);
      }
      return std::to_string(typeTag(td));
    }

    std::string compileTimeTypeTagFromUnary(const TypeUnaryExpr& t) {
      TypeDesc td = t.has_type ? t.type_arg : exprType(*t.value);
      return compileTimeTypeTag(td);
    }

    std::string emitTypeUnaryExpr(const TypeUnaryExpr& t) {
      if (t.op == "typeof") {
        TypeDesc td = t.has_type ? t.type_arg : exprType(*t.value);
        return emitStringLit(typeDescName(td));
      }
      if (t.op == "type_tag") {
        return compileTimeTypeTagFromUnary(t);
      }
      if (t.op == "sizeof") {
        if (t.has_type)
          return std::to_string(elemSizeBytes(t.type_arg));
        return std::to_string(elemSizeBytes(t.value->type));
      }
      if (t.op == "alignof") {
        if (t.has_type)
          return std::to_string(elemAlignBytes(t.type_arg));
        return std::to_string(elemAlignBytes(t.value->type));
      }
      if (t.op == "stackalloc") {
        if (!t.has_type || !t.value)
          throw FarError("stackalloc requires element type and count");
        std::string count = emitExpr(*t.value);
        return emitStackAlloc(memCtx(), t.type_arg, count);
      }
      throw FarError("unknown type operator");
    }

    std::string emitIsExpr(const IsExpr& is) {
      std::string val = emitExpr(*is.value);
      TypeDesc val_ty = exprType(*is.value);
      (void)val;
      if (isUserDesc(is.type)) {
        const UserTypeDef* ut = gen_->obj_reg_.lookup(is.type.user_name);
        if (!ut)
          return "0";
        if (typeDescEquals(val_ty, is.type))
          return "1";
        return "0";
      }
      return typeDescEquals(val_ty, is.type) ? "1" : "0";
    }

    std::string emitExpr(const Expr& expr) {
      if (++emit_depth_ > 512)
        throw FarError("codegen expression depth exceeded");
      struct EmitDepthGuard {
        Ctx* self;
        explicit EmitDepthGuard(Ctx* c) : self(c) {}
        ~EmitDepthGuard() {
          if (self && self->emit_depth_ > 0)
            --self->emit_depth_;
        }
      } depth_guard(this);
      switch (expr.kind) {
        case Expr::Int:
          return intLitCodegenText(expr);
        case Expr::Float: {
          std::string dlit = gen_->formatDoubleLiteral(expr.float_lit.value);
          if (isPrimTy(expr.type, FarTypeId::F32))
            return gen_->emitFloatConstAsDouble(expr.float_lit.value);
          return dlit;
        }
        case Expr::String:
          return emitStringLit(expr.string_lit.value);
        case Expr::Char: {
          std::string wide = gen_->fresh();
          gen_->out_ << "  %" << wide << " = zext i16 " << expr.char_lit.value << " to " << I64 << "\n";
          return "%" + wide;
        }
        case Expr::Variable:
          return loadVar(expr.var.name);
        case Expr::Binary:
          return emitBinOp(expr.bin_op, expr.type);
        case Expr::FnCall: {
          std::string result = emitCall(expr.call, expr.type);
          emitPendingThrowResumeCheck();
          return result;
        }
        case Expr::CastExpr: {
          std::string val = emitExpr(*expr.cast.value);
          return gen_->emitCastValueDesc(expr.cast.value->type, expr.cast.target, val);
        }
        case Expr::TypeConstExpr: {
          const FarTypeInfo& info = typeInfo(expr.type_const.type);
          double fv = expr.type_const.is_max ? info.max_f : info.min_f;
          if (expr.type_const.type == FarTypeId::F32)
            return gen_->emitFloatConstAsDouble(fv);
          if (expr.type_const.type == FarTypeId::F64)
            return gen_->formatDoubleLiteral(fv);
          int64_t iv = expr.type_const.is_max ? info.max_i : info.min_i;
          return std::to_string(iv);
        }
        case Expr::SpawnExpr:
          return emitSpawn(expr.spawn);
        case Expr::ParallelExpr:
          return emitParallel(expr.parallel);
        case Expr::IndexExpr:
          return emitIndex(expr.index);
        case Expr::SliceExpr:
          return emitSlice(expr.slice);
        case Expr::ArrayLitExpr:
          return emitArrayLit(expr.array_lit);
        case Expr::DictLitExpr: {
          TypeDesc key = expr.type.args.size() >= 1 ? expr.type.args[0] : TypeDesc::prim(FarTypeId::I64);
          TypeDesc val = expr.type.args.size() >= 2 ? expr.type.args[1] : TypeDesc::prim(FarTypeId::I64);
          return emitDictLit(collCtx(), expr.dict_lit, key, val);
        }
        case Expr::TupleLitExpr: {
          size_t n = expr.tuple_lit.elements.size();
          std::string handle = gen_->fresh("tuple");
          gen_->out_ << "  %" << handle << " = call i64 @far_tarray_new(i64 " << n << ", i16 0, i64 8)\n";
          for (size_t i = 0; i < n; ++i) {
            std::string val = emitExpr(*expr.tuple_lit.elements[i]);
            gen_->out_ << "  call void @far_tarray_set(i64 %" << handle << ", i64 " << i << ", i64 " << val
                       << ")\n";
          }
          return "%" + handle;
        }
        case Expr::MemberExpr: {
          TypeDesc obj_ty = exprType(*expr.member.object);
          if (obj_ty.form == TypeForm::Tuple) {
            int64_t raw_idx =
                parseIntLiteral(expr.member.member.substr(1), expr.line, expr.col);
            if (raw_idx < 0 || static_cast<size_t>(raw_idx) >= obj_ty.args.size())
              throw FarError("tuple field index out of range");
            size_t idx = static_cast<size_t>(raw_idx);
            std::string obj_val = emitExpr(*expr.member.object);
            std::string tmp = gen_->fresh("tupf");
            gen_->out_ << "  %" << tmp << " = call i64 @far_tarray_get(i64 " << obj_val << ", i64 " << idx
                       << ")\n";
            return "%" + tmp;
          }
          if (isUserDesc(obj_ty)) {
            std::string obj_val = emitExpr(*expr.member.object);
            std::string getter = "__prop_get_" + expr.member.member;
            if (gen_->obj_reg_.lookupMethod(obj_ty, getter)) {
              std::string sym = userMangleMethod(userTypeKey(obj_ty), getter);
              std::string tmp = gen_->fresh("pget");
              gen_->out_ << "  %" << tmp << " = call i64 @" << sym << "(i64 " << obj_val << ")\n";
              return "%" + tmp;
            }
            return emitUserMember(userCtx(), gen_->obj_reg_, expr.member, obj_ty, obj_val);
          }
          return emitAggregateMember(aggCtx(), expr.member, aggregateDescId(obj_ty));
        }
        case Expr::FnLitExpr: {
          std::string sym = "__lambda_" + std::to_string(expr.fn_lit.id);
          auto it = gen_->fn_by_name_.find(sym);
          if (it == gen_->fn_by_name_.end() || it->second.empty())
            throw FarError("internal: missing lambda " + sym);
          const Function* lf = it->second.back();
          return emitClosureNew(lf, expr.fn_lit.captures);
        }
        case Expr::AwaitExprK: {
          std::string handle = emitExpr(*expr.await.value);
          std::string tmp = gen_->fresh("await");
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_await(" << I64 << " " << handle << ")\n";
          return "%" + tmp;
        }
        case Expr::AssignExprK:
          return emitAssignExpr(expr.assign);
        case Expr::TernaryExprK:
          return emitTernaryExpr(expr.ternary, expr.type);
        case Expr::PrefixExprK:
          return emitPrefixExpr(expr.prefix);
        case Expr::PostfixExprK:
          return emitPostfixExpr(expr.postfix);
        case Expr::TypeUnaryExprK:
          return emitTypeUnaryExpr(expr.type_unary);
        case Expr::IsExprK:
          return emitIsExpr(expr.is_expr);
        case Expr::AsExprK: {
          std::string val = emitExpr(*expr.as_expr.value);
          return gen_->emitCastValueDesc(expr.as_expr.value->type, expr.as_expr.type, val);
        }
        case Expr::MethodExpr: {
          if (expr.method_call.is_type_construct) {
            const std::string& type_name = expr.method_call.method;
            const UserTypeDef* ut = gen_->obj_reg_.lookup(type_name);
            if (!ut)
              throw FarError("unknown type '" + type_name + "'");
            std::vector<std::string> arg_vals;
            for (const auto& a : expr.method_call.args)
              arg_vals.push_back(emitExpr(*a));
            if (expr.method_call.resolved_ctor)
              return emitUserConstructCall(userCtx(), *ut, *expr.method_call.resolved_ctor, arg_vals);
            return emitUserConstruct(userCtx(), *ut, arg_vals);
          }
          if (expr.method_call.is_module_call) {
            Call call;
            call.name = expr.method_call.resolved_fn;
            call.resolved = expr.method_call.resolved;
            call.resolved_llvm_name = expr.method_call.resolved_llvm_name;
            for (const auto& a : expr.method_call.args)
              call.bound_exprs.push_back(a.get());
            std::string result = emitCall(call, expr.type);
            emitPendingThrowResumeCheck();
            return result;
          }
          if (expr.method_call.is_geom_call)
            return emitAggregateStaticCall(aggCtx(), expr.method_call, expr.method_call.geom_agg_type,
                                           primTy(expr.type));
          TypeDesc obj_ty = exprType(*expr.method_call.object);
          if (isOptionDesc(obj_ty) || isResultDesc(obj_ty)) {
            std::string obj_val = emitExpr(*expr.method_call.object);
            std::string result = emitErrMethod(errCtx(), expr.method_call, obj_ty, obj_val);
            if (expr.method_call.method == "unwrap")
              emitPendingThrowResumeCheck();
            return result;
          }
          if (isConcurrencyHandleDesc(obj_ty)) {
            std::string obj_val = emitExpr(*expr.method_call.object);
            return emitConcMethod(concCtx(), expr.method_call, obj_ty, obj_val);
          }
          if (isMemoryHandleDesc(obj_ty)) {
            std::string obj_val = emitExpr(*expr.method_call.object);
            return emitMemMethod(memCtx(), expr.method_call, obj_ty, obj_val);
          }
          if (isPrimTy(obj_ty, FarTypeId::String) || isPrimTy(obj_ty, FarTypeId::RawString)) {
            std::string obj_val = emitExpr(*expr.method_call.object);
            return emitStrMethod(strCtx(), expr.method_call, obj_val);
          }
          if (isUserDesc(obj_ty)) {
            std::string obj_val = emitExpr(*expr.method_call.object);
            const UserTypeDef* ut = gen_->obj_reg_.lookup(obj_ty.user_name);
            if (ut && ut->kind == UserTypeKind::Actor && lookupActorMethod(expr.method_call.method))
              return emitActorMethod(concCtx(), expr.method_call, obj_val);
            std::vector<std::string> args;
            for (const auto& a : expr.method_call.args)
              args.push_back(emitExpr(*a));
            return emitUserMethodCall(userCtx(), gen_->obj_reg_, expr.method_call, obj_ty, obj_val, args);
          }
          if (isCollectionDesc(obj_ty))
            return emitCollectionMethod(collCtx(), expr.method_call, obj_ty, expr.type);
          return emitAggregateMethod(aggCtx(), expr.method_call, aggregateDescId(obj_ty), primTy(expr.type));
        }
        case Expr::EnumVariantExprK:
          return std::to_string(expr.enum_variant.value);
        case Expr::UnionVariantExprK: {
          std::vector<std::string> args;
          for (const auto& a : expr.union_variant.args)
            args.push_back(emitExpr(*a));
          return emitUnionConstruct(patCtx(), expr.union_variant, args);
        }
        case Expr::MacroSubstExprK:
        case Expr::MacroInvokeExprK:
        case Expr::ComptimeExprK:
          throw FarError("unexpanded macro/comptime expression in codegen");
      }
      throw FarError("unsupported expression");
    }

    std::string emitIndex(const Index& idx) {
      TypeDesc arr_ty = exprType(*idx.array);
      std::string arr = emitExpr(*idx.array);
      std::string index = emitExpr(*idx.index);
      if (isUserDesc(arr_ty)) {
        std::string sym = userMangleMethod(userTypeKey(arr_ty), "__index_get");
        std::string tmp = gen_->fresh("idx");
        gen_->out_ << "  %" << tmp << " = call i64 @" << sym << "(i64 " << arr << ", i64 " << index << ")\n";
        return "%" + tmp;
      }
      if (arr_ty.form == TypeForm::Array || arr_ty.form == TypeForm::List || arr_ty.form == TypeForm::Dict ||
          arr_ty.form == TypeForm::Slice || isPrimTy(arr_ty, FarTypeId::Arr))
        return emitCollectionIndex(collCtx(), arr_ty, arr, index);
      if (isPrimTy(arr_ty, FarTypeId::String) || isPrimTy(arr_ty, FarTypeId::RawString)) {
        std::string ptr = gen_->fresh("sptr");
        gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << arr << " to i8*\n";
        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_str_char_at(i8* %" << ptr << ", " << I64
                   << " " << index << ")\n";
        return "%" + tmp;
      }
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_array_get(" << I64 << " " << arr << ", " << I64
                 << " " << index << ")\n";
      return "%" + tmp;
    }

    std::string emitSlice(const Slice& sl) {
      TypeDesc arr_ty = exprType(*sl.array);
      std::string arr = emitExpr(*sl.array);
      std::string start = sl.start ? emitExpr(*sl.start) : "0";
      std::string end = sl.end ? emitExpr(*sl.end) : emitCollectionLen(collCtx(), arr_ty, arr);
      if (arr_ty.form == TypeForm::Array || arr_ty.form == TypeForm::List || arr_ty.form == TypeForm::Slice ||
          isPrimTy(arr_ty, FarTypeId::Arr))
        return emitCollectionSlice(collCtx(), arr_ty, arr, start, end);
      if (isPrimTy(arr_ty, FarTypeId::String) || isPrimTy(arr_ty, FarTypeId::RawString)) {
        std::string ptr = gen_->fresh("sptr");
        gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << arr << " to i8*\n";
        std::string slice_ptr = gen_->fresh("sslice");
        gen_->out_ << "  %" << slice_ptr << " = call i8* @far_str_slice(i8* %" << ptr << ", " << I64
                   << " " << start << ", " << I64 << " " << end << ")\n";
        std::string as_i64 = gen_->fresh();
        gen_->out_ << "  %" << as_i64 << " = ptrtoint i8* %" << slice_ptr << " to " << I64 << "\n";
        return "%" + as_i64;
      }
      throw FarError("cannot slice type in codegen");
    }

    std::string emitArrayLit(const ArrayLit& lit) {
      TypeDesc elem = lit.elements.empty() ? TypeDesc::prim(FarTypeId::I64) : lit.elements[0]->type;
      return emitTypedArrayLit(collCtx(), lit, elem);
    }

    std::string emitParallel(const Parallel& par) {

      auto it = gen_->fn_by_name_.find(par.fn_name);

      if (it == gen_->fn_by_name_.end() || it->second.empty())

        throw FarError("undefined function '" + par.fn_name + "'");

      const Function* fn = it->second[0];

      if (fn->params.size() != 1)

        throw FarError("parallel worker must take one argument (thread id)");

      std::string count;

      if (par.count) {

        count = emitExpr(*par.count);

      } else {

        std::string tmp = gen_->fresh();

        gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_thread_count()\n";

        count = "%" + tmp;

      }

      std::string fn_ptr = fnPointerBitcastLlvm(fn->llvm_name, fn);

      std::string tmp = gen_->fresh();

      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_parallel(i8* " << fn_ptr << ", " << I64 << " "

                 << count << ")\n";

      return "%" + tmp;

    }



    std::string fnPointerBitcastLlvm(const std::string& llvm_sym, const Function* fn) {
      std::ostringstream sig;
      sig << llvmAbiTypeDesc(fn->return_type) << " (";
      for (size_t i = 0; i < fn->params.size(); ++i) {
        if (fn->params[i].is_variadic)
          continue;
        if (i > 0)
          sig << ", ";
        sig << llvmAbiTypeDesc(fn->params[i].type);
      }
      sig << ")*";
      std::string ptr = gen_->fresh("fnptr");
      gen_->out_ << "  %" << ptr << " = bitcast " << sig.str() << " @" << llvm_sym << " to i8*\n";
      return "%" + ptr;
    }



    std::string emitSpawn(const Spawn& spawn) {

      const Expr& call_expr = *spawn.call;

      if (call_expr.kind != Expr::FnCall)

        throw FarError("spawn requires a function call");

      const Call& call = call_expr.call;

      if (!call.resolved)
        throw FarError("spawn requires resolved function call");

      const Function* fn = call.resolved;

      size_t nargs = call.bound_exprs.size();

      if (nargs > 4)

        throw FarError("spawn supports at most 4 arguments");

      std::string fn_ptr = fnPointerBitcastLlvm(call.resolved_llvm_name, fn);

      std::ostringstream args;

      args << "i8* " << fn_ptr << ", " << I64 << " " << nargs;

      for (size_t i = 0; i < 4; ++i) {

        args << ", " << I64 << " ";

        args << (i < nargs ? emitExpr(*call.bound_exprs[i]) : "0");

      }

      std::string tmp = gen_->fresh();

      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_spawn(" << args.str() << ")\n";

      return "%" + tmp;

    }



    std::string emitBinOp(const BinOp& op, const TypeDesc& result_ty) {

      if (op.op == "and" || op.op == "&&") return emitShortCircuitAnd(*op.left, *op.right);

      if (op.op == "or" || op.op == "||") return emitShortCircuitOr(*op.left, *op.right);

      TypeDesc lt = exprType(*op.left);
      if (isUserDesc(lt)) {
        const UserMethod* om = gen_->obj_reg_.lookupMethod(lt, userOpMethodName(op.op));
        if (om) {
          std::string obj = emitExpr(*op.left);
          std::string rval = emitExpr(*op.right);
          std::string sym = userMangleMethod(userTypeKey(lt), om->name);
          std::string tmp = gen_->fresh("uop");
          gen_->out_ << "  %" << tmp << " = call i64 @" << sym << "(i64 " << obj << ", i64 " << rval << ")\n";
          return "%" + tmp;
        }
      }

      if (op.op == "??") {
        if (exprConstNonNullish(*op.left))
          return coerceToType(*op.left, result_ty, emitExpr(*op.left));
        if (op.left->kind == Expr::Int && op.left->int_lit.value == 0)
          return coerceToType(*op.right, result_ty, emitExpr(*op.right));
        if (op.left->kind == Expr::Float && op.left->float_lit.value == 0.0)
          return coerceToType(*op.right, result_ty, emitExpr(*op.right));
        if (op.left->kind == Expr::String && op.left->string_lit.value.empty())
          return coerceToType(*op.right, result_ty, emitExpr(*op.right));
        const char* st = slotLlvmTypeDesc(result_ty);
        std::string slot = gen_->fresh("ncslot");
        gen_->hoisted_allocas_ << "  %" << slot << " = alloca " << st << "\n";
        std::string left = emitExpr(*op.left);
        std::string is_n = emitIsNullish(*op.left, left);
        std::string rhs_label = gen_->fresh("nclr");
        std::string end_label = gen_->fresh("nclend");
        std::string ok_label = gen_->fresh("nclok");
        gen_->out_ << "  br i1 " << is_n << ", label %" << rhs_label << ", label %" << ok_label << "\n";
        gen_->out_ << ok_label << ":\n";
        std::string left_ok = coerceToType(*op.left, result_ty, left);
        gen_->out_ << "  store " << st << " " << left_ok << ", " << st << "* %" << slot << "\n";
        gen_->out_ << "  br label %" << end_label << "\n";
        gen_->out_ << rhs_label << ":\n";
        std::string right = coerceToType(*op.right, result_ty, emitExpr(*op.right));
        gen_->out_ << "  store " << st << " " << right << ", " << st << "* %" << slot << "\n";
        gen_->out_ << "  br label %" << end_label << "\n";
        gen_->out_ << end_label << ":\n";
        std::string loaded = gen_->fresh("ncld");
        gen_->out_ << "  %" << loaded << " = load " << st << ", " << st << "* %" << slot << "\n";
        return "%" + loaded;
      }

      if (op.op == "in" || op.op == "not in") {
        TypeDesc rt = exprType(*op.right);
        std::string needle = emitExpr(*op.left);
        std::string hay = emitExpr(*op.right);
        std::string tmp = gen_->fresh();
        if (isPrimTy(rt, FarTypeId::String)) {
          std::string hay_ptr = gen_->fresh("sp");
          std::string needle_ptr = gen_->fresh("sp");
          gen_->out_ << "  %" << hay_ptr << " = inttoptr " << I64 << " " << hay << " to i8*\n";
          gen_->out_ << "  %" << needle_ptr << " = inttoptr " << I64 << " " << needle << " to i8*\n";
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_str_contains(i8* %" << hay_ptr << ", i8* %"
                     << needle_ptr << ")\n";
        } else if (rt.form == TypeForm::Set) {
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_set_contains(" << I64 << " " << hay << ", "
                     << I64 << " " << needle << ")\n";
        } else if (rt.form == TypeForm::Dict) {
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_dict_contains_key(" << I64 << " " << hay
                     << ", " << I64 << " " << needle << ")\n";
        } else {
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_tarray_contains(" << I64 << " " << hay << ", "
                     << I64 << " " << needle << ")\n";
        }
        if (op.op == "not in") {
          std::string inv = gen_->fresh();
          gen_->out_ << "  %" << inv << " = icmp eq " << I64 << " %" << tmp << ", 0\n";
          std::string z = gen_->fresh();
          gen_->out_ << "  %" << z << " = zext i1 %" << inv << " to " << I64 << "\n";
          return "%" + z;
        }
        std::string ne0 = gen_->fresh();
        gen_->out_ << "  %" << ne0 << " = icmp ne " << I64 << " %" << tmp << ", 0\n";
        std::string z = gen_->fresh();
        gen_->out_ << "  %" << z << " = zext i1 %" << ne0 << " to " << I64 << "\n";
        return "%" + z;
      }

      if (op.op == "&" || op.op == "|" || op.op == "^" || op.op == "<<" || op.op == ">>") {
        std::string left = emitExpr(*op.left);
        std::string right = emitExpr(*op.right);
        std::string tmp = gen_->fresh();
        if (op.op == "&")
          gen_->out_ << "  %" << tmp << " = and " << I64 << " " << left << ", " << right << "\n";
        else if (op.op == "|")
          gen_->out_ << "  %" << tmp << " = or " << I64 << " " << left << ", " << right << "\n";
        else if (op.op == "^")
          gen_->out_ << "  %" << tmp << " = xor " << I64 << " " << left << ", " << right << "\n";
        else if (op.op == "<<")
          return emitGuardedShl(left, right);
        else if (op.op == ">>") {
          if (isUnsignedTypeDesc(lt))
            return emitGuardedShr(left, right, true);
          return emitGuardedShr(left, right, false);
        }
        return "%" + tmp;
      }


      if (op.op == "!") {

        std::string val = emitExpr(*op.right);

        std::string cmp = gen_->fresh();

        gen_->out_ << "  %" << cmp << " = icmp eq " << I64 << " " << val << ", 0\n";

        std::string z = gen_->fresh();

        gen_->out_ << "  %" << z << " = zext i1 %" << cmp << " to " << I64 << "\n";

        return "%" + z;

      }

      if (op.op == "~" && op.left->kind == Expr::Int && op.left->int_lit.value == 0) {
        TypeDesc rty = exprType(*op.right);
        if (isAggregateDesc(rty))
          return emitUnaryAggregate(aggCtx(), "~", aggregateDescId(rty), emitExpr(*op.right));
        std::string val = emitExpr(*op.right);
        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = xor " << I64 << " " << val << ", -1\n";
        return "%" + tmp;
      }

      if (op.op == "?.") {
        std::string obj = emitExpr(*op.left);
        TypeDesc obj_ty = exprType(*op.left);
        TypeDesc base_ty = obj_ty;
        if (isOptionDesc(obj_ty) && !obj_ty.args.empty())
          base_ty = obj_ty.args[0];
        else if (isPointerDesc(obj_ty))
          base_ty = obj_ty.args[0];
        std::string is_n = gen_->fresh();
        if (isOptionDesc(obj_ty)) {
          std::string some = gen_->fresh("some");
          gen_->out_ << "  %" << some << " = call i64 @far_option_is_some(i64 " << obj << ")\n";
          gen_->out_ << "  %" << is_n << " = icmp eq i64 %" << some << ", 0\n";
        } else {
          gen_->out_ << "  %" << is_n << " = icmp eq " << I64 << " " << obj << ", 0\n";
        }
        std::string ok = gen_->fresh("optok");
        std::string nil = gen_->fresh("optnil");
        std::string end = gen_->fresh("optend");
        gen_->out_ << "  br i1 %" << is_n << ", label %" << nil << ", label %" << ok << "\n";
        gen_->out_ << ok << ":\n";
        std::string inner_val = obj;
        if (isOptionDesc(obj_ty)) {
          std::string unwrapped = gen_->fresh("optu");
          gen_->out_ << "  %" << unwrapped << " = call i64 @far_option_unwrap(i64 " << obj << ")\n";
          emitPendingThrowResumeCheck();
          inner_val = "%" + unwrapped;
        }
        std::string val = "0";
        if (op.right && op.right->kind == Expr::String && isUserDesc(base_ty)) {
          MemberAccess mem;
          mem.member = op.right->string_lit.value;
          val = emitUserMember(userCtx(), gen_->obj_reg_, mem, base_ty, inner_val);
        } else if (op.right && op.right->kind == Expr::String && isUserDesc(obj_ty)) {
          MemberAccess mem;
          mem.member = op.right->string_lit.value;
          val = emitUserMember(userCtx(), gen_->obj_reg_, mem, obj_ty, obj);
        }
        gen_->out_ << "  br label %" << end << "\n";
        gen_->out_ << nil << ":\n";
        gen_->out_ << "  br label %" << end << "\n";
        gen_->out_ << end << ":\n";
        std::string phi = gen_->fresh("opt");
        gen_->out_ << "  %" << phi << " = phi " << I64 << " [ 0, %" << nil << " ], [ " << val << ", %" << ok
                   << " ]\n";
        return "%" + phi;
      }

      if (op.op == "-" && op.left->kind == Expr::Int && op.left->int_lit.value == 0) {
        TypeDesc rty = exprType(*op.right);
        if (isAggregateDesc(rty))
          return emitUnaryAggregate(aggCtx(), "-", aggregateDescId(rty), emitExpr(*op.right));
        if (isPrimitiveDesc(rty) && isFloatType(rty.primitive)) {
          std::string right = emitExpr(*op.right);
          std::string tmp = gen_->fresh();
          gen_->out_ << "  %" << tmp << " = fsub " << F64 << " 0.0, " << right << "\n";
          return "%" + tmp;
        }
        std::string right = emitExpr(*op.right);
        if (isUnsignedTypeDesc(rty)) {
          std::string tmp = gen_->fresh();
          gen_->out_ << "  %" << tmp << " = sub " << I64 << " 0, " << right << "\n";
          return "%" + tmp;
        }
        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_i64_neg_checked(" << I64 << " " << right
                   << ")\n";
        return "%" + tmp;
      }

      if (op.op == "**") {
        TypeDesc lty = exprType(*op.left);
        TypeDesc rty = exprType(*op.right);
        if (!isAggregateDesc(lty) && !isAggregateDesc(rty)) {
          std::string left = emitExpr(*op.left);
          std::string right = emitExpr(*op.right);
          std::string tmp = gen_->fresh();
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_ipow(" << I64 << " " << left << ", " << I64
                     << " " << right << ")\n";
          return "%" + tmp;
        }
      }

      if (op.op == "//") {
        TypeDesc lty = exprType(*op.left);
        TypeDesc rty = exprType(*op.right);
        if (!isAggregateDesc(lty) && !isAggregateDesc(rty)) {
        if ((isPrimitiveDesc(lty) && isFloatType(lty.primitive)) ||
            (isPrimitiveDesc(rty) && isFloatType(rty.primitive)))
          throw FarError("// requires integer operands");

        std::string left = emitExpr(*op.left);

        std::string right = emitExpr(*op.right);

        if (binOpUsesUnsigned(lty, rty))
          return emitGuardedUdiv(left, right);

        std::string q = emitGuardedSdiv(left, right);

        std::string rem = emitGuardedSrem(left, right);

        std::string r_ne = gen_->fresh();

        gen_->out_ << "  %" << r_ne << " = icmp ne " << I64 << " " << rem << ", 0\n";

        std::string a_neg = gen_->fresh();

        std::string b_neg = gen_->fresh();

        gen_->out_ << "  %" << a_neg << " = icmp slt " << I64 << " " << left << ", 0\n";

        gen_->out_ << "  %" << b_neg << " = icmp slt " << I64 << " " << right << ", 0\n";

        std::string signs = gen_->fresh();

        gen_->out_ << "  %" << signs << " = xor i1 %" << a_neg << ", %" << b_neg << "\n";

        std::string need = gen_->fresh();

        gen_->out_ << "  %" << need << " = and i1 %" << r_ne << ", %" << signs << "\n";

        std::string q_adj = gen_->fresh();

        gen_->out_ << "  %" << q_adj << " = sub " << I64 << " " << q << ", 1\n";

        std::string res = gen_->fresh();

        gen_->out_ << "  %" << res << " = select i1 %" << need << ", " << I64 << " %" << q_adj << ", " << I64 << " "
                   << q << "\n";

        return "%" + res;

        }
      }

      if (op.op == "+" && (isPrimTy(exprType(*op.left), FarTypeId::String) ||
                           isPrimTy(exprType(*op.right), FarTypeId::String) ||
                           isPrimTy(exprType(*op.left), FarTypeId::Char) ||
                           isPrimTy(exprType(*op.right), FarTypeId::Char))) {

        std::string l = emitExprAsString(*op.left);

        std::string r = emitExprAsString(*op.right);

        std::string lp = gen_->fresh("sp");

        std::string rp = gen_->fresh("sp");

        gen_->out_ << "  %" << lp << " = inttoptr " << I64 << " " << l << " to i8*\n";

        gen_->out_ << "  %" << rp << " = inttoptr " << I64 << " " << r << " to i8*\n";

        std::string res = gen_->fresh();

        gen_->out_ << "  %" << res << " = call i8* @far_str_concat(i8* %" << lp << ", i8* %" << rp << ")\n";

        std::string as_i64 = gen_->fresh();

        gen_->out_ << "  %" << as_i64 << " = ptrtoint i8* %" << res << " to " << I64 << "\n";

        return "%" + as_i64;

      }

      lt = exprType(*op.left);
      TypeDesc rt = exprType(*op.right);
      if (isAggregateDesc(lt) || isAggregateDesc(rt)) {
        std::string left = emitExpr(*op.left);
        std::string right = emitExpr(*op.right);
        FarTypeId lid = aggregateDescId(lt);
        FarTypeId rid = aggregateDescId(rt);
        if (op.op == "==" || op.op == "!=")
          return emitAggregateCompare(aggCtx(), op.op, lid, left, right);
        return emitAggregateBinOp(aggCtx(), op.op, lid, rid, left, right);
      }
      std::string cmp_op = op.op;
      if (cmp_op == "===")
        cmp_op = "==";
      if (cmp_op == "!==")
        cmp_op = "!=";
      auto cmp = cmpOps.find(cmp_op);
      if (cmp != cmpOps.end()) {
        std::string left = emitExpr(*op.left);
        std::string right = emitExpr(*op.right);
        std::string tmp = gen_->fresh();
        if (((isUnsignedTypeDesc(lt) && isSignedIntegerTypeDesc(rt)) ||
             (isSignedIntegerTypeDesc(lt) && isUnsignedTypeDesc(rt)))) {
          const bool unsigned_is_left = isUnsignedTypeDesc(lt);
          std::string unsigned_val = unsigned_is_left ? left : right;
          std::string signed_val = unsigned_is_left ? right : left;
          std::string mixed_op = unsigned_is_left ? cmp_op : flipOrderedCmpOp(cmp_op);
          return emitMixedSignIntegerCompare(gen_->out_, [&](const std::string& hint) { return gen_->fresh(hint); },
                                             mixed_op, unsigned_val, signed_val);
        }
        if ((isPrimTy(lt, FarTypeId::String) || isPrimTy(lt, FarTypeId::RawString)) &&
            (isPrimTy(rt, FarTypeId::String) || isPrimTy(rt, FarTypeId::RawString)) &&
            (cmp_op == "==" || cmp_op == "!=")) {
          std::string lp = gen_->fresh("sp");
          std::string rp = gen_->fresh("sp");
          gen_->out_ << "  %" << lp << " = inttoptr " << I64 << " " << left << " to i8*\n";
          gen_->out_ << "  %" << rp << " = inttoptr " << I64 << " " << right << " to i8*\n";
          std::string eq = gen_->fresh();
          gen_->out_ << "  %" << eq << " = call " << I64 << " @far_str_equal(i8* %" << lp << ", i8* %" << rp << ")\n";
          if (cmp_op == "!=") {
            std::string ne = gen_->fresh();
            gen_->out_ << "  %" << ne << " = icmp eq " << I64 << " %" << eq << ", 0\n";
            std::string bool_tmp = gen_->fresh();
            gen_->out_ << "  %" << bool_tmp << " = zext i1 %" << ne << " to " << I64 << "\n";
            return "%" + bool_tmp;
          }
          std::string bool_tmp = gen_->fresh();
          gen_->out_ << "  %" << bool_tmp << " = icmp ne " << I64 << " %" << eq << ", 0\n";
          std::string z = gen_->fresh();
          gen_->out_ << "  %" << z << " = zext i1 %" << bool_tmp << " to " << I64 << "\n";
          return "%" + z;
        }
        if ((isPrimitiveDesc(lt) && isFloatType(lt.primitive)) ||
            (isPrimitiveDesc(rt) && isFloatType(rt.primitive))) {
          std::string l = left;
          std::string r = right;
          if (!(isPrimitiveDesc(lt) && isFloatType(lt.primitive)))
            l = gen_->emitIntToF64Desc(lt, left);
          if (!(isPrimitiveDesc(rt) && isFloatType(rt.primitive)))
            r = gen_->emitIntToF64Desc(rt, right);
          static const std::unordered_map<std::string, std::string> fcmpOps = {
              {"==", "oeq"}, {"!=", "une"}, {"<", "olt"}, {">", "ogt"}, {"<=", "ole"}, {">=", "oge"}};
          gen_->out_ << "  %" << tmp << " = fcmp " << fcmpOps.at(cmp_op) << " " << F64 << " " << l << ", " << r << "\n";
        } else {
          std::string icmp = cmp->second;
          if (binOpUsesUnsigned(lt, rt)) {
            if (icmp == "slt")
              icmp = "ult";
            else if (icmp == "sgt")
              icmp = "ugt";
            else if (icmp == "sle")
              icmp = "ule";
            else if (icmp == "sge")
              icmp = "uge";
          }
          gen_->out_ << "  %" << tmp << " = icmp " << icmp << " " << I64 << " " << left << ", " << right << "\n";
        }
        std::string bool_tmp = gen_->fresh();
        gen_->out_ << "  %" << bool_tmp << " = zext i1 %" << tmp << " to " << I64 << "\n";
        return "%" + bool_tmp;
      }

      auto arith = arithOps.find(op.op);
      if (arith == arithOps.end())
        throw FarError("unknown operator '" + op.op + "'");
      std::string left = emitExpr(*op.left);
      std::string right = emitExpr(*op.right);
      std::string tmp = gen_->fresh();
      const bool unsigned_op = binOpUsesUnsigned(lt, rt);
      if ((isPrimitiveDesc(lt) && isFloatType(lt.primitive)) ||
          (isPrimitiveDesc(rt) && isFloatType(rt.primitive))) {
        std::string l = left;
        std::string r = right;
        if (!(isPrimitiveDesc(lt) && isFloatType(lt.primitive)))
          l = gen_->emitIntToF64Desc(lt, left);
        if (!(isPrimitiveDesc(rt) && isFloatType(rt.primitive)))
          r = gen_->emitIntToF64Desc(rt, right);
        static const std::unordered_map<std::string, std::string> fops = {
            {"+", "fadd"}, {"-", "fsub"}, {"*", "fmul"}};
        if (op.op == "/") {
          std::string res = gen_->fresh();
          gen_->out_ << "  %" << res << " = call " << F64 << " @far_f64_div_checked(" << F64 << " " << l
                     << ", " << F64 << " " << r << ")\n";
          return "%" + res;
        }
        if (op.op == "%") {
          std::string res = gen_->fresh();
          gen_->out_ << "  %" << res << " = call " << F64 << " @far_f64_rem_checked(" << F64 << " " << l
                     << ", " << F64 << " " << r << ")\n";
          return "%" + res;
        }
        auto it = fops.find(op.op);
        if (it == fops.end())
          throw FarError("unknown float operator '" + op.op + "'");
        gen_->out_ << "  %" << tmp << " = " << it->second << " " << F64 << " " << l << ", " << r << "\n";
      } else {
        std::string llvm_op = arith->second;
        if (unsigned_op) {
          if (llvm_op == "sdiv")
            llvm_op = "udiv";
          else if (llvm_op == "srem")
            llvm_op = "urem";
        } else {
          if (llvm_op == "add")
            return emitGuardedSignedAdd(left, right);
          if (llvm_op == "sub")
            return emitGuardedSignedSub(left, right);
          if (llvm_op == "mul")
            return emitGuardedSignedMul(left, right);
          if (llvm_op == "sdiv")
            return emitGuardedSdiv(left, right);
          if (llvm_op == "srem")
            return emitGuardedSrem(left, right);
        }
        if (llvm_op == "udiv")
          return emitGuardedUdiv(left, right);
        if (llvm_op == "urem")
          return emitGuardedUrem(left, right);
        gen_->out_ << "  %" << tmp << " = " << llvm_op << " " << I64 << " " << left << ", " << right << "\n";
      }
      return "%" + tmp;
    }



    std::string emitGuardedUdiv(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_u64_div_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedUrem(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_u64_mod_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedShl(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_i64_shl_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedShr(const std::string& left, const std::string& right, bool unsigned_shift) {
      std::string tmp = gen_->fresh();
      const char* fn = unsigned_shift ? "far_u64_shr_checked" : "far_i64_shr_checked";
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @" << fn << "(" << I64 << " " << left << ", " << I64
                 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedSignedAdd(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_i64_add_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedSignedSub(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_i64_sub_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedSignedMul(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_i64_mul_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedSdiv(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_i64_div_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitGuardedSrem(const std::string& left, const std::string& right) {
      std::string tmp = gen_->fresh();
      gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_i64_mod_checked(" << I64 << " " << left
                 << ", " << I64 << " " << right << ")\n";
      return "%" + tmp;
    }

    std::string emitShortCircuitAnd(const Expr& left, const Expr& right) {

      ConstBool lb = constBoolFromExpr(left);
      if (lb == ConstBool::False)
        return "0";
      if (lb == ConstBool::True) {
        std::string rval = emitExpr(right);
        std::string rcond = emitTruthyCond(exprType(right), rval);
        std::string rz = gen_->fresh();
        gen_->out_ << "  %" << rz << " = zext i1 " << rcond << " to " << I64 << "\n";
        return "%" + rz;
      }

      std::string slot = gen_->fresh("andslot");
      gen_->hoisted_allocas_ << "  %" << slot << " = alloca " << I64 << "\n";

      std::string lval = emitExpr(left);

      std::string lcond = emitTruthyCond(exprType(left), lval);

      std::string rhs_label = gen_->fresh("and.rhs");

      std::string false_label = gen_->fresh("and.false");

      std::string end_label = gen_->fresh("and.end");

      gen_->out_ << "  br i1 " << lcond << ", label %" << rhs_label << ", label %" << false_label << "\n";

      gen_->out_ << false_label << ":\n";

      gen_->out_ << "  store " << I64 << " 0, " << I64 << "* %" << slot << "\n";

      gen_->out_ << "  br label %" << end_label << "\n";

      gen_->out_ << rhs_label << ":\n";

      std::string rval = emitExpr(right);

      std::string rcond = emitTruthyCond(exprType(right), rval);

      std::string rz = gen_->fresh();

      gen_->out_ << "  %" << rz << " = zext i1 " << rcond << " to " << I64 << "\n";

      gen_->out_ << "  store " << I64 << " %" << rz << ", " << I64 << "* %" << slot << "\n";

      gen_->out_ << "  br label %" << end_label << "\n";

      gen_->out_ << end_label << ":\n";

      std::string loaded = gen_->fresh("andld");

      gen_->out_ << "  %" << loaded << " = load " << I64 << ", " << I64 << "* %" << slot << "\n";

      return "%" + loaded;

    }



    std::string emitShortCircuitOr(const Expr& left, const Expr& right) {

      ConstBool lb = constBoolFromExpr(left);
      if (lb == ConstBool::True)
        return "1";
      if (lb == ConstBool::False) {
        std::string rval = emitExpr(right);
        std::string rcond = emitTruthyCond(exprType(right), rval);
        std::string rz = gen_->fresh();
        gen_->out_ << "  %" << rz << " = zext i1 " << rcond << " to " << I64 << "\n";
        return "%" + rz;
      }

      std::string slot = gen_->fresh("orslot");
      gen_->hoisted_allocas_ << "  %" << slot << " = alloca " << I64 << "\n";

      std::string lval = emitExpr(left);

      std::string lcond = emitTruthyCond(exprType(left), lval);

      std::string true_label = gen_->fresh("or.true");

      std::string rhs_label = gen_->fresh("or.rhs");

      std::string end_label = gen_->fresh("or.end");

      gen_->out_ << "  br i1 " << lcond << ", label %" << true_label << ", label %" << rhs_label << "\n";

      gen_->out_ << true_label << ":\n";

      gen_->out_ << "  store " << I64 << " 1, " << I64 << "* %" << slot << "\n";

      gen_->out_ << "  br label %" << end_label << "\n";

      gen_->out_ << rhs_label << ":\n";

      std::string rval = emitExpr(right);

      std::string rcond = emitTruthyCond(exprType(right), rval);

      std::string rz = gen_->fresh();

      gen_->out_ << "  %" << rz << " = zext i1 " << rcond << " to " << I64 << "\n";

      gen_->out_ << "  store " << I64 << " %" << rz << ", " << I64 << "* %" << slot << "\n";

      gen_->out_ << "  br label %" << end_label << "\n";

      gen_->out_ << end_label << ":\n";

      std::string loaded = gen_->fresh("orld");

      gen_->out_ << "  %" << loaded << " = load " << I64 << ", " << I64 << "* %" << slot << "\n";

      return "%" + loaded;

    }



    std::string emitCall(const Call& call, TypeDesc result_type) {

      if (call.name == "thread_count" || call.name == "cores") {

        if (!call.args.empty())

          throw FarError(call.name + "() takes no arguments");

        std::string tmp = gen_->fresh();

        gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_thread_count()\n";

        return "%" + tmp;

      }

      if (call.name == "join") {

        if (call.args.size() != 1)

          throw FarError("join() takes exactly one argument");

        std::string handle = emitExpr(*call.args[0].value);

        std::string tmp = gen_->fresh();

        gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_join(" << I64 << " " << handle << ")\n";

        return "%" + tmp;

      }

      if (const ConstructorInfo* ctor = lookupConstructor(call.name)) {
        if (static_cast<int>(call.args.size()) != ctor->nargs)
          throw FarError(std::string(ctor->name) + "() expects " + std::to_string(ctor->nargs) + " argument(s)");
        std::vector<Expr*> ctor_args;
        for (const auto& a : call.args)
          ctor_args.push_back(a.value.get());
        return emitAggregateConstruct(aggCtx(), ctor->ret, ctor_args);
      }
      if (call.name == "reflect_name") {
        if (call.args.size() != 1)
          throw FarError("reflect_name() expects 1 argument");
        const Expr& arg = *call.args[0].value;
        std::string type_name;
        if (arg.kind == Expr::TypeUnaryExprK && arg.type_unary.op == "typeof" && arg.type_unary.has_type &&
            isUserDesc(arg.type_unary.type_arg))
          type_name = arg.type_unary.type_arg.user_name;
        else if (arg.kind == Expr::Int) {
          int64_t tag = arg.int_lit.value;
          for (const auto* ut : gen_->obj_reg_.ordered) {
            if (ut->type_tag == static_cast<int>(tag)) {
              type_name = ut->name;
              break;
            }
          }
        }
        if (type_name.empty())
          return "0";
        return std::to_string(reflectNameHash(type_name));
      }
      if (call.name == "reflect_compile_value") {
        if (call.args.size() != 1)
          throw FarError("reflect_compile_value() expects 1 argument");
        return emitExpr(*call.args[0].value);
      }
      if (call.name == "reflect_kind" || call.name == "reflect_fields") {
        const Expr& arg = *call.args[0].value;
        std::string tag;
        if (arg.kind == Expr::TypeUnaryExprK &&
            (arg.type_unary.op == "type_tag" ||
             (arg.type_unary.op == "typeof" && arg.type_unary.has_type))) {
          tag = compileTimeTypeTagFromUnary(arg.type_unary);
        } else {
          tag = emitExpr(arg);
        }
        std::string tmp = gen_->fresh("refl");
        if (call.name == "reflect_kind")
          gen_->out_ << "  %" << tmp << " = call i64 @far_reflect_kind(i64 " << tag << ")\n";
        else
          gen_->out_ << "  %" << tmp << " = call i64 @far_reflect_fields(i64 " << tag << ")\n";
        return "%" + tmp;
      }
      if (call.name == "alloc") {
        std::string sz = emitExpr(*call.args[0].value);
        std::string tmp = gen_->fresh("heap");
        gen_->out_ << "  %" << tmp << " = call i64 @far_malloc(i64 " << sz << ")\n";
        return "%" + tmp;
      }
      if (call.name == "free") {
        std::string p = emitExpr(*call.args[0].value);
        gen_->out_ << "  call void @far_free(i64 " << p << ")\n";
        return "0";
      }
      if (call.name == "realloc") {
        std::string p = emitExpr(*call.args[0].value);
        std::string sz = emitExpr(*call.args[1].value);
        std::string tmp = gen_->fresh("heap");
        gen_->out_ << "  %" << tmp << " = call i64 @far_realloc(i64 " << p << ", i64 " << sz << ")\n";
        return "%" + tmp;
      }
      if (call.name == "borrow") {
        if (call.args[0].value->kind == Expr::Variable) {
          auto it = env_.find(call.args[0].value->var.name);
          if (it != env_.end())
            return emitAddressOf(memCtx(), it->second.slot, slotLlvmTypeDesc(it->second.type));
        }
        return emitExpr(*call.args[0].value);
      }
      if (call.name == "move") {
        TypeDesc arg_ty = exprType(*call.args[0].value);
        std::string val = emitExpr(*call.args[0].value);
        if (isBoxDesc(arg_ty)) {
          std::string tmp = gen_->fresh("mv");
          gen_->out_ << "  %" << tmp << " = call i64 @far_box_move(i64 " << val << ")\n";
          return "%" + tmp;
        }
        return val;
      }
      if (call.name == "drop") {
        TypeDesc arg_ty = exprType(*call.args[0].value);
        std::string val = emitExpr(*call.args[0].value);
        if (isMemoryHandleDesc(arg_ty)) {
          emitMemDrop(memCtx(), arg_ty.form, val);
          if (call.args[0].value->kind == Expr::Variable) {
            storeSlotValueNoDrop(call.args[0].value->var.name, "0");
            removeAutoDrop(call.args[0].value->var.name);
          }
        } else if (isConcurrencyHandleDesc(arg_ty)) {
          emitConcDrop(concCtx(), arg_ty.form, val);
          if (call.args[0].value->kind == Expr::Variable) {
            storeSlotValueNoDrop(call.args[0].value->var.name, "0");
            removeAutoDrop(call.args[0].value->var.name);
          }
        } else if (isOwnedHeapCollectionDesc(arg_ty)) {
          emitOwnedCollectionDrop(autodropFormFor(arg_ty), val);
          if (call.args[0].value->kind == Expr::Variable) {
            storeSlotValueNoDrop(call.args[0].value->var.name, "0");
            removeAutoDrop(call.args[0].value->var.name);
          }
        } else if (isUserDesc(arg_ty)) {
          const UserTypeDef* ut = gen_->obj_reg_.lookup(userTypeKey(arg_ty));
          if (ut && ut->kind == UserTypeKind::Union) {
            gen_->out_ << "  call void @far_union_drop(i64 " << val << ")\n";
            if (call.args[0].value->kind == Expr::Variable) {
              storeSlotValueNoDrop(call.args[0].value->var.name, "0");
              removeAutoDrop(call.args[0].value->var.name);
            }
          }
        }
        return "0";
      }
      if (const MemConstructorInfo* mc = lookupMemConstructor(call.name)) {
        TypeDesc elem = mc->has_elem_type && !call.type_args.empty()
                            ? call.type_args[0]
                            : TypeDesc::prim(FarTypeId::I8);
        std::vector<std::string> arg_vals;
        for (const auto& a : call.args)
          arg_vals.push_back(emitExpr(*a.value));
        return emitMemConstruct(memCtx(), mc->form, elem, arg_vals);
      }
      if (const ConcConstructorInfo* cc = lookupConcConstructor(call.name)) {
        TypeDesc elem = cc->has_elem_type && !call.type_args.empty()
                            ? call.type_args[0]
                            : TypeDesc::prim(FarTypeId::I8);
        std::vector<std::string> arg_vals;
        for (const auto& a : call.args)
          arg_vals.push_back(emitExpr(*a.value));
        return emitConcConstruct(concCtx(), cc->form, elem, arg_vals);
      }
      if (const ErrConstructorInfo* ec = lookupErrConstructor(call.name)) {
        std::vector<std::string> arg_vals;
        for (const auto& a : call.args)
          arg_vals.push_back(emitExpr(*a.value));
        return emitErrConstruct(errCtx(), *ec, arg_vals);
      }
      if (call.name == "panic") {
        std::string msg = emitExpr(*call.args[0].value);
        gen_->out_ << "  call void @far_panic(i64 " << msg << ")\n";
        gen_->out_ << "  unreachable\n";
        return "0";
      }
      if (call.name == "assert") {
        std::string cond = emitBranchCond(*call.args[0].value);
        std::string ok = gen_->fresh("assert.ok");
        std::string fail = gen_->fresh("assert.fail");
        gen_->out_ << "  br i1 " << cond << ", label %" << ok << ", label %" << fail << "\n";
        gen_->out_ << fail << ":\n";
        std::string msg = call.args.size() > 1 ? emitExpr(*call.args[1].value) : "0";
        gen_->out_ << "  call void @far_assert(i64 0, i64 " << msg << ")\n";
        gen_->out_ << "  unreachable\n";
        gen_->out_ << ok << ":\n";
        return "0";
      }
      if (call.name == "stack_trace") {
        gen_->out_ << "  call void @far_stack_trace()\n";
        return "0";
      }
      if (lookupCollConstructor(call.name)) {
        if (call.name == "Range") {
          std::string tmp = gen_->fresh("coll");
          std::string a = call.args.size() > 0 ? emitExpr(*call.args[0].value) : "0";
          std::string b = call.args.size() > 1 ? emitExpr(*call.args[1].value) : "0";
          std::string c = call.args.size() > 2 ? emitExpr(*call.args[2].value) : "1";
          gen_->out_ << "  %" << tmp << " = call i64 @far_range_new(i64 " << a << ", i64 " << b << ", i64 " << c
                     << ")\n";
          return "%" + tmp;
        }
        return emitCollConstructor(collCtx(), call.name, result_type);
      }

      if (call.is_hof_call) {
        std::string fp = loadVar(call.name);
        if (call.bound_exprs.size() != 1)
          throw FarError("higher-order call expects one argument");
        std::string arg = emitExpr(*call.bound_exprs[0]);
        std::string tmp = gen_->fresh("hof");
        gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_closure_call(" << I64 << " " << fp << ", "
                   << I64 << " " << arg << ")\n";
        return "%" + tmp;
      }

      if (call.resolved) {
        const Function* fn = call.resolved;
        std::ostringstream args;
        bool first = true;
        for (size_t i = 0; i < fn->params.size(); ++i) {
          if (fn->params[i].is_variadic) {
            std::string arr = gen_->fresh("varr");
            int64_t n = static_cast<int64_t>(call.variadic_exprs.size());
            gen_->out_ << "  %" << arr << " = call i64 @far_tarray_new(i64 " << n << ", i16 0, i64 8)\n";
            for (int64_t vi = 0; vi < n; ++vi) {
              std::string val = emitExpr(*call.variadic_exprs[static_cast<size_t>(vi)]);
              gen_->out_ << "  call void @far_tarray_set(i64 %" << arr << ", i64 " << vi << ", i64 " << val
                         << ")\n";
            }
            if (!first)
              args << ", ";
            first = false;
            args << llvmAbiTypeDesc(fn->params[i].type) << " %" << arr;
            continue;
          }
          if (!first)
            args << ", ";
          first = false;
          Expr* arg = nullptr;
          if (i < call.bound_exprs.size())
            arg = call.bound_exprs[i];
          if (!arg && i < call.args.size())
            arg = call.args[i].value.get();
          if (!arg)
            throw FarError("internal: missing bound argument for '" + call.name + "'");
          std::string val = emitExpr(*arg);
          std::string abi = gen_->internalToAbiDesc(arg->type, fn->params[i].type, val);
          args << llvmAbiTypeDesc(fn->params[i].type) << " " << abi;
        }
        const char* ret_ty = llvmAbiTypeDesc(fn->return_type);
        if (fn->is_async) {
          std::string fn_ptr = fnPointerBitcastLlvm(call.resolved_llvm_name, fn);
          std::string tmp = gen_->fresh("async");
          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_spawn(i8* " << fn_ptr << ", " << I64
                     << " 0, " << I64 << " 0, " << I64 << " 0, " << I64 << " 0, " << I64 << " 0)\n";
          return "%" + tmp;
        }
        if (fn->is_generator || fn->is_coroutine) {
          std::string fn_ptr = fnPointerBitcastLlvm(call.resolved_llvm_name, fn);
          std::string tmp = gen_->fresh("gen");
          gen_->out_ << "  %" << tmp << " = ptrtoint i8* " << fn_ptr << " to " << I64 << "\n";
          return "%" + tmp;
        }
        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << ret_ty << " @" << call.resolved_llvm_name << "("
                   << args.str() << ")\n";
        if (isAggregateDesc(fn->return_type))
          return "%" + tmp;
        return gen_->abiParamToInternalDesc(fn->return_type, "%" + tmp);
      }

      if (const IoFn* iofn = resolveIoCall(call.name, static_cast<int>(call.args.size()))) {
        std::ostringstream call_args;
        for (int i = 0; i < iofn->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          IoTy at = iofn->args[i];
          std::string val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          if (at == IoTy::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          else if (at == IoTy::Str) {
            std::string ptr = gen_->fresh("sp");
            gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";
            val = "%" + ptr;
          }
          call_args << ioArgLlvm(at) << " " << val;
        }

        if (iofn->ret == IoTy::Void) {
          gen_->out_ << "  call void @" << iofn->rt_name << "(" << call_args.str() << ")\n";
          return "0";
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << ioRetLlvm(iofn->ret) << " @" << iofn->rt_name << "("
                  << call_args.str() << ")\n";
        if (iofn->ret == IoTy::Str) {
          std::string slot = gen_->fresh("ss");
          gen_->out_ << "  %" << slot << " = ptrtoint i8* %" << tmp << " to " << I64 << "\n";
          return "%" + slot;
        }
        return "%" + tmp;
      }

      if (const PerfFn* perffn = lookupPerf(call.name)) {
        if (static_cast<int>(call.args.size()) != perffn->nargs)
          throw FarError(perffn->name + std::string("() expects ") + std::to_string(perffn->nargs) + " argument(s)");

        std::ostringstream call_args;
        for (int i = 0; i < perffn->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          PerfTy at = perffn->args[i];
          std::string val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          if (at == PerfTy::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          else if (at == PerfTy::Str) {
            std::string ptr = gen_->fresh("sp");
            gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";
            val = "%" + ptr;
          }
          call_args << perfArgLlvm(at) << " " << val;
        }

        if (perffn->ret == PerfTy::Void) {
          gen_->out_ << "  call void @" << perffn->rt_name << "(" << call_args.str() << ")\n";
          return "0";
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << perfRetLlvm(perffn->ret) << " @" << perffn->rt_name << "("
                  << call_args.str() << ")\n";
        if (perffn->ret == PerfTy::Str) {
          std::string slot = gen_->fresh("ss");
          gen_->out_ << "  %" << slot << " = ptrtoint i8* %" << tmp << " to " << I64 << "\n";
          return "%" + slot;
        }
        return "%" + tmp;
      }

      if (const SecFn* secfn = lookupSec(call.name)) {
        if (static_cast<int>(call.args.size()) != secfn->nargs)
          throw FarError(secfn->name + std::string("() expects ") + std::to_string(secfn->nargs) + " argument(s)");

        std::ostringstream call_args;
        for (int i = 0; i < secfn->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          SecTy at = secfn->args[i];
          std::string val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          if (at == SecTy::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          else if (at == SecTy::Str) {
            std::string ptr = gen_->fresh("sp");
            gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";
            val = "%" + ptr;
          }
          call_args << secArgLlvm(at) << " " << val;
        }

        if (secfn->ret == SecTy::Void) {
          gen_->out_ << "  call void @" << secfn->rt_name << "(" << call_args.str() << ")\n";
          return "0";
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << secRetLlvm(secfn->ret) << " @" << secfn->rt_name << "("
                  << call_args.str() << ")\n";
        if (secfn->ret == SecTy::Str) {
          std::string slot = gen_->fresh("ss");
          gen_->out_ << "  %" << slot << " = ptrtoint i8* %" << tmp << " to " << I64 << "\n";
          return "%" + slot;
        }
        return "%" + tmp;
      }

      if (const ModernFn* modfn = lookupModern(call.name)) {
        if (static_cast<int>(call.args.size()) != modfn->nargs)
          throw FarError(modfn->name + std::string("() expects ") + std::to_string(modfn->nargs) + " argument(s)");

        std::ostringstream call_args;
        for (int i = 0; i < modfn->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          ModernTy at = modfn->args[i];
          std::string val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          if (at == ModernTy::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          else if (at == ModernTy::Str) {
            std::string ptr = gen_->fresh("sp");
            gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";
            val = "%" + ptr;
          }
          call_args << modernArgLlvm(at) << " " << val;
        }

        if (modfn->ret == ModernTy::Void) {
          gen_->out_ << "  call void @" << modfn->rt_name << "(" << call_args.str() << ")\n";
          return "0";
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << modernRetLlvm(modfn->ret) << " @" << modfn->rt_name << "("
                  << call_args.str() << ")\n";
        if (modfn->ret == ModernTy::Str) {
          std::string slot = gen_->fresh("ss");
          gen_->out_ << "  %" << slot << " = ptrtoint i8* %" << tmp << " to " << I64 << "\n";
          return "%" + slot;
        }
        return "%" + tmp;
      }

      if (const NetFn* netfn = lookupNet(call.name)) {
        if (static_cast<int>(call.args.size()) != netfn->nargs)
          throw FarError(netfn->name + std::string("() expects ") + std::to_string(netfn->nargs) + " argument(s)");

        std::ostringstream call_args;
        for (int i = 0; i < netfn->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          NetTy at = netfn->args[i];
          std::string val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          if (at == NetTy::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          else if (at == NetTy::Str) {
            std::string ptr = gen_->fresh("sp");
            gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";
            val = "%" + ptr;
          }
          call_args << netArgLlvm(at) << " " << val;
        }

        if (netfn->ret == NetTy::Void) {
          gen_->out_ << "  call void @" << netfn->rt_name << "(" << call_args.str() << ")\n";
          return "0";
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << netRetLlvm(netfn->ret) << " @" << netfn->rt_name << "("
                  << call_args.str() << ")\n";
        if (netfn->ret == NetTy::Str) {
          std::string slot = gen_->fresh("ss");
          gen_->out_ << "  %" << slot << " = ptrtoint i8* %" << tmp << " to " << I64 << "\n";
          return "%" + slot;
        }
        return "%" + tmp;
      }

      if (const ScienceFn* scifn = lookupScience(call.name)) {
        if (static_cast<int>(call.args.size()) != scifn->nargs)
          throw FarError(scifn->name + std::string("() expects ") + std::to_string(scifn->nargs) + " argument(s)");

        std::ostringstream call_args;
        for (int i = 0; i < scifn->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          ScienceTy at = scifn->args[i];
          std::string val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          if (at == ScienceTy::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          call_args << scienceArgLlvm(at) << " " << val;
        }

        if (scifn->ret == ScienceTy::Void) {
          gen_->out_ << "  call void @" << scifn->rt_name << "(" << call_args.str() << ")\n";
          return "0";
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << scienceRetLlvm(scifn->ret) << " @" << scifn->rt_name << "("
                  << call_args.str() << ")\n";
        return "%" + tmp;
      }

      if (const StdlibFn* stdfn = lookupStdlib(call.name)) {
        if (static_cast<int>(call.args.size()) != stdfn->nargs)
          throw FarError(stdfn->name + std::string("() expects ") + std::to_string(stdfn->nargs) + " argument(s)");

        std::ostringstream call_args;
        for (int i = 0; i < stdfn->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          StdlibTy at = stdfn->args[i];
          std::string val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          if (at == StdlibTy::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          else if (at == StdlibTy::Str) {
            std::string ptr = gen_->fresh("sp");
            gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";
            val = "%" + ptr;
          }
          call_args << stdlibArgLlvm(at) << " " << val;
        }

        if (stdfn->ret == StdlibTy::Void) {
          gen_->out_ << "  call void @" << stdfn->rt_name << "(" << call_args.str() << ")\n";
          return "0";
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << stdlibRetLlvm(stdfn->ret) << " @" << stdfn->rt_name << "("
                  << call_args.str() << ")\n";
        if (stdfn->ret == StdlibTy::Str) {
          std::string slot = gen_->fresh("ss");
          gen_->out_ << "  %" << slot << " = ptrtoint i8* %" << tmp << " to " << I64 << "\n";
          return "%" + slot;
        }
        return "%" + tmp;
      }

      if (const BuiltinInfo* builtin = lookupBuiltin(call.name)) {
        if (static_cast<int>(call.args.size()) != builtin->nargs)
          throw FarError(builtin->name + std::string("() expects ") + std::to_string(builtin->nargs) + " argument(s)");

        AggCodegenCtx agg = aggCtx();
        std::ostringstream call_args;
        for (int i = 0; i < builtin->nargs; ++i) {
          if (i > 0)
            call_args << ", ";
          FarTypeId at = builtin->args[i];
          std::string val;
          if (at == FarTypeId::F64)
            val = emitExprAsDouble(*call.args[static_cast<size_t>(i)].value);
          else if (isAggregateType(at))
            val = aggValueToPtr(agg, emitExpr(*call.args[static_cast<size_t>(i)].value), at);
          else
            val = emitExpr(*call.args[static_cast<size_t>(i)].value);
          call_args << builtinArgLlvm(at) << " " << val;
        }

        if (isAggregateType(builtin->ret)) {
          std::string out_slot = gen_->fresh("bout");
          gen_->out_ << "  %" << out_slot << " = alloca " << aggLlvmType(builtin->ret) << "\n";
          gen_->out_ << "  call void @" << builtin->rt_name << "(" << call_args.str();
          if (builtin->nargs > 0)
            gen_->out_ << ", ";
          gen_->out_ << builtinArgLlvm(builtin->ret) << " %" << out_slot << ")\n";
          return loadAggValue(agg, "%" + out_slot, builtin->ret);
        }

        std::string tmp = gen_->fresh();
        gen_->out_ << "  %" << tmp << " = call " << builtinRetLlvm(builtin) << " @" << builtin->rt_name << "("
                  << call_args.str() << ")\n";
        // Runtime builtins use i64/double; matches internal slot representation.
        return "%" + tmp;
      }

      if (const UserTypeDef* ut = gen_->obj_reg_.lookup(call.name)) {
        if (ut->kind == UserTypeKind::Enum || ut->kind == UserTypeKind::FlagsEnum ||
            ut->kind == UserTypeKind::Union || ut->kind == UserTypeKind::Interface ||
            ut->kind == UserTypeKind::Trait)
          throw FarError("invalid construction of " + call.name);
        std::vector<std::string> arg_vals;
        for (const auto& a : call.args)
          arg_vals.push_back(emitExpr(*a.value));
        if (ut->kind == UserTypeKind::Actor) {
          std::string init = arg_vals.empty() ? "0" : arg_vals[0];
          return emitActorConstruct(concCtx(), userMangleMethod(ut->name, "on_msg"), init);
        }
        if (!ut->type_params.empty()) {
          const UserTypeDef* mono =
              resolveUserType(result_type, gen_->obj_reg_, const_cast<Program&>(gen_->program_));
          if (!mono)
            throw FarError("unknown generic instance of " + call.name);
          if (call.resolved_ctor)
            return emitUserConstructCall(userCtx(), *mono, *call.resolved_ctor, arg_vals);
          return emitUserConstruct(userCtx(), *mono, arg_vals);
        }
        if (call.resolved_ctor)
          return emitUserConstructCall(userCtx(), *ut, *call.resolved_ctor, arg_vals);
        return emitUserConstruct(userCtx(), *ut, arg_vals);
      }

      if (call.name == "len") {

        if (call.args.size() != 1)

          throw FarError("len() takes exactly one argument");

        const Expr& arg = *call.args[0].value;

        TypeDesc ty = exprType(arg);

        std::string val = emitExpr(arg);

        std::string tmp = gen_->fresh();

        if (isPrimTy(ty, FarTypeId::String)) {

          std::string ptr = gen_->fresh("sp");

          gen_->out_ << "  %" << ptr << " = inttoptr " << I64 << " " << val << " to i8*\n";

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_str_len(i8* %" << ptr << ")\n";

        } else if (ty.form == TypeForm::Array || ty.form == TypeForm::Slice || isPrimTy(ty, FarTypeId::Arr)) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_tarray_len(" << I64 << " " << val << ")\n";

        } else if (ty.form == TypeForm::List) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_list_len(" << I64 << " " << val << ")\n";

        } else if (ty.form == TypeForm::Dict) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_dict_len(" << I64 << " " << val << ")\n";

        } else if (ty.form == TypeForm::Set) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_set_len(" << I64 << " " << val << ")\n";

        } else if (ty.form == TypeForm::Queue) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_queue_len(" << I64 << " " << val << ")\n";

        } else if (ty.form == TypeForm::Stack) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_stack_len(" << I64 << " " << val << ")\n";

        } else if (ty.form == TypeForm::LinkedList) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_llist_len(" << I64 << " " << val << ")\n";

        } else if (ty.form == TypeForm::Range) {

          gen_->out_ << "  %" << tmp << " = call " << I64 << " @far_range_len(" << I64 << " " << val << ")\n";

        } else {

          throw FarError("len() requires string or collection");

        }

        return "%" + tmp;

      }

      throw FarError("undefined function '" + call.name + "'");

    }

  };

};



std::string generateIR(const Program& program, const FarTarget& target) {
  return LLVMCodegen(program, target).emit();
}



}  // namespace far

