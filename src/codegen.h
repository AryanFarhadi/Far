#pragma once
#include "ast.h"
#include "target.h"
#include <string>

namespace far {

std::string generateIR(const Program& program, const FarTarget& target = hostTarget());

}  // namespace far
