#pragma once

#include <string>

namespace far {

struct FarTarget {
  std::string alias;
  std::string triple;
  std::string datalayout;
  std::string link_flags;
  std::string runtime_cflags;
  std::string exe_suffix;
  std::string object_cache_name;
};

FarTarget hostTarget();
FarTarget parseTargetAlias(const std::string& alias);
FarTarget resolveTarget(const std::string& alias_or_empty);

}  // namespace far
