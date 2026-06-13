#include "target.h"

#include "error.h"

namespace far {

namespace {

FarTarget makeTarget(const char* alias, const char* triple, const char* datalayout, const char* link_flags,
                     const char* runtime_cflags, const char* exe_suffix, const char* object_cache_name) {
  FarTarget t;
  t.alias = alias;
  t.triple = triple;
  t.datalayout = datalayout;
  t.link_flags = link_flags;
  t.runtime_cflags = runtime_cflags;
  t.exe_suffix = exe_suffix;
  t.object_cache_name = object_cache_name;
  return t;
}

FarTarget windowsX64() {
  return makeTarget("windows-x64", "x86_64-pc-windows-msvc",
                    "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128",
                    "", "", ".exe", "far_rt.windows-x64.o");
}

FarTarget linuxX64() {
  return makeTarget("linux-x64", "x86_64-unknown-linux-gnu",
                    "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f128:128-f64:64-f80:128-n8:16:32:64-S128",
                    "-lpthread -lm -ldl", "-fPIC", "", "far_rt.linux-x64.o");
}

FarTarget linuxArm64() {
  return makeTarget("linux-arm64", "aarch64-unknown-linux-gnu",
                    "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f128:128-f64:64-n8:16:32:64-S128",
                    "-lpthread -lm -ldl", "-fPIC", "", "far_rt.linux-arm64.o");
}

}  // namespace

FarTarget hostTarget() {
#if defined(_WIN32)
  return windowsX64();
#elif defined(__aarch64__)
  return linuxArm64();
#else
  return linuxX64();
#endif
}

FarTarget parseTargetAlias(const std::string& alias) {
  if (alias == "windows-x64" || alias == "win64" || alias == "windows")
    return windowsX64();
  if (alias == "linux-x64" || alias == "linux" || alias == "linux64")
    return linuxX64();
  if (alias == "linux-arm64" || alias == "arm64" || alias == "aarch64")
    return linuxArm64();
  throw FarError("unknown target '" + alias + "'; use windows-x64, linux-x64, or linux-arm64");
}

FarTarget resolveTarget(const std::string& alias_or_empty) {
  if (alias_or_empty.empty())
    return hostTarget();
  return parseTargetAlias(alias_or_empty);
}

}  // namespace far
