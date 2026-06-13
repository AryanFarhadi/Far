#pragma once

#include "ast.h"
#include "type_desc.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace far {

struct ObjectRegistry {
  Program* program = nullptr;
  std::unordered_map<std::string, UserTypeDef*> by_name;
  std::vector<UserTypeDef*> ordered;
  std::unordered_map<std::string, std::vector<const UserMethod*>> extensions;
  int next_type_tag = 0x8000;

  void build(Program& program, bool materialize_methods = true);
  const UserTypeDef* lookup(const std::string& name) const;
  UserTypeDef* lookupMut(const std::string& name);
  bool isUserType(const std::string& name) const;
  const UserMethod* lookupMethod(const TypeDesc& ty, const std::string& name) const;
  const UserMethod* lookupExtension(const std::string& type_name, const std::string& method) const;
  const PropertyDef* lookupProperty(const TypeDesc& ty, const std::string& name) const;
  const IndexerDef* lookupIndexer(const TypeDesc& ty) const;
  const OperatorDef* lookupOperator(const TypeDesc& ty, const std::string& op) const;
  int lookupFieldIndex(const TypeDesc& ty, const std::string& field) const;
  int enumVariantValue(const std::string& type_name, const std::string& variant) const;
  bool hasAttribute(const UserTypeDef& td, const std::string& attr) const;
  void validateImplements(const UserTypeDef& td) const;
  void applyMixins(UserTypeDef& td, Program& program);
  void lowerSugar(UserTypeDef& td, Program& program);
};

bool isUserValueType(const UserTypeDef& td);
bool isUserRefType(const UserTypeDef& td);
std::string userMangleMethod(const std::string& type_name, const std::string& method);

Visibility defaultMemberVisibility(UserTypeKind kind);
size_t userMethodCallArgCount(const UserMethod& method);
void prepareUserTypeMethods(UserTypeDef& td);
const UserField* lookupUserField(const UserTypeDef& td, const std::string& name);
std::string userTypeBaseName(const std::string& type_name);
bool userTypeHasConstructor(const UserTypeDef& td);
const UserMethod* lookupUserConstructor(const UserTypeDef& td, size_t nargs);
std::string userOpMethodName(const std::string& op);
const char* userKindName(UserTypeKind k);

}  // namespace far
