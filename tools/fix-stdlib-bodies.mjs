#!/usr/bin/env node
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const cppPath = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'src', 'far_stdlib_modules.cpp');
let cpp = fs.readFileSync(cppPath, 'utf8');

const fixes = [
  [
    `public class Bench {
  public static fun start() -> i64 { return bench_now() }
  public static fun elapsed_ms(start_ticks: i64) -> i64 { return bench_elapsed_ms(start_ticks) }
  public static fun measure(work: i64) -> i64 {

}`,
    `public class Bench {
  public static fun start() -> i64 { return bench_now() }
  public static fun elapsed_ms(start_ticks: i64) -> i64 { return bench_elapsed_ms(start_ticks) }
  public static fun measure(work: i64) -> i64 {
    t0 = bench_now()
    x = work
    return bench_elapsed_ms(t0)
  }
}`,
  ],
  [
    `public class Cli {
  public static fun argc() -> i64 { return args_count() }
  public static fun argv(i: i64) -> string { return args_get(i) }
  public static fun has_flag(flag: string) -> i64 {
  public static fun flag_value(flag: string) -> string {

}`,
    `public class Cli {
  public static fun argc() -> i64 { return args_count() }
  public static fun argv(i: i64) -> string { return args_get(i) }
  public static fun has_flag(flag: string) -> i64 {
    i = 0
    while (i < args_count()) {
      if (args_get(i) == flag) { return 1 }
      i = i + 1
    }
    return 0
  }
  public static fun flag_value(flag: string) -> string {
    i = 0
    while (i < args_count()) {
      if (args_get(i) == flag && i + 1 < args_count()) {
        return args_get(i + 1)
      }
      i = i + 1
    }
    return ""
  }
}`,
  ],
  [
    `public class I18n {
  public static fun translate(key: string, lang: string) -> string {
  public static fun format_msg(key: string, lang: string) -> string {

}`,
    `public class I18n {
  public static fun translate(key: string, lang: string) -> string {
    return key
  }
  public static fun format_msg(key: string, lang: string) -> string {
    return key
  }
}`,
  ],
  [
    `  public static fun map_or_default(opt: i64, mapped: i64, fallback: i64) -> i64 {

}`,
    `  public static fun map_or_default(opt: i64, mapped: i64, fallback: i64) -> i64 {
    return null_map_or(opt, mapped, fallback)
  }
}`,
  ],
  [
    `public class Grpc {
  public static fun call(host: string, port: i64, method: string, payload: string) -> string {

}`,
    `public class Grpc {
  public static fun call(host: string, port: i64, method: string, payload: string) -> string {
    return grpc_call(host, port, method, payload)
  }
}`,
  ],
  [
    `  public static fun post_ct(url: string, body: string, ctype: string) -> string {
  public static fun parse_status(response: string) -> i64 { return http_parse_status(response) }`,
    `  public static fun post_ct(url: string, body: string, ctype: string) -> string {
    return http_post_ct(url, body, ctype)
  }
  public static fun parse_status(response: string) -> i64 { return http_parse_status(response) }`,
  ],
  [
    `public class Client {
  public static fun tcp_open(host: string, port: i64) -> TcpClient {

}`,
    `public class Client {
  public static fun tcp_open(host: string, port: i64) -> TcpClient {
    c = TcpClient()
    c.connect(host, port)
    return c
  }
}`,
  ],
  [
    `public class Rpc {
  public static fun call(host: string, port: i64, method: string, params: string) -> string {

}`,
    `public class Rpc {
  public static fun call(host: string, port: i64, method: string, params: string) -> string {
    return rpc_call(host, port, method, params)
  }
}`,
  ],
  [
    `  public static fun pack(service: string, method: string, body: string) -> string {
  public static fun unpack_method(packed: string) -> string { return net_unpack_method(packed) }`,
    `  public static fun pack(service: string, method: string, body: string) -> string {
    return net_pack(service, method, body)
  }
  public static fun unpack_method(packed: string) -> string { return net_unpack_method(packed) }`,
  ],
  [
    `  public static fun send_to(sock: i64, host: string, port: i64, data: string) -> i64 {
  public static fun recv_from(sock: i64, max: i64) -> string { return udp_recv(sock, max) }`,
    `  public static fun send_to(sock: i64, host: string, port: i64, data: string) -> i64 {
    return udp_send(sock, host, port, data)
  }
  public static fun recv_from(sock: i64, max: i64) -> string { return udp_recv(sock, max) }`,
  ],
  [
    `  public static fun open(host: string, port: i64, path: string) -> i64 {
  public static fun send_text(sock: i64, text: string) -> i64 { return ws_send(sock, text) }`,
    `  public static fun open(host: string, port: i64, path: string) -> i64 {
    return ws_connect(host, port, path)
  }
  public static fun send_text(sock: i64, text: string) -> i64 { return ws_send(sock, text) }`,
  ],
  [
    `public class Random {
  public static fun seed(s: i64) -> i64 {
  public static fun next_i64() -> i64 {
  public static fun next_f64() -> f64 {
  public static fun rand_between(lo: i64, hi: i64) -> i64 {

}`,
    `public class Random {
  public static fun seed(s: i64) -> i64 {
    rand_seed(s)
    return 0
  }
  public static fun next_i64() -> i64 {
    return rand_i64()
  }
  public static fun next_f64() -> f64 {
    return rand_f64()
  }
  public static fun rand_between(lo: i64, hi: i64) -> i64 {
    return rand_range(lo, hi)
  }
}`,
  ],
  [
    `  public static fun trace2(m00: f64, m01: f64, m10: f64, m11: f64) -> f64 {
  public static fun mul_vec2(m: mat2, v: vec2) -> vec2 { return m.mul(v) }`,
    `  public static fun trace2(m00: f64, m01: f64, m10: f64, m11: f64) -> f64 {
    return sci_mat2_trace(m00, m01, m10, m11)
  }
  public static fun mul_vec2(m: mat2, v: vec2) -> vec2 { return m.mul(v) }`,
  ],
  [
    `public class Optimization {
  public static fun grad_step(x: f64, lr: f64, grad: f64) -> f64 {
  public static fun parabola_vertex(a: f64, b: f64, c: f64) -> f64 {

}`,
    `public class Optimization {
  public static fun grad_step(x: f64, lr: f64, grad: f64) -> f64 {
    return sci_gradient_descent(x, lr, grad)
  }
  public static fun parabola_vertex(a: f64, b: f64, c: f64) -> f64 {
    return sci_parabola_min(a, b, c)
  }
}`,
  ],
  [
    `  public static fun dot_components(ax: f64, ay: f64, az: f64, bx: f64, by: f64, bz: f64) -> f64 {

}`,
    `  public static fun dot_components(ax: f64, ay: f64, az: f64, bx: f64, by: f64, bz: f64) -> f64 {
    return sci_v3_dot(ax, ay, az, bx, by, bz)
  }
}`,
  ],
  [
    `public class Test {
  public static fun assert_eq_i64(a: i64, b: i64) -> i64 {
  public static fun assert_true(v: i64) -> i64 {
  public static fun run_case(name: string, code: i64) -> i64 {

}`,
    `public class Test {
  public static fun assert_eq_i64(a: i64, b: i64) -> i64 {
    if (a != b) { return 1 }
    return 0
  }
  public static fun assert_true(v: i64) -> i64 {
    if (v == 0) { return 1 }
    return 0
  }
  public static fun run_case(name: string, code: i64) -> i64 {
    return code
  }
}`,
  ],
];

for (const [from, to] of fixes) {
  if (!cpp.includes(from)) {
    console.warn('Missing expected block:', from.split('\n')[0]);
    continue;
  }
  cpp = cpp.replace(from, to);
}

fs.writeFileSync(cppPath, cpp);
console.log('Fixed stdlib method bodies');
