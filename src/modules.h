#pragma once

#include "ast.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace far {

std::string moduleFullName(const Program& program);
std::string dotPathToFilePath(const std::string& dot_path);
bool packageMatches(const std::string& importer_pkg, const std::string& target_pkg);
bool canImportVisibility(Visibility vis, ImportKind kind, const std::string& importer_pkg,
                         const std::string& target_pkg);

Program resolveImports(Program program, const std::string& base_dir);

}  // namespace far
