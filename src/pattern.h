#pragma once

#include "ast.h"
#include "object_model.h"

#include <sstream>
#include <string>

namespace far {

int unionMaxFields(const UserTypeDef& td);
const EnumVariant* lookupVariant(const UserTypeDef& td, const std::string& name);

void declarePatternRuntime(std::ostringstream& out);

}  // namespace far
