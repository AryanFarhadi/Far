#include "far_net.h"

#include "error.h"
#include "type_desc.h"

#include <unordered_map>

namespace far {

namespace {

#define S NetTy::Str
#define I NetTy::I64
#define D NetTy::F64
#define V NetTy::Void

#define N0(n, rt, r) \
  { n, rt, r, 0, {} }
#define N1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }
#define N2(n, rt, r, a0, a1) \
  { n, rt, r, 2, { a0, a1 } }
#define N3(n, rt, r, a0, a1, a2) \
  { n, rt, r, 3, { a0, a1, a2 } }
#define N4(n, rt, r, a0, a1, a2, a3) \
  { n, rt, r, 4, { a0, a1, a2, a3 } }

static const NetFn kNet[] = {
    // TCP
    N2("tcp_connect", "far_tcp_connect", I, S, I),
    N1("tcp_listen", "far_tcp_listen", I, I),
    N1("tcp_accept", "far_tcp_accept", I, I),
    N2("tcp_send", "far_tcp_send", I, I, S),
    N2("tcp_recv", "far_tcp_recv", S, I, I),
    N1("tcp_close", "far_tcp_close", I, I),

    // UDP
    N1("udp_bind", "far_udp_bind", I, I),
    N4("udp_send", "far_udp_send", I, I, S, I, S),
    N2("udp_recv", "far_udp_recv", S, I, I),
    N1("udp_close", "far_udp_close", I, I),

    // HTTP
    N1("http_get", "far_http_get", S, S),
    N2("http_post", "far_http_post", S, S, S),
    N3("http_post_ct", "far_http_post_ct", S, S, S, S),
    N1("http_parse_status", "far_http_parse_status", I, S),

    // HTTPS
    N1("https_get", "far_https_get", S, S),
    N2("https_post", "far_https_post", S, S, S),

    // HTTP or HTTPS (auto from URL)
    N1("web_get", "far_web_get", S, S),
    N2("web_post", "far_web_post", S, S, S),

    // WebSocket
    N3("ws_connect", "far_ws_connect", I, S, I, S),
    N2("ws_send", "far_ws_send", I, I, S),
    N1("ws_recv", "far_ws_recv", S, I),
    N1("ws_close", "far_ws_close", I, I),

    // RPC (JSON-RPC 2.0 over HTTP)
    N4("rpc_call", "far_rpc_call", S, S, I, S, S),

    // REST
    N1("rest_get", "far_rest_get", S, S),
    N2("rest_post", "far_rest_post", S, S, S),
    N2("rest_put", "far_rest_put", S, S, S),
    N1("rest_delete", "far_rest_delete", S, S),

    // gRPC (simplified unary framing over TCP)
    N4("grpc_call", "far_grpc_call", S, S, I, S, S),

    // Serialization
    N3("net_pack", "far_net_pack", S, S, S, S),
    N1("net_unpack_method", "far_net_unpack_method", S, S),
    N1("net_unpack_body", "far_net_unpack_body", S, S),
    N1("net_serialize", "far_net_serialize", S, S),
    N2("net_deserialize", "far_net_deserialize", S, S, S),

    // Packet compression
    N1("net_compress", "far_net_compress", S, S),
    N1("net_decompress", "far_net_decompress", S, S),
};

#undef N0
#undef N1
#undef N2
#undef N3
#undef N4
#undef S
#undef I
#undef D
#undef V

static FarTypeId netToFar(NetTy ty) {
  switch (ty) {
    case NetTy::F64:
      return FarTypeId::F64;
    case NetTy::Str:
      return FarTypeId::String;
    case NetTy::Void:
    case NetTy::I64:
    default:
      return FarTypeId::I64;
  }
}

}  // namespace

const NetFn* lookupNet(const std::string& name) {
  static const std::unordered_map<std::string, const NetFn*> map = [] {
    std::unordered_map<std::string, const NetFn*> m;
    for (const auto& f : kNet)
      m[f.name] = &f;
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

std::string netArgLlvm(NetTy ty) {
  if (ty == NetTy::F64)
    return "double";
  if (ty == NetTy::Str)
    return "i8*";
  return "i64";
}

std::string netRetLlvm(NetTy ty) {
  if (ty == NetTy::F64)
    return "double";
  if (ty == NetTy::Str)
    return "i8*";
  return "i64";
}

void declareNetRuntime(std::ostringstream& out) {
  for (const auto& f : kNet) {
    std::ostringstream sig;
    sig << (f.ret == NetTy::Void ? "void" : netRetLlvm(f.ret)) << " @" << f.rt_name << "(";
    for (int i = 0; i < f.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << netArgLlvm(f.args[i]);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

TypeDesc netRetType(const NetFn* fn) {
  if (fn->ret == NetTy::Bool)
    return TypeDesc::prim(FarTypeId::I64);
  return TypeDesc::prim(netToFar(fn->ret));
}

bool checkNetArgs(const NetFn* fn, const std::vector<CallArg>& args,
                  const std::function<TypeDesc(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != fn->nargs) {
    throw FarError(std::string(fn->name) + "() expects " + std::to_string(fn->nargs) + " argument(s)");
  }
  for (int i = 0; i < fn->nargs; ++i) {
    TypeDesc td = typecheck_expr(*args[static_cast<size_t>(i)].value);
    if (!isPrimitiveDesc(td))
      throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
    FarTypeId at = td.primitive;
    FarTypeId want = netToFar(fn->args[i]);
    if (canAssign(at, want))
      continue;
    if (want == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
  }
  return true;
}

}  // namespace far
