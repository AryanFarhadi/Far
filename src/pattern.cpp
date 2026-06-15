#include "pattern.h"

namespace far {

int unionMaxFields(const UserTypeDef& td) {
  int max_f = 0;
  for (const auto& v : td.variants) {
    int n = static_cast<int>(v.fields.size());
    if (n > max_f)
      max_f = n;
  }
  return max_f;
}

const EnumVariant* lookupVariant(const UserTypeDef& td, const std::string& name) {
  for (const auto& v : td.variants) {
    if (v.name == name)
      return &v;
  }
  return nullptr;
}

void declarePatternRuntime(std::ostringstream& out) {
  out << "declare i64 @far_union_new(i64, i64, i64, i64, i64, i64, i64, i64, i64)\n";
  out << "declare void @far_union_drop(i64)\n";
  out << "declare i64 @far_union_tag(i64)\n";
  out << "declare i64 @far_union_field(i64, i64)\n";
}

}  // namespace far
