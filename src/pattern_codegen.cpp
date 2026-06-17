#include "pattern_codegen.h"

#include "error.h"
#include "generics.h"
#include "object_codegen.h"
#include "pattern.h"
#include "type_desc.h"
#include "types.h"

#include <cmath>
#include <sstream>

namespace far {

static std::string emitIcmpEq(PatCodegenCtx ctx, const std::string& a, const std::string& b) {
  std::string tmp = ctx.fresh("pcmp");
  ctx.out << "  %" << tmp << " = icmp eq i64 " << a << ", " << b << "\n";
  return "%" + tmp;
}

static std::string patFormatDouble(double v) {
  std::ostringstream os;
  os.precision(17);
  os << v;
  std::string s = os.str();
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
      s.find('E') == std::string::npos)
    s += ".0";
  return s;
}

PatTestResult emitPatternTest(PatCodegenCtx ctx, const Pattern& pat, const std::string& scrut_val,
                              const TypeDesc& scrut_ty) {
  PatTestResult r;
  switch (pat.kind) {
    case PatKind::Wildcard:
      r.cond = "1";
      r.always = true;
      return r;
    case PatKind::Bind:
      r.cond = "1";
      r.always = true;
      r.binds.push_back({pat.bind_name, scrut_val, scrut_ty});
      return r;
    case PatKind::Literal:
      if (pat.literal_is_float ||
          (isPrimitiveDesc(scrut_ty) &&
           (scrut_ty.primitive == FarTypeId::F32 || scrut_ty.primitive == FarTypeId::F64))) {
        std::string cmp_val = scrut_val;
        if (isPrimitiveDesc(scrut_ty) && scrut_ty.primitive == FarTypeId::F32) {
          std::string ext = ctx.fresh("f32pe");
          ctx.out << "  %" << ext << " = fpext float " << scrut_val << " to double\n";
          cmp_val = "%" + ext;
        }
        std::string tmp = ctx.fresh("pcmp");
        double lit =
            pat.literal_is_float ? pat.float_literal : static_cast<double>(pat.literal);
        if (std::isnan(lit)) {
          ctx.out << "  %" << tmp << " = fcmp uno double " << cmp_val << ", "
                  << patFormatDouble(lit) << "\n";
        } else {
          ctx.out << "  %" << tmp << " = fcmp oeq double " << cmp_val << ", "
                  << patFormatDouble(lit) << "\n";
        }
        r.cond = "%" + tmp;
      } else {
        r.cond = emitIcmpEq(ctx, scrut_val, std::to_string(pat.literal));
      }
      return r;
    case PatKind::EnumVariant:
      r.cond = emitIcmpEq(ctx, scrut_val, std::to_string(pat.variant_value));
      return r;
    case PatKind::UnionVariant: {
      std::string tag_tmp = ctx.fresh("utag");
      ctx.out << "  %" << tag_tmp << " = call i64 @far_union_tag(i64 " << scrut_val << ")\n";
      r.cond = emitIcmpEq(ctx, "%" + tag_tmp, std::to_string(pat.variant_value));
      const UserTypeDef* td = ctx.obj_reg->lookup(pat.type_name);
      if (!td || td->kind != UserTypeKind::Union)
        throw FarError("unknown union type '" + pat.type_name + "'");
      const EnumVariant* uv = lookupVariant(*td, pat.variant);
      if (!uv)
        throw FarError("unknown union variant");
      for (size_t i = 0; i < pat.fields.size() && i < uv->fields.size(); ++i) {
        std::string fld_tmp = ctx.fresh("ufld");
        ctx.out << "  %" << fld_tmp << " = call i64 @far_union_field(i64 " << scrut_val << ", i64 " << i
                << ")\n";
        PatTestResult sub =
            emitPatternTest(ctx, *pat.fields[i], "%" + fld_tmp, uv->fields[i].type);
        if (!sub.always) {
          std::string merged = ctx.fresh("pand");
          ctx.out << "  %" << merged << " = and i1 " << r.cond << ", " << sub.cond << "\n";
          r.cond = "%" + merged;
        }
        for (const auto& b : sub.binds)
          r.binds.push_back(b);
      }
      return r;
    }
    case PatKind::TypeTest: {
      if (!ctx.obj_reg)
        throw FarError("internal error: missing object registry for type pattern");
      const UserTypeDef* td = ctx.obj_reg->lookup(pat.type_name);
      if (!td)
        throw FarError("unknown type '" + pat.type_name + "'");
      if (isUserDesc(scrut_ty) && typeDescEquals(scrut_ty, pat.type_test)) {
        r.cond = "1";
        r.always = true;
      } else {
        std::string false_tmp = ctx.fresh("pfalse");
        ctx.out << "  %" << false_tmp << " = icmp eq i1 0, 1\n";
        r.cond = "%" + false_tmp;
      }
      return r;
    }
    case PatKind::StructDestructure: {
      TypeDesc obj_ty = isUserDesc(scrut_ty) ? scrut_ty : TypeDesc::user(pat.type_name);
      const UserTypeDef* td = ctx.obj_reg->lookup(userTypeKey(obj_ty));
      if (!td)
        throw FarError("unknown type '" + pat.type_name + "'");
      if (isUserDesc(scrut_ty) && userTypeKey(scrut_ty) != pat.type_name &&
          td->name != pat.type_name && td->mangled_name != pat.type_name)
        throw FarError("pattern type mismatch");
      r.cond = "1";
      r.always = true;
      for (size_t i = 0; i < pat.fields.size(); ++i) {
        int fidx = static_cast<int>(i);
        if (!pat.field_names.empty()) {
          fidx = ctx.obj_reg->lookupFieldIndex(obj_ty, pat.field_names[i]);
          if (fidx < 0)
            throw FarError("unknown field '" + pat.field_names[i] + "'");
        }
        MemberAccess mem;
        mem.member = td->fields[static_cast<size_t>(fidx)].name;
        ObjCodegenCtx octx{ctx.out, ctx.fresh};
        std::string fld_val = emitUserMember(octx, *ctx.obj_reg, mem, obj_ty, scrut_val);
        PatTestResult sub = emitPatternTest(ctx, *pat.fields[i], fld_val,
                                            td->fields[static_cast<size_t>(fidx)].type);
        if (!sub.always) {
          std::string merged = ctx.fresh("pand");
          ctx.out << "  %" << merged << " = and i1 " << r.cond << ", " << sub.cond << "\n";
          r.cond = "%" + merged;
        }
        for (const auto& b : sub.binds)
          r.binds.push_back(b);
      }
      return r;
    }
    case PatKind::TupleDestructure: {
      if (scrut_ty.form != TypeForm::Tuple)
        throw FarError("tuple pattern requires tuple scrutinee");
      r.cond = "1";
      r.always = true;
      for (size_t i = 0; i < pat.fields.size(); ++i) {
        std::string fld_tmp = ctx.fresh("tupf");
        ctx.out << "  %" << fld_tmp << " = call i64 @far_tarray_get(i64 " << scrut_val << ", i64 " << i
                << ")\n";
        TypeDesc ft = i < scrut_ty.args.size() ? scrut_ty.args[i] : TypeDesc::prim(FarTypeId::I64);
        PatTestResult sub = emitPatternTest(ctx, *pat.fields[i], "%" + fld_tmp, ft);
        if (!sub.always) {
          std::string merged = ctx.fresh("pand");
          ctx.out << "  %" << merged << " = and i1 " << r.cond << ", " << sub.cond << "\n";
          r.cond = "%" + merged;
        }
        for (const auto& b : sub.binds)
          r.binds.push_back(b);
      }
      return r;
    }
  }
  throw FarError("unsupported pattern kind");
}

std::string emitUnionConstruct(PatCodegenCtx ctx, const UnionVariantExpr& uv,
                               const std::vector<std::string>& arg_vals) {
  if (arg_vals.size() > 8)
    throw FarError("union variants with more than 8 fields are not supported");
  std::vector<std::string> fields(8, "0");
  for (size_t i = 0; i < arg_vals.size() && i < fields.size(); ++i)
    fields[i] = arg_vals[i];
  std::string tmp = ctx.fresh("union");
  ctx.out << "  %" << tmp << " = call i64 @far_union_new(i64 " << uv.value << ", i64 " << fields[0]
          << ", i64 " << fields[1] << ", i64 " << fields[2] << ", i64 " << fields[3] << ", i64 " << fields[4]
          << ", i64 " << fields[5] << ", i64 " << fields[6] << ", i64 " << fields[7] << ")\n";
  return "%" + tmp;
}

}  // namespace far
