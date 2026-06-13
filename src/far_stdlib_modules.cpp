#include "far_stdlib.h"

#include "error.h"

#include <unordered_map>
#include <unordered_set>

namespace far {

static const char kStdlibMod_bench[] = R"FAR_STDLIB(package far

module bench

public class bench {
  public static fun start() -> i64 { return bench_now() }
  public static fun elapsed_ms(start_ticks: i64) -> i64 { return bench_elapsed_ms(start_ticks) }
  public static fun measure(work: i64) -> i64 {
    t0 = bench_now()
    x = work
    return bench_elapsed_ms(t0)
  }
}


export bench





)FAR_STDLIB";

static const char kStdlibMod_cli[] = R"FAR_STDLIB(package far

module cli

public class cli {
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
}


export cli





)FAR_STDLIB";

static const char kStdlibMod_compress[] = R"FAR_STDLIB(package far

module compress

public class compress {
  public static fun rle_encode(data: string) -> string { return compress_rle(data) }
  public static fun rle_decode(data: string) -> string { return decompress_rle(data) }
  public static fun compress(data: string) -> string { return net_compress(data) }
  public static fun decompress(data: string) -> string { return net_decompress(data) }

}


export compress





)FAR_STDLIB";

static const char kStdlibMod_crypto[] = R"FAR_STDLIB(package far

module crypto

public class crypto {
  public static fun xor_encrypt(data: string, key: string) -> string { return xor_crypt(data, key) }
  public static fun xor_decrypt(data: string, key: string) -> string { return xor_crypt(data, key) }

}


export crypto





)FAR_STDLIB";

static const char kStdlibMod_csv[] = R"FAR_STDLIB(package far

module csv

public class csv {
  public static fun count(line: string) -> i64 { return csv_count(line) }
  public static fun field(line: string, index: i64) -> string { return csv_field(line, index) }

}


export csv





)FAR_STDLIB";

static const char kStdlibMod_date[] = R"FAR_STDLIB(package far

module date

public class date {
  public static fun year(ts: i64) -> i64 { return date_year(ts) }
  public static fun month(ts: i64) -> i64 { return date_month(ts) }
  public static fun day(ts: i64) -> i64 { return date_day(ts) }
  public static fun hour(ts: i64) -> i64 { return date_hour(ts) }
  public static fun minute(ts: i64) -> i64 { return date_minute(ts) }
  public static fun second(ts: i64) -> i64 { return date_second(ts) }

}


export date





)FAR_STDLIB";

static const char kStdlibMod_dev[] = R"FAR_STDLIB(package far

module dev

public class debug {
  public static fun set_break(id: i64) -> i64 { return dbg_break(id) }
  public static fun has_break(id: i64) -> i64 { return dbg_is_break(id) }
  public static fun step() -> i64 { return dbg_step() }

}

public class deps {
  public static fun count(manifest: string) -> i64 { return dep_count(manifest) }
  public static fun at(manifest: string, index: i64) -> string { return dep_at(manifest, index) }
  public static fun satisfies(name: string, constraint: string) -> i64 { return dep_satisfies(name, constraint) }

}

public class format {
  public static fun trim(text: string) -> string { return fmt_trim(text) }
  public static fun indent(text: string, spaces: i64) -> string { return fmt_indent(text, spaces) }

}

public class hotreload {
  public static fun mtime(path: string) -> i64 { return hot_mtime(path) }
  public static fun stale(path: string, since_ms: i64) -> i64 { return hot_stale(path, since_ms) }

}

public class immutable {
  public static fun seal(value: i64) -> i64 { return immut_seal(value) }
  public static fun value(sealed: i64) -> i64 { return immut_value(sealed) }
  public static fun is_sealed(sealed: i64) -> i64 { return immut_is_sealed(sealed) }

}

public class inference {
  public static fun kind(tag: i64) -> i64 { return infer_kind(tag) }
  public static fun fields(tag: i64) -> i64 { return infer_fields(tag) }
  public static fun label(kind: i64) -> string { return infer_label(kind) }

}

public class lint {
  public static fun valid_ident(name: string) -> i64 { return lint_valid_ident(name) }
  public static fun count_issues(source: string) -> i64 { return lint_count_issues(source) }

}

public class live {
  public static fun generation() -> i64 { return live_generation() }
  public static fun bump() -> i64 { return live_bump() }
  public static fun tick(gen: i64) -> i64 { return live_tick(gen) }

}

public class lsp {
  public static fun hover(symbol: string) -> string { return lsp_hover(symbol) }
  public static fun symbol_kind(symbol: string) -> i64 { return lsp_kind(symbol) }

}

public class nullable {
  public static fun of_value(value: i64) -> i64 { return null_some(value) }
  public static fun empty() -> i64 { return null_none() }
  public static fun has_value(opt: i64) -> i64 { return null_is_some(opt) }
  public static fun get_or(opt: i64, alt: i64) -> i64 { return null_unwrap_or(opt, alt) }
  public static fun map_or_default(opt: i64, mapped: i64, fallback: i64) -> i64 {
    return null_map_or(opt, mapped, fallback)
  }
}

public class pattern {
  public static fun eq(value: i64, literal: i64) -> i64 { return pat_eq(value, literal) }
  public static fun wildcard() -> i64 { return pat_wildcard() }
  public static fun in_range(value: i64, lo: i64, hi: i64) -> i64 { return pat_in_range(value, lo, hi) }

}

public class pkg {
  public static fun read_manifest(path: string) -> string { return pkg_read(path) }
  public static fun name(manifest: string) -> string { return pkg_name(manifest) }
  public static fun version(manifest: string) -> string { return pkg_version(manifest) }

}

public class profile {
  public static fun start() -> i64 { return prof_start() }
  public static fun elapsed(start_ms: i64) -> i64 { return prof_elapsed(start_ms) }
  public static fun mem_kb() -> i64 { return prof_mem_kb() }

}

public class readonly {
  public static fun wrap(value: i64) -> i64 { return readonly_wrap(value) }
  public static fun get(wrapped: i64) -> i64 { return readonly_get(wrapped) }
  public static fun is_readonly(wrapped: i64) -> i64 { return readonly_is(wrapped) }

}

public class repl {
  public static fun eval_expr(expr: string) -> i64 { return repl_eval(expr) }
  public static fun history_add(line: string) -> i64 { return repl_history_add(line) }
  public static fun history_count() -> i64 { return repl_history_count() }

}

public class shell {
  public static fun prompt(label: string) -> string { return shell_prompt(label) }
  public static fun read_line(prompt: string) -> string { return shell_read(prompt) }

}

export debug, deps, format, hotreload, immutable, inference, lint, live, lsp, nullable, pattern, pkg, profile, readonly, repl, shell




)FAR_STDLIB";

static const char kStdlibMod_env[] = R"FAR_STDLIB(package far

module env

public class env {
  public static fun env_get_var(name: string) -> string { return env_get(name) }
  public static fun env_set_var(name: string, value: string) -> i64 { return env_set(name, value) }
  public static fun env_has_var(name: string) -> i64 { return env_has(name) }

}


export env





)FAR_STDLIB";

static const char kStdlibMod_fs[] = R"FAR_STDLIB(package far

module fs

public class fs {
  public static fun read_text(path: string) -> string { return fs_read(path) }
  public static fun write_text(path: string, content: string) -> i64 { return fs_write(path, content) }
  public static fun exists(path: string) -> i64 { return fs_exists(path) }
  public static fun is_file(path: string) -> i64 { return fs_is_file(path) }
  public static fun is_dir(path: string) -> i64 { return fs_is_dir(path) }
  public static fun mkdir(path: string) -> i64 { return fs_mkdir(path) }
  public static fun remove(path: string) -> i64 { return fs_remove(path) }

}


export fs





)FAR_STDLIB";

static const char kStdlibMod_hash[] = R"FAR_STDLIB(package far

module hash

public class hash {
  public static fun fnv(s: string) -> i64 { return hash_fnv(s) }
  public static fun crc32(s: string) -> i64 { return hash_crc32(s) }
  public static fun md5_hex(s: string) -> string { return hash_md5_hex(s) }

}


export hash





)FAR_STDLIB";

static const char kStdlibMod_i18n[] = R"FAR_STDLIB(package far

module i18n

public class i18n {
  public static fun translate(key: string, lang: string) -> string {
    return key
  }
  public static fun format_msg(key: string, lang: string) -> string {
    return key
  }
}


export i18n





)FAR_STDLIB";

static const char kStdlibMod_io[] = R"FAR_STDLIB(package far

module io

public class console {
  public static fun input() -> string { return io_read_line() }
  public static fun input(msg: string) -> string { return io_read_line_prompt(msg) }
  public static fun input_prompt(msg: string) -> string { return io_read_line_prompt(msg) }
  public static fun read_line() -> string { return io_read_line() }
  public static fun read_line_prompt(msg: string) -> string { return io_read_line_prompt(msg) }
  public static fun read_char() -> i64 { return io_read_char() }
  public static fun read_i64() -> i64 { return io_read_i64() }
  public static fun read_f64() -> f64 { return io_read_f64() }
  public static fun has_input() -> i64 { return io_has_input() }
  public static fun confirm(msg: string) -> i64 { return io_confirm(msg) }
  public static fun write(text: string) -> i64 { io_write(text); return 0 }
  public static fun writeln(text: string) -> i64 { io_writeln(text); return 0 }
  public static fun write_err(text: string) -> i64 { io_write_err(text); return 0 }
  public static fun write_errln(text: string) -> i64 { io_write_errln(text); return 0 }
  public static fun flush_out() -> i64 { io_flush_stdout(); return 0 }
  public static fun flush_err() -> i64 { io_flush_stderr(); return 0 }
  public static fun clear() -> i64 { io_clear(); return 0 }
  public static fun is_tty() -> i64 { return io_is_tty() }
  public static fun columns() -> i64 { return io_columns() }
  public static fun beep() -> i64 { io_beep(); return 0 }

}

public class input {
  public static fun read_line() -> string { return io_read_line() }
  public static fun read_line_prompt(msg: string) -> string { return io_read_line_prompt(msg) }
  public static fun read_char() -> i64 { return io_read_char() }
  public static fun read_i64() -> i64 { return io_read_i64() }
  public static fun read_f64() -> f64 { return io_read_f64() }
  public static fun has_input() -> i64 { return io_has_input() }
  public static fun confirm(msg: string) -> i64 { return io_confirm(msg) }
  public static fun input() -> string { return io_read_line() }
  public static fun input(msg: string) -> string { return io_read_line_prompt(msg) }
  public static fun input_prompt(msg: string) -> string { return io_read_line_prompt(msg) }

}

public class output {
  public static fun write_text(text: string) -> i64 { io_write(text); return 0 }
  public static fun write_line(text: string) -> i64 { io_writeln(text); return 0 }
  public static fun write_err(text: string) -> i64 { io_write_err(text); return 0 }
  public static fun write_err_line(text: string) -> i64 { io_write_errln(text); return 0 }
  public static fun flush_out() -> i64 { io_flush_stdout(); return 0 }
  public static fun flush_err() -> i64 { io_flush_stderr(); return 0 }

}

public class terminal {
  public static fun clear_screen() -> i64 { io_clear(); return 0 }
  public static fun is_tty() -> i64 { return io_is_tty() }
  public static fun columns() -> i64 { return io_columns() }
  public static fun beep() -> i64 { io_beep(); return 0 }

}

export console, input, output, terminal




)FAR_STDLIB";

static const char kStdlibMod_json[] = R"FAR_STDLIB(package far

module json

public class json {
  public static fun escape(s: string) -> string { return json_escape(s) }
  public static fun stringify_i64(v: i64) -> string { return json_stringify_i64(v) }
  public static fun stringify_str(s: string) -> string { return json_stringify_str(s) }
  public static fun get_i64(doc: string, key: string) -> i64 { return json_get_i64(doc, key) }
  public static fun get_str(doc: string, key: string) -> string { return json_get_str(doc, key) }

}


export json





)FAR_STDLIB";

static const char kStdlibMod_log[] = R"FAR_STDLIB(package far

module log

public class log {
  public static fun info(msg: string) -> i64 { log_info(msg); return 0 }
  public static fun warn(msg: string) -> i64 { log_warn(msg); return 0 }
  public static fun error(msg: string) -> i64 { log_error(msg); return 0 }
  public static fun debug(msg: string) -> i64 { log_debug(msg); return 0 }

}


export log





)FAR_STDLIB";

static const char kStdlibMod_math[] = R"FAR_STDLIB(package far

module math

public class math {
  public static fun sin(x: f64) -> f64 { return sin(x) }
  public static fun cos(x: f64) -> f64 { return cos(x) }
  public static fun tan(x: f64) -> f64 { return tan(x) }
  public static fun asin(x: f64) -> f64 { return asin(x) }
  public static fun acos(x: f64) -> f64 { return acos(x) }
  public static fun atan(x: f64) -> f64 { return atan(x) }
  public static fun atan2(x: f64, y: f64) -> f64 { return atan2(x, y) }
  public static fun sinh(x: f64) -> f64 { return sinh(x) }
  public static fun cosh(x: f64) -> f64 { return cosh(x) }
  public static fun tanh(x: f64) -> f64 { return tanh(x) }
  public static fun asinh(x: f64) -> f64 { return asinh(x) }
  public static fun acosh(x: f64) -> f64 { return acosh(x) }
  public static fun atanh(x: f64) -> f64 { return atanh(x) }
  public static fun sqrt(x: f64) -> f64 { return sqrt(x) }
  public static fun cbrt(x: f64) -> f64 { return cbrt(x) }
  public static fun hypot(x: f64, y: f64) -> f64 { return hypot(x, y) }
  public static fun pow(x: f64, y: f64) -> f64 { return pow(x, y) }
  public static fun exp(x: f64) -> f64 { return exp(x) }
  public static fun log(x: f64) -> f64 { return log(x) }
  public static fun log10(x: f64) -> f64 { return log10(x) }
  public static fun log2(x: f64) -> f64 { return log2(x) }
  public static fun exp2(x: f64) -> f64 { return exp2(x) }
  public static fun log1p(x: f64) -> f64 { return log1p(x) }
  public static fun expm1(x: f64) -> f64 { return expm1(x) }
  public static fun floor(x: f64) -> f64 { return floor(x) }
  public static fun ceil(x: f64) -> f64 { return ceil(x) }
  public static fun round(x: f64) -> f64 { return round(x) }
  public static fun trunc(x: f64) -> f64 { return trunc(x) }
  public static fun fabs(x: f64) -> f64 { return fabs(x) }
  public static fun fmod(x: f64, y: f64) -> f64 { return fmod(x, y) }
  public static fun copysign(x: f64, y: f64) -> f64 { return copysign(x, y) }
  public static fun pi() -> f64 { return pi() }
  public static fun e() -> f64 { return e() }
  public static fun tau() -> f64 { return tau() }
  public static fun phi() -> f64 { return phi() }
  public static fun sqrt2() -> f64 { return sqrt2() }
  public static fun sqrt3() -> f64 { return sqrt3() }
  public static fun ln2() -> f64 { return ln2() }
  public static fun ln10() -> f64 { return ln10() }
  public static fun deg_per_rad() -> f64 { return deg_per_rad() }
  public static fun rad_per_deg() -> f64 { return rad_per_deg() }
  public static fun imin(x: i64, y: i64) -> i64 { return imin(x, y) }
  public static fun imax(x: i64, y: i64) -> i64 { return imax(x, y) }
  public static fun imin3(x: i64, y: i64, z: i64) -> i64 { return imin3(x, y, z) }
  public static fun imax3(x: i64, y: i64, z: i64) -> i64 { return imax3(x, y, z) }
  public static fun iabs(x: i64) -> i64 { return iabs(x) }
  public static fun isign(x: i64) -> i64 { return isign(x) }
  public static fun clamp_i(x: i64, y: i64, z: i64) -> i64 { return clamp_i(x, y, z) }
  public static fun is_even(x: i64) -> bool { return is_even(x) }
  public static fun is_odd(x: i64) -> bool { return is_odd(x) }
  public static fun mod_pos(x: i64, y: i64) -> i64 { return mod_pos(x, y) }
  public static fun gcd(x: i64, y: i64) -> i64 { return gcd(x, y) }
  public static fun lcm(x: i64, y: i64) -> i64 { return lcm(x, y) }
  public static fun factorial(x: i64) -> i64 { return factorial(x) }
  public static fun binomial(x: i64, y: i64) -> i64 { return binomial(x, y) }
  public static fun isqrt(x: i64) -> i64 { return isqrt(x) }
  public static fun ipow(x: i64, y: i64) -> i64 { return ipow(x, y) }
  public static fun sum_range(x: i64, y: i64) -> i64 { return sum_range(x, y) }
  public static fun sum_range_inclusive(x: i64, y: i64) -> i64 { return sum_range_inclusive(x, y) }
  public static fun product_range(x: i64, y: i64) -> i64 { return product_range(x, y) }
  public static fun fib(x: i64) -> i64 { return fib(x) }
  public static fun fib_iter(x: i64) -> i64 { return fib_iter(x) }
  public static fun twice(x: i64) -> i64 { return twice(x) }
  public static fun thrice(x: i64) -> i64 { return thrice(x) }
  public static fun quad(x: i64) -> i64 { return quad(x) }
  public static fun deg_to_rad(x: f64) -> f64 { return deg_to_rad(x) }
  public static fun rad_to_deg(x: f64) -> f64 { return rad_to_deg(x) }
  public static fun sin_deg(x: f64) -> f64 { return sin_deg(x) }
  public static fun cos_deg(x: f64) -> f64 { return cos_deg(x) }
  public static fun tan_deg(x: f64) -> f64 { return tan_deg(x) }
  public static fun asin_deg(x: f64) -> f64 { return asin_deg(x) }
  public static fun acos_deg(x: f64) -> f64 { return acos_deg(x) }
  public static fun atan_deg(x: f64) -> f64 { return atan_deg(x) }
  public static fun atan2_deg(x: f64, y: f64) -> f64 { return atan2_deg(x, y) }
  public static fun normalize_rad(x: f64) -> f64 { return normalize_rad(x) }
  public static fun normalize_deg(x: f64) -> f64 { return normalize_deg(x) }
  public static fun sec(x: f64) -> f64 { return sec(x) }
  public static fun csc(x: f64) -> f64 { return csc(x) }
  public static fun cot(x: f64) -> f64 { return cot(x) }
  public static fun haversine(x: f64, y: f64, z: f64, w: f64) -> f64 { return haversine(x, y, z, w) }
  public static fun dmin(x: f64, y: f64) -> f64 { return dmin(x, y) }
  public static fun dmax(x: f64, y: f64) -> f64 { return dmax(x, y) }
  public static fun dmin3(x: f64, y: f64, z: f64) -> f64 { return dmin3(x, y, z) }
  public static fun dmax3(x: f64, y: f64, z: f64) -> f64 { return dmax3(x, y, z) }
  public static fun clamp_d(x: f64, y: f64, z: f64) -> f64 { return clamp_d(x, y, z) }
  public static fun saturate(x: f64) -> f64 { return saturate(x) }
  public static fun lerp(x: f64, y: f64, z: f64) -> f64 { return lerp(x, y, z) }
  public static fun inv_lerp(x: f64, y: f64, z: f64) -> f64 { return inv_lerp(x, y, z) }
  public static fun remap(x: f64, y: f64, z: f64, w: f64, v: f64) -> f64 { return remap(x, y, z, w, v) }
  public static fun square(x: f64) -> f64 { return square(x) }
  public static fun cube(x: f64) -> f64 { return cube(x) }
  public static fun approx_eq(x: f64, y: f64, z: f64) -> bool { return approx_eq(x, y, z) }
  public static fun approx_zero(x: f64, y: f64) -> bool { return approx_zero(x, y) }
  public static fun dist2(x: f64, y: f64, z: f64, w: f64) -> f64 { return dist2(x, y, z, w) }
  public static fun dist(x: f64, y: f64, z: f64, w: f64) -> f64 { return dist(x, y, z, w) }
  public static fun sign_d(x: f64) -> f64 { return sign_d(x) }
  public static fun round_i(x: f64) -> i64 { return round_i(x) }
  public static fun floor_i(x: f64) -> i64 { return floor_i(x) }
  public static fun ceil_i(x: f64) -> i64 { return ceil_i(x) }
  public static fun log_n(x: f64, y: f64) -> f64 { return log_n(x, y) }
  public static fun exp10(x: f64) -> f64 { return exp10(x) }
  public static fun smoothstep(x: f64, y: f64, z: f64) -> f64 { return smoothstep(x, y, z) }
  public static fun mean2(x: f64, y: f64) -> f64 { return mean2(x, y) }
  public static fun mean3(x: f64, y: f64, z: f64) -> f64 { return mean3(x, y, z) }
  public static fun variance2(x: f64, y: f64) -> f64 { return variance2(x, y) }
  public static fun stddev2(x: f64, y: f64) -> f64 { return stddev2(x, y) }
  public static fun arr_min(x: arr) -> i64 { return arr_min(x) }
  public static fun arr_max(x: arr) -> i64 { return arr_max(x) }
  public static fun arr_sum(x: arr) -> i64 { return arr_sum(x) }
  public static fun arr_mean(x: arr) -> i64 { return arr_mean(x) }
  public static fun arr_count(x: arr, y: i64) -> i64 { return arr_count(x, y) }
  public static fun arr_index_of(x: arr, y: i64) -> i64 { return arr_index_of(x, y) }
  public static fun vec2_lerp(x: dvec2, y: dvec2, z: f64) -> dvec2 { return vec2_lerp(x, y, z) }
  public static fun vec2_reflect(x: dvec2, y: dvec2) -> dvec2 { return vec2_reflect(x, y) }
  public static fun vec2_angle(x: dvec2) -> f64 { return vec2_angle(x) }  public static fun rect_union(x: drect, y: drect) -> drect { return rect_union(x, y) }
  public static fun clamp(x: f64, lo: f64, hi: f64) -> f64 { return clamp_d(x, lo, hi) }
  public static fun deg(v: f64) -> f64 { return rad_to_deg(v) }
  public static fun rad(v: f64) -> f64 { return deg_to_rad(v) }
}

export math



)FAR_STDLIB";

static const char kStdlibMod_network[] = R"FAR_STDLIB(package far

module network

public class TcpClient {
  public sock: i64

  public TcpClient() {
    sock = -1
  }

  public fun connect(host: string, port: i64) -> i64 {
    if (sock >= 0) {
      tcp_close(sock)
    }
    sock = tcp_connect(host, port)
    return sock
  }

  public fun send(data: string) -> i64 {
    return tcp_send(sock, data)
  }

  public fun sendall(data: string) -> i64 {
    return tcp_send(sock, data)
  }

  public fun recv(max: i64) -> string {
    return tcp_recv(sock, max)
  }

  public fun close() -> i64 {
    if (sock < 0) { return 0 }
    r = tcp_close(sock)
    sock = -1
    return r
  }

  public fun is_open() -> i64 {
    if (sock >= 0) { return 1 }
    return 0
  }
}

public class UdpClient {
  public sock: i64

  public UdpClient() {
    sock = udp_bind(0)
  }

  public fun bind(port: i64) -> i64 {
    if (sock >= 0) {
      udp_close(sock)
    }
    sock = udp_bind(port)
    return sock
  }

  public fun sendto(host: string, port: i64, data: string) -> i64 {
    return udp_send(sock, host, port, data)
  }

  public fun recvfrom(max: i64) -> string {
    return udp_recv(sock, max)
  }

  public fun close() -> i64 {
    if (sock < 0) { return 0 }
    r = udp_close(sock)
    sock = -1
    return r
  }
}

public class client {
  public static fun tcp_open(host: string, port: i64) -> TcpClient {
    c = TcpClient()
    c.connect(host, port)
    return c
  }
}

public class HttpClient {
  public base: string

  public HttpClient(base_url: string) {
    base = base_url
  }

  public fun fetch() -> string {
    return web_get(base + "/")
  }

  public fun get(path: string) -> string {
    return web_get(base + path)
  }

  public fun post(path: string, body: string) -> string {
    return web_post(base + path, body)
  }

  public fun post_form(path: string, body: string) -> string {
    return http_post_ct(base + path, body, "application/x-www-form-urlencoded")
  }

  public fun put(path: string, body: string) -> string {
    return rest_put(base + path, body)
  }

  public fun del(path: string) -> string {
    return rest_delete(base + path)
  }
}

public class HttpsClient {
  public base: string

  public HttpsClient(base_url: string) {
    base = base_url
  }

  public fun get(path: string) -> string {
    return https_get(base + path)
  }

  public fun post(path: string, body: string) -> string {
    return https_post(base + path, body)
  }
}

public class tcp {
  public static fun open(host: string, port: i64) -> i64 { return tcp_connect(host, port) }
  public static fun listen_on(port: i64) -> i64 { return tcp_listen(port) }
  public static fun accept_client(sock: i64) -> i64 { return tcp_accept(sock) }
  public static fun write(sock: i64, data: string) -> i64 { return tcp_send(sock, data) }
  public static fun read(sock: i64, max: i64) -> string { return tcp_recv(sock, max) }
  public static fun close_conn(sock: i64) -> i64 { return tcp_close(sock) }

}

public class udp {
  public static fun bind_port(port: i64) -> i64 { return udp_bind(port) }
  public static fun send_to(sock: i64, host: string, port: i64, data: string) -> i64 {
    return udp_send(sock, host, port, data)
  }
  public static fun recv_from(sock: i64, max: i64) -> string { return udp_recv(sock, max) }
  public static fun close_sock(sock: i64) -> i64 { return udp_close(sock) }

}

public class http {
  public static fun get(url: string) -> string { return http_get(url) }
  public static fun post(url: string, body: string) -> string { return http_post(url, body) }
  public static fun post_ct(url: string, body: string, ctype: string) -> string {
    return http_post_ct(url, body, ctype)
  }
  public static fun parse_status(response: string) -> i64 { return http_parse_status(response) }

}

public class https {
  public static fun get(url: string) -> string { return https_get(url) }
  public static fun post(url: string, body: string) -> string { return https_post(url, body) }

}

public class rest {
  public static fun get(url: string) -> string { return rest_get(url) }
  public static fun post(url: string, body: string) -> string { return rest_post(url, body) }
  public static fun put(url: string, body: string) -> string { return rest_put(url, body) }
  public static fun del(url: string) -> string { return rest_delete(url) }

}

public class rpc {
  public static fun call(host: string, port: i64, method: string, params: string) -> string {
    return rpc_call(host, port, method, params)
  }
}

public class grpc {
  public static fun call(host: string, port: i64, method: string, payload: string) -> string {
    return grpc_call(host, port, method, payload)
  }
}

public class serialize {
  public static fun pack(service: string, method: string, body: string) -> string {
    return net_pack(service, method, body)
  }
  public static fun unpack_method(packed: string) -> string { return net_unpack_method(packed) }
  public static fun unpack_body(packed: string) -> string { return net_unpack_body(packed) }
  public static fun to_json(data: string) -> string { return net_serialize(data) }
  public static fun from_json(json: string, key: string) -> string { return net_deserialize(json, key) }

}

public class websocket {
  public static fun open(host: string, port: i64, path: string) -> i64 {
    return ws_connect(host, port, path)
  }
  public static fun send_text(sock: i64, text: string) -> i64 { return ws_send(sock, text) }
  public static fun recv_text(sock: i64) -> string { return ws_recv(sock) }
  public static fun close_sock(sock: i64) -> i64 { return ws_close(sock) }

}

public class compress {
  public static fun compress(data: string) -> string { return net_compress(data) }
  public static fun decompress(data: string) -> string { return net_decompress(data) }

}

public class net {
  public static fun connect(host: string, port: i64) -> i64 { return net_connect(host, port) }
  public static fun send(sock: i64, data: string) -> i64 { return net_send(sock, data) }
  public static fun recv(sock: i64, max: i64) -> string { return net_recv(sock, max) }
  public static fun close(sock: i64) -> i64 { return net_close(sock) }

}

export TcpClient, UdpClient, client, HttpClient, HttpsClient, tcp, udp, http, https, rest, rpc, grpc, serialize, websocket, compress, net




)FAR_STDLIB";

static const char kStdlibMod_perf[] = R"FAR_STDLIB(package far

module perf

public class incremental {
  public static fun generation() -> i64 { return cache_generation() }
  public static fun bump() -> i64 { return cache_bump() }
  public static fun stamp(path: string) -> i64 { return cache_stamp(path) }
  public static fun stale(path: string, since_ms: i64) -> i64 { return cache_stale(path, since_ms) }

}

public class llvm {
  public static fun version() -> string { return llvm_version() }
  public static fun opt_level() -> i64 { return llvm_opt_level() }

}

public class lowmem {
  public static fun heap_size_kb() -> i64 { return heap_kb() }
  public static fun peak_size_kb() -> i64 { return peak_heap_kb() }

}

public class native {
  public static fun target_triple() -> string { return native_target() }
  public static fun compiled_native() -> i64 { return is_native() }

}

public class predictable {
  public static fun mark() -> i64 { return mark_latency() }
  public static fun since_mark_ms(start: i64) -> i64 { return latency_ms(start) }
  public static fun jitter_between(a: i64, b: i64) -> i64 { return jitter_ms(a, b) }
  public static fun deterministic(a: i64, b: i64) -> i64 { return is_deterministic(a, b) }

}

public class simd {
  public static fun width() -> i64 { return simd_width() }
  public static fun add4(a: i64, b: i64, c: i64, d: i64) -> i64 { return simd_add4(a, b, c, d) }

}

public class startup {
  public static fun boot_ms() -> i64 { return boot_time_ms() }
  public static fun ready() -> i64 { return runtime_ready() }
  public static fun elapsed_ms() -> i64 { return startup_elapsed() }

}

public class threads {
  public static fun count() -> i64 { return thread_count() }
  public static fun current_id() -> i64 { return current_thread() }

  public static fun join(handle: i64) -> i64 { return join(handle) }
  public static fun cores() -> i64 { return cores() }
}

public class vectorize {
  public static fun enabled() -> i64 { return vec_enabled() }
  public static fun hint(width: i64) -> i64 { return vec_hint(width) }
  public static fun dot4(a: i64, b: i64, c: i64, d: i64) -> i64 { return vec_dot4(a, b, c, d) }

}

export incremental, llvm, lowmem, native, predictable, simd, startup, threads, vectorize




)FAR_STDLIB";

static const char kStdlibMod_proc[] = R"FAR_STDLIB(package far

module proc

public class proc {
  public static fun pid() -> i64 { return proc_pid() }
  public static fun run(cmd: string) -> i64 { return proc_run(cmd) }

}


export proc





)FAR_STDLIB";

static const char kStdlibMod_random[] = R"FAR_STDLIB(package far

module random

public class random {
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
}


export random





)FAR_STDLIB";

static const char kStdlibMod_regex[] = R"FAR_STDLIB(package far

module regex

public class regex {
  public static fun glob_match(pat: string, text: string) -> i64 { return regex_match(pat, text) }
  public static fun glob_find(pat: string, text: string) -> i64 { return regex_find(pat, text) }

}


export regex





)FAR_STDLIB";

static const char kStdlibMod_science[] = R"FAR_STDLIB(package far

module science

public class fft {
  public static fun forward(signal: arr) -> arr { return sci_fft(signal) }
  public static fun inverse(spectrum: arr) -> arr { return sci_ifft(spectrum) }

}

public class ml {
  public static fun sigmoid(x: f64) -> f64 { return sci_sigmoid(x) }
  public static fun relu(x: f64) -> f64 { return sci_relu(x) }
  public static fun tanh_act(x: f64) -> f64 { return sci_tanh(x) }
  public static fun softmax_vec(v: arr) -> arr { return sci_softmax(v) }
  public static fun dot_vecs(a: arr, b: arr) -> f64 { return sci_dot(a, b) }

}

public class numerical {
  public static fun integrate_trapz(y: arr, h: f64) -> f64 { return sci_trapz(y, h) }
  public static fun integrate_simpson(y: arr, h: f64) -> f64 { return sci_simpson(y, h) }
  public static fun finite_diff(y: arr) -> arr { return sci_finite_diff(y) }
  public static fun scale_arr(y: arr, t: f64) -> arr { return sci_lerp_arr(y, t) }

}

public class optimization {
  public static fun grad_step(x: f64, lr: f64, grad: f64) -> f64 {
    return sci_gradient_descent(x, lr, grad)
  }
  public static fun parabola_vertex(a: f64, b: f64, c: f64) -> f64 {
    return sci_parabola_min(a, b, c)
  }
}

public class physics {
  public static fun kinetic(m: f64, v: f64) -> f64 { return sci_kinetic_energy(m, v) }
  public static fun potential(m: f64, g: f64, h: f64) -> f64 { return sci_potential_energy(m, g, h) }
  public static fun gravity(m1: f64, m2: f64, r: f64) -> f64 { return sci_gravitational_force(m1, m2, r) }
  public static fun projectile_range(v0: f64, angle: f64, g: f64) -> f64 { return sci_projectile_range(v0, angle, g) }
  public static fun hooke(k: f64, x: f64) -> f64 { return sci_hooke_force(k, x) }

}

public class statistics {
  public static fun mean(data: arr) -> f64 { return sci_mean(data) }
  public static fun variance(data: arr) -> f64 { return sci_variance(data) }
  public static fun stddev(data: arr) -> f64 { return sci_stddev(data) }
  public static fun median(data: arr) -> f64 { return sci_median(data) }
  public static fun correlation(a: arr, b: arr) -> f64 { return sci_correlation(a, b) }

}

export fft, ml, numerical, optimization, physics, statistics




)FAR_STDLIB";

static const char kStdlibMod_security[] = R"FAR_STDLIB(package far

module security

public class boundscheck {
  public static fun index_ok(index: i64, len: i64) -> i64 { return bounds_check(index, len) }
  public static fun slice_ok(start: i64, len: i64, cap: i64) -> i64 { return bounds_slice(start, len, cap) }
  public static fun clamp_index(index: i64, len: i64) -> i64 { return bounds_clamp(index, len) }

}

public class concurrency {
  public static fun acquire(id: i64) -> i64 { return safe_lock(id) }
  public static fun release(id: i64) -> i64 { return safe_unlock(id) }
  public static fun try_acquire(id: i64) -> i64 { return safe_try_lock(id) }
  public static fun is_owned(id: i64) -> i64 { return safe_owned(id) }

}

public class crypto {
  public static fun digest(data: string) -> string { return crypto_digest(data) }
  public static fun encrypt(data: string, key: string) -> string { return crypto_encrypt(data, key) }
  public static fun secure_eq(a: string, b: string) -> i64 { return crypto_verify(a, b) }
  public static fun token(nbytes: i64) -> string { return crypto_token(nbytes) }

}

public class memsafe {
  public static fun make_guard(size: i64) -> i64 { return mem_guard(size) }
  public static fun is_valid(guard_id: i64) -> i64 { return mem_valid(guard_id) }
  public static fun wipe(guard_id: i64) -> i64 { return mem_scrub(guard_id) }
  public static fun guarded_size(guard_id: i64) -> i64 { return mem_size(guard_id) }

}

public class overflow {
  public static fun add_safe(a: i64, b: i64) -> i64 { return i64_add_safe(a, b) }
  public static fun mul_safe(a: i64, b: i64) -> i64 { return i64_mul_safe(a, b) }
  public static fun sub_safe(a: i64, b: i64) -> i64 { return i64_sub_safe(a, b) }
  public static fun overflowed() -> i64 { return i64_overflowed() }

}

public class permission {
  public static fun grant(subject: i64, perm: i64) -> i64 { return perm_grant(subject, perm) }
  public static fun revoke(subject: i64, perm: i64) -> i64 { return perm_revoke(subject, perm) }
  public static fun has_perm(subject: i64, perm: i64) -> i64 { return perm_check(subject, perm) }
  public static fun bits(subject: i64) -> i64 { return perm_bits(subject) }

}

public class sandbox {
  public static fun open_sandbox(level: i64) -> i64 { return sandbox_enter(level) }
  public static fun close_sandbox() -> i64 { return sandbox_exit() }
  public static fun is_active() -> i64 { return sandbox_active() }
  public static fun allow_path(path: string) -> i64 { return sandbox_allow(path) }
  public static fun can_access(path: string) -> i64 { return sandbox_can(path) }

}

export boundscheck, concurrency, crypto, memsafe, overflow, permission, sandbox




)FAR_STDLIB";

static const char kStdlibMod_test[] = R"FAR_STDLIB(package far

module test

public class test {
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
}


export test





)FAR_STDLIB";

static const char kStdlibMod_time[] = R"FAR_STDLIB(package far

module time

public class time {
  public static fun milliseconds() -> i64 { return now_ms() }
  public static fun seconds() -> i64 { return now_sec() }
  public static fun ticks() -> i64 { return clock_ticks() }

}


export time





)FAR_STDLIB";

static const char kStdlibMod_xml[] = R"FAR_STDLIB(package far

module xml

public class xml {
  public static fun escape(s: string) -> string { return xml_escape(s) }
  public static fun tag(name: string, body: string) -> string { return xml_tag(name, body) }
  public static fun get_attr(doc: string, attr: string) -> string { return xml_get_attr(doc, attr) }

}


export xml





)FAR_STDLIB";

static const char kStdlibMod_yaml[] = R"FAR_STDLIB(package far

module yaml

public class yaml {
  public static fun yaml_get_value(doc: string, key: string) -> string { return yaml_get(doc, key) }

}


export yaml





)FAR_STDLIB";

// === GEOM_MODULES_BEGIN ===
static const char kStdlibMod_vectors[] = R"FAR_STDLIB(package far

module vectors

public class vec2 {}

public class vec3 {}

public class vec4 {}

public class dvec2 {}

public class dvec3 {}

public class dvec4 {}

public class ivec2 {}

public class ivec3 {}

public class ivec4 {}

public class vectors {
  public static fun length(v: vec2) -> f64 { return 0.0 }
  public static fun length(v: vec3) -> f64 { return 0.0 }
  public static fun length(v: vec4) -> f64 { return 0.0 }
  public static fun length(v: dvec2) -> f64 { return 0.0 }
  public static fun length(v: dvec3) -> f64 { return 0.0 }
  public static fun length(v: dvec4) -> f64 { return 0.0 }
  public static fun length(v: ivec2) -> f64 { return 0.0 }
  public static fun length(v: ivec3) -> f64 { return 0.0 }
  public static fun length(v: ivec4) -> f64 { return 0.0 }
  public static fun length2(v: vec2) -> f64 { return 0.0 }
  public static fun length2(v: vec3) -> f64 { return 0.0 }
  public static fun length2(v: vec4) -> f64 { return 0.0 }
  public static fun length2(v: dvec2) -> f64 { return 0.0 }
  public static fun length2(v: dvec3) -> f64 { return 0.0 }
  public static fun length2(v: dvec4) -> f64 { return 0.0 }
  public static fun length2(v: ivec2) -> i64 { return 0 }
  public static fun length2(v: ivec3) -> i64 { return 0 }
  public static fun length2(v: ivec4) -> i64 { return 0 }
  public static fun dot(a: vec2, b: vec2) -> f64 { return 0.0 }
  public static fun dot(a: vec3, b: vec3) -> f64 { return 0.0 }
  public static fun dot(a: vec4, b: vec4) -> f64 { return 0.0 }
  public static fun dot(a: dvec2, b: dvec2) -> f64 { return 0.0 }
  public static fun dot(a: dvec3, b: dvec3) -> f64 { return 0.0 }
  public static fun dot(a: dvec4, b: dvec4) -> f64 { return 0.0 }
  public static fun dot(a: ivec2, b: ivec2) -> i64 { return 0 }
  public static fun dot(a: ivec3, b: ivec3) -> i64 { return 0 }
  public static fun dot(a: ivec4, b: ivec4) -> i64 { return 0 }
  public static fun distance(a: vec2, b: vec2) -> f64 { return 0.0 }
  public static fun distance(a: vec3, b: vec3) -> f64 { return 0.0 }
  public static fun distance(a: vec4, b: vec4) -> f64 { return 0.0 }
  public static fun distance(a: dvec2, b: dvec2) -> f64 { return 0.0 }
  public static fun distance(a: dvec3, b: dvec3) -> f64 { return 0.0 }
  public static fun distance(a: dvec4, b: dvec4) -> f64 { return 0.0 }
  public static fun distance2(a: vec2, b: vec2) -> f64 { return 0.0 }
  public static fun distance2(a: vec3, b: vec3) -> f64 { return 0.0 }
  public static fun distance2(a: vec4, b: vec4) -> f64 { return 0.0 }
  public static fun distance2(a: dvec2, b: dvec2) -> f64 { return 0.0 }
  public static fun distance2(a: dvec3, b: dvec3) -> f64 { return 0.0 }
  public static fun distance2(a: dvec4, b: dvec4) -> f64 { return 0.0 }
  public static fun normalize(v: vec2) -> vec2 { return v }
  public static fun normalize(v: vec3) -> vec3 { return v }
  public static fun normalize(v: vec4) -> vec4 { return v }
  public static fun normalize(v: dvec2) -> dvec2 { return v }
  public static fun normalize(v: dvec3) -> dvec3 { return v }
  public static fun normalize(v: dvec4) -> dvec4 { return v }
  public static fun min(a: vec2, b: vec2) -> vec2 { return a }
  public static fun min(a: vec3, b: vec3) -> vec3 { return a }
  public static fun min(a: vec4, b: vec4) -> vec4 { return a }
  public static fun min(a: dvec2, b: dvec2) -> dvec2 { return a }
  public static fun min(a: dvec3, b: dvec3) -> dvec3 { return a }
  public static fun min(a: dvec4, b: dvec4) -> dvec4 { return a }
  public static fun min(a: ivec2, b: ivec2) -> ivec2 { return a }
  public static fun min(a: ivec3, b: ivec3) -> ivec3 { return a }
  public static fun min(a: ivec4, b: ivec4) -> ivec4 { return a }
  public static fun max(a: vec2, b: vec2) -> vec2 { return a }
  public static fun max(a: vec3, b: vec3) -> vec3 { return a }
  public static fun max(a: vec4, b: vec4) -> vec4 { return a }
  public static fun max(a: dvec2, b: dvec2) -> dvec2 { return a }
  public static fun max(a: dvec3, b: dvec3) -> dvec3 { return a }
  public static fun max(a: dvec4, b: dvec4) -> dvec4 { return a }
  public static fun max(a: ivec2, b: ivec2) -> ivec2 { return a }
  public static fun max(a: ivec3, b: ivec3) -> ivec3 { return a }
  public static fun max(a: ivec4, b: ivec4) -> ivec4 { return a }
  public static fun clamp(v: vec2, lo: vec2, hi: vec2) -> vec2 { return v }
  public static fun clamp(v: vec3, lo: vec3, hi: vec3) -> vec3 { return v }
  public static fun clamp(v: vec4, lo: vec4, hi: vec4) -> vec4 { return v }
  public static fun clamp(v: dvec2, lo: dvec2, hi: dvec2) -> dvec2 { return v }
  public static fun clamp(v: dvec3, lo: dvec3, hi: dvec3) -> dvec3 { return v }
  public static fun clamp(v: dvec4, lo: dvec4, hi: dvec4) -> dvec4 { return v }
  public static fun approx_eq(a: vec2, b: vec2, eps: f64) -> bool { return false }
  public static fun approx_eq(a: vec3, b: vec3, eps: f64) -> bool { return false }
  public static fun approx_eq(a: vec4, b: vec4, eps: f64) -> bool { return false }
  public static fun approx_eq(a: dvec2, b: dvec2, eps: f64) -> bool { return false }
  public static fun approx_eq(a: dvec3, b: dvec3, eps: f64) -> bool { return false }
  public static fun approx_eq(a: dvec4, b: dvec4, eps: f64) -> bool { return false }
  public static fun lerp(a: vec2, b: vec2, t: f64) -> vec2 { return vec2(lerp(a.x, b.x, t), lerp(a.y, b.y, t)) }
  public static fun lerp(a: vec3, b: vec3, t: f64) -> vec3 { return vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)) }
  public static fun lerp(a: dvec2, b: dvec2, t: f64) -> dvec2 { return dvec2(lerp(a.x, b.x, t), lerp(a.y, b.y, t)) }
  public static fun lerp(a: dvec3, b: dvec3, t: f64) -> dvec3 { return dvec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)) }
  public static fun reflect_v(v: vec2, n: vec2) -> vec2 { return v }
  public static fun angle(v: vec2) -> f64 { return 0.0 }
  public static fun cross(a: vec3, b: vec3) -> vec3 { return a }
  public static fun cross(a: vec4, b: vec4) -> vec4 { return a }
  public static fun cross(a: dvec3, b: dvec3) -> dvec3 { return a }
  public static fun cross(a: dvec4, b: dvec4) -> dvec4 { return a }
  public static fun cross(a: ivec3, b: ivec3) -> ivec3 { return a }
  public static fun cross(a: ivec4, b: ivec4) -> ivec4 { return a }
  public static fun dot_components(ax: f64, ay: f64, az: f64, bx: f64, by: f64, bz: f64) -> f64 { return sci_v3_dot(ax, ay, az, bx, by, bz) }
}

export vec2, vec3, vec4, dvec2, dvec3, dvec4, ivec2, ivec3, ivec4, vectors
)FAR_STDLIB";

static const char kStdlibMod_matrices[] = R"FAR_STDLIB(package far

module matrices

public class mat2 {}

public class mat3 {}

public class mat4 {}

public class dmat2 {}

public class dmat3 {}

public class dmat4 {}

public class matrices {
  public static fun transpose(v: mat2) -> mat2 { return v }
  public static fun transpose(v: mat3) -> mat3 { return v }
  public static fun transpose(v: mat4) -> mat4 { return v }
  public static fun transpose(v: dmat2) -> dmat2 { return v }
  public static fun transpose(v: dmat3) -> dmat3 { return v }
  public static fun transpose(v: dmat4) -> dmat4 { return v }
  public static fun determinant(v: mat2) -> f64 { return 0.0 }
  public static fun determinant(v: mat3) -> f64 { return 0.0 }
  public static fun determinant(v: mat4) -> f64 { return 0.0 }
  public static fun determinant(v: dmat2) -> f64 { return 0.0 }
  public static fun determinant(v: dmat3) -> f64 { return 0.0 }
  public static fun determinant(v: dmat4) -> f64 { return 0.0 }
  public static fun mul(a: mat2, b: vec2) -> vec2 { return vec2(0.0, 0.0) }
  public static fun mul(a: mat3, b: vec3) -> vec3 { return vec3(0.0, 0.0, 0.0) }
  public static fun mul(a: mat4, b: vec4) -> vec4 { return vec4(0.0, 0.0, 0.0, 0.0) }
  public static fun mul(a: dmat2, b: dvec2) -> dvec2 { return dvec2(0.0, 0.0) }
  public static fun mul(a: dmat3, b: dvec3) -> dvec3 { return dvec3(0.0, 0.0, 0.0) }
  public static fun mul(a: dmat4, b: dvec4) -> dvec4 { return dvec4(0.0, 0.0, 0.0, 0.0) }
  public static fun trace2(m00: f64, m01: f64, m10: f64, m11: f64) -> f64 { return sci_mat2_trace(m00, m01, m10, m11) }
}

export mat2, mat3, mat4, dmat2, dmat3, dmat4, matrices
)FAR_STDLIB";

static const char kStdlibMod_points[] = R"FAR_STDLIB(package far

module points

public class point {}

public class dpoint {}

public class points {
  public static fun length(v: point) -> f64 { return 0.0 }
  public static fun length(v: dpoint) -> f64 { return 0.0 }
  public static fun length2(v: point) -> f64 { return 0.0 }
  public static fun length2(v: dpoint) -> f64 { return 0.0 }
  public static fun dot(a: point, b: point) -> f64 { return 0.0 }
  public static fun dot(a: dpoint, b: dpoint) -> f64 { return 0.0 }
  public static fun distance(a: point, b: point) -> f64 { return 0.0 }
  public static fun distance(a: dpoint, b: dpoint) -> f64 { return 0.0 }
  public static fun distance2(a: point, b: point) -> f64 { return 0.0 }
  public static fun distance2(a: dpoint, b: dpoint) -> f64 { return 0.0 }
  public static fun normalize(v: point) -> point { return v }
  public static fun normalize(v: dpoint) -> dpoint { return v }
  public static fun min(a: point, b: point) -> point { return a }
  public static fun min(a: dpoint, b: dpoint) -> dpoint { return a }
  public static fun max(a: point, b: point) -> point { return a }
  public static fun max(a: dpoint, b: dpoint) -> dpoint { return a }
  public static fun clamp(v: point, lo: point, hi: point) -> point { return v }
  public static fun clamp(v: dpoint, lo: dpoint, hi: dpoint) -> dpoint { return v }
  public static fun approx_eq(a: point, b: point, eps: f64) -> bool { return false }
  public static fun approx_eq(a: dpoint, b: dpoint, eps: f64) -> bool { return false }
  public static fun distance_to(a: point, b: point) -> f64 { return 0.0 }
  public static fun distance_to(a: dpoint, b: dpoint) -> f64 { return 0.0 }
  public static fun translate(a: point, b: point) -> point { return a }
  public static fun translate(a: dpoint, b: dpoint) -> dpoint { return a }
}

export point, dpoint, points
)FAR_STDLIB";

static const char kStdlibMod_rects[] = R"FAR_STDLIB(package far

module rects

public class rect {}

public class drect {}

public class rects {
  public static fun width(v: rect) -> f64 { return 0.0 }
  public static fun width(v: drect) -> f64 { return 0.0 }
  public static fun height(v: rect) -> f64 { return 0.0 }
  public static fun height(v: drect) -> f64 { return 0.0 }
  public static fun center(v: rect) -> point { return fpoint(0.0, 0.0) }
  public static fun center(v: drect) -> dpoint { return dpoint(0.0, 0.0) }
  public static fun contains(a: rect, b: point) -> bool { return false }
  public static fun contains(a: drect, b: dpoint) -> bool { return false }
  public static fun intersects(a: rect, b: rect) -> bool { return false }
  public static fun intersects(a: drect, b: drect) -> bool { return false }
  public static fun expand(a: rect, b: rect) -> rect { return a }
  public static fun expand(a: drect, b: drect) -> drect { return a }
  public static fun area(v: rect) -> f64 { return 0.0 }
  public static fun area(v: drect) -> f64 { return 0.0 }
  public static fun from_xywh(x: f64, y: f64, w: f64, h: f64) -> drect { return rect_from_xywh(x, y, w, h) }
  public static fun union_rect(a: drect, b: drect) -> drect { return rect_union(a, b) }
}

export rect, drect, rects
)FAR_STDLIB";

static const char kStdlibMod_quaternions[] = R"FAR_STDLIB(package far

module quaternions

public class quat {}

public class dquat {}

public class quaternions {
  public static fun length(v: quat) -> f64 { return 0.0 }
  public static fun length(v: dquat) -> f64 { return 0.0 }
  public static fun length2(v: quat) -> f64 { return 0.0 }
  public static fun length2(v: dquat) -> f64 { return 0.0 }
  public static fun dot(a: quat, b: quat) -> f64 { return 0.0 }
  public static fun dot(a: dquat, b: dquat) -> f64 { return 0.0 }
  public static fun normalize(v: quat) -> quat { return v }
  public static fun normalize(v: dquat) -> dquat { return v }
  public static fun mul(a: quat, b: quat) -> quat { return a }
  public static fun mul(a: dquat, b: dquat) -> dquat { return a }
}

export quat, dquat, quaternions
)FAR_STDLIB";

static const char kStdlibMod_colors[] = R"FAR_STDLIB(package far

module colors

public class color {}

public class color32 {}

public class colors {
  public static fun min(a: color, b: color) -> color { return a }
  public static fun max(a: color, b: color) -> color { return a }
  public static fun clamp(v: color, lo: color, hi: color) -> color { return v }
  public static fun approx_eq(a: color, b: color, eps: f64) -> bool { return false }
  public static fun to_color(v: color32) -> color { return color(0.0, 0.0, 0.0, 0.0) }
}

export color, color32, colors
)FAR_STDLIB";

static const char kStdlibMod_bounds[] = R"FAR_STDLIB(package far

module bounds

public class bounds {
  public static fun contains(a: bounds, b: vec3) -> bool { return false }
  public static fun intersects(a: bounds, b: bounds) -> bool { return false }
  public static fun expand(a: bounds, b: bounds) -> bounds { return a }
  public static fun center(v: bounds) -> vec3 { return vec3(0.0, 0.0, 0.0) }
  public static fun size(v: bounds) -> vec3 { return vec3(0.0, 0.0, 0.0) }
}

export bounds
)FAR_STDLIB";

static const char kStdlibMod_transforms[] = R"FAR_STDLIB(package far

module transforms

public class transform {}

public class transforms {
}

export transform, transforms
)FAR_STDLIB";
// === GEOM_MODULES_END ===

static const char* primaryStdlibModuleSource(const std::string& primary) {
  static const std::unordered_map<std::string, const char*> map = {
// === FLAT_MODULE_MAP_BEGIN ===
    {"bench", kStdlibMod_bench},
    {"cli", kStdlibMod_cli},
    {"compress", kStdlibMod_compress},
    {"crypto", kStdlibMod_crypto},
    {"csv", kStdlibMod_csv},
    {"date", kStdlibMod_date},
    {"dev", kStdlibMod_dev},
    {"env", kStdlibMod_env},
    {"fs", kStdlibMod_fs},
    {"hash", kStdlibMod_hash},
    {"i18n", kStdlibMod_i18n},
    {"io", kStdlibMod_io},
    {"json", kStdlibMod_json},
    {"log", kStdlibMod_log},
    {"math", kStdlibMod_math},
    {"network", kStdlibMod_network},
    {"perf", kStdlibMod_perf},
    {"proc", kStdlibMod_proc},
    {"random", kStdlibMod_random},
    {"regex", kStdlibMod_regex},
    {"science", kStdlibMod_science},
    {"security", kStdlibMod_security},
    {"test", kStdlibMod_test},
    {"time", kStdlibMod_time},
    {"xml", kStdlibMod_xml},
    {"yaml", kStdlibMod_yaml},
    {"vectors", kStdlibMod_vectors},
    {"matrices", kStdlibMod_matrices},
    {"points", kStdlibMod_points},
    {"rects", kStdlibMod_rects},
    {"quaternions", kStdlibMod_quaternions},
    {"colors", kStdlibMod_colors},
    {"bounds", kStdlibMod_bounds},
    {"transforms", kStdlibMod_transforms},
// === FLAT_MODULE_MAP_END ===
  };
  auto it = map.find(primary);
  return it == map.end() ? nullptr : it->second;
}

static const char* lookupLegacyImportHint(const std::string& path) {
  static const std::unordered_map<std::string, const char*> kHints = {
      {"core.bench", "use import bench"},
      {"core.cli", "use import cli"},
      {"core.compress", "use import compress"},
      {"core.crypto", "use import crypto"},
      {"core.csv", "use import csv"},
      {"core.date", "use import date"},
      {"core.env", "use import env"},
      {"core.fs", "use import fs"},
      {"core.hash", "use import hash"},
      {"core.i18n", "use import i18n"},
      {"core.json", "use import json"},
      {"core.log", "use import log"},
      {"core.math", "use import math"},
      {"core.proc", "use import proc"},
      {"core.random", "use import random"},
      {"core.regex", "use import regex"},
      {"core.test", "use import test"},
      {"core.time", "use import time"},
      {"core.xml", "use import xml"},
      {"core.yaml", "use import yaml"},
      {"io.boundscheck", "use import security"},
      {"io.client", "use import network"},
      {"io.concurrency", "use import security"},
      {"io.console", "use import io"},
      {"io.debug", "use import dev"},
      {"io.deps", "use import dev"},
      {"io.fft", "use import science"},
      {"io.format", "use import dev"},
      {"io.grpc", "use import network"},
      {"io.hotreload", "use import dev"},
      {"io.http", "use import network"},
      {"io.https", "use import network"},
      {"io.immutable", "use import dev"},
      {"io.incremental", "use import perf"},
      {"io.inference", "use import dev"},
      {"io.input", "use import io"},
      {"io.lint", "use import dev"},
      {"io.live", "use import dev"},
      {"io.llvm", "use import perf"},
      {"io.lowmem", "use import perf"},
      {"io.lsp", "use import dev"},
      {"io.memsafe", "use import security"},
      {"io.ml", "use import science"},
      {"io.native", "use import perf"},
      {"io.net", "use import network"},
      {"io.nullable", "use import dev"},
      {"io.numerical", "use import science"},
      {"io.optimization", "use import science"},
      {"io.output", "use import io"},
      {"io.overflow", "use import security"},
      {"io.pack", "use import network"},
      {"io.pattern", "use import dev"},
      {"io.permission", "use import security"},
      {"io.physics", "use import science"},
      {"io.pkg", "use import dev"},
      {"io.predictable", "use import perf"},
      {"io.profile", "use import dev"},
      {"io.readonly", "use import dev"},
      {"io.repl", "use import dev"},
      {"io.rest", "use import network"},
      {"io.rpc", "use import network"},
      {"io.sandbox", "use import security"},
      {"io.secure", "use import security"},
      {"io.serialize", "use import network"},
      {"io.shell", "use import dev"},
      {"io.simd", "use import perf"},
      {"io.startup", "use import perf"},
      {"io.statistics", "use import science"},
      {"io.tcp", "use import network"},
      {"io.terminal", "use import io"},
      {"io.threads", "use import perf"},
      {"io.udp", "use import network"},
      {"io.vectorize", "use import perf"},
      {"io.websocket", "use import network"},
      {"modern", "use import dev"},
      {"net.boundscheck", "use import security"},
      {"net.client", "use import network"},
      {"net.compress", "use import network"},
      {"net.concurrency", "use import security"},
      {"net.console", "use import io"},
      {"net.debug", "use import dev"},
      {"net.deps", "use import dev"},
      {"net.fft", "use import science"},
      {"net.format", "use import dev"},
      {"net.grpc", "use import network"},
      {"net.hotreload", "use import dev"},
      {"net.http", "use import network"},
      {"net.https", "use import network"},
      {"net.immutable", "use import dev"},
      {"net.incremental", "use import perf"},
      {"net.inference", "use import dev"},
      {"net.input", "use import io"},
      {"net.lint", "use import dev"},
      {"net.live", "use import dev"},
      {"net.llvm", "use import perf"},
      {"net.lowmem", "use import perf"},
      {"net.lsp", "use import dev"},
      {"net.memsafe", "use import security"},
      {"net.ml", "use import science"},
      {"net.native", "use import perf"},
      {"net.net", "use import network"},
      {"net.nullable", "use import dev"},
      {"net.numerical", "use import science"},
      {"net.optimization", "use import science"},
      {"net.output", "use import io"},
      {"net.overflow", "use import security"},
      {"net.pack", "use import network"},
      {"net.pattern", "use import dev"},
      {"net.permission", "use import security"},
      {"net.physics", "use import science"},
      {"net.pkg", "use import dev"},
      {"net.predictable", "use import perf"},
      {"net.profile", "use import dev"},
      {"net.readonly", "use import dev"},
      {"net.repl", "use import dev"},
      {"net.rest", "use import network"},
      {"net.rpc", "use import network"},
      {"net.sandbox", "use import security"},
      {"net.secure", "use import security"},
      {"net.serialize", "use import network"},
      {"net.shell", "use import dev"},
      {"net.simd", "use import perf"},
      {"net.startup", "use import perf"},
      {"net.statistics", "use import science"},
      {"net.tcp", "use import network"},
      {"net.terminal", "use import io"},
      {"net.threads", "use import perf"},
      {"net.udp", "use import network"},
      {"net.vectorize", "use import perf"},
      {"net.websocket", "use import network"},
      {"sec.crypto", "use import crypto"},
      {"std.bench", "use import bench"},
      {"std.bounds", "use import bounds"},
      {"std.boundscheck", "use import security"},
      {"std.cli", "use import cli"},
      {"std.client", "use import network"},
      {"std.color", "use import colors"},
      {"std.color32", "use import colors"},
      {"std.compress", "use import compress"},
      {"std.concurrency", "use import security"},
      {"std.console", "use import io"},
      {"std.crypto", "use import crypto"},
      {"std.csv", "use import csv"},
      {"std.date", "use import date"},
      {"std.debug", "use import dev"},
      {"std.deps", "use import dev"},
      {"std.dev.debug", "use import dev"},
      {"std.dev.deps", "use import dev"},
      {"std.dev.format", "use import dev"},
      {"std.dev.hotreload", "use import dev"},
      {"std.dev.immutable", "use import dev"},
      {"std.dev.inference", "use import dev"},
      {"std.dev.lint", "use import dev"},
      {"std.dev.live", "use import dev"},
      {"std.dev.lsp", "use import dev"},
      {"std.dev.nullable", "use import dev"},
      {"std.dev.pattern", "use import dev"},
      {"std.dev.pkg", "use import dev"},
      {"std.dev.profile", "use import dev"},
      {"std.dev.readonly", "use import dev"},
      {"std.dev.repl", "use import dev"},
      {"std.dev.shell", "use import dev"},
      {"std.dmat2", "use import matrices"},
      {"std.dmat3", "use import matrices"},
      {"std.dmat4", "use import matrices"},
      {"std.dpoint", "use import points"},
      {"std.dquat", "use import quaternions"},
      {"std.drect", "use import rects"},
      {"std.dvec2", "use import vectors"},
      {"std.dvec3", "use import vectors"},
      {"std.dvec4", "use import vectors"},
      {"std.env", "use import env"},
      {"std.fft", "use import science"},
      {"std.format", "use import dev"},
      {"std.fs", "use import fs"},
      {"std.grpc", "use import network"},
      {"std.hash", "use import hash"},
      {"std.hotreload", "use import dev"},
      {"std.http", "use import network"},
      {"std.https", "use import network"},
      {"std.i18n", "use import i18n"},
      {"std.immutable", "use import dev"},
      {"std.incremental", "use import perf"},
      {"std.inference", "use import dev"},
      {"std.input", "use import io"},
      {"std.io.console", "use import io"},
      {"std.io.input", "use import io"},
      {"std.io.output", "use import io"},
      {"std.io.terminal", "use import io"},
      {"std.ivec2", "use import vectors"},
      {"std.ivec3", "use import vectors"},
      {"std.ivec4", "use import vectors"},
      {"std.json", "use import json"},
      {"std.linalg", "use import vectors"},
      {"std.lint", "use import dev"},
      {"std.live", "use import dev"},
      {"std.llvm", "use import perf"},
      {"std.log", "use import log"},
      {"std.lowmem", "use import perf"},
      {"std.lsp", "use import dev"},
      {"std.mat2", "use import matrices"},
      {"std.mat3", "use import matrices"},
      {"std.mat4", "use import matrices"},
      {"std.math", "use import math"},
      {"std.matrices", "use import matrices"},
      {"std.memsafe", "use import security"},
      {"std.ml", "use import science"},
      {"std.modern", "use import dev"},
      {"std.native", "use import perf"},
      {"std.net", "use import network"},
      {"std.net.compress", "use import network"},
      {"std.network.client", "use import network"},
      {"std.network.grpc", "use import network"},
      {"std.network.http", "use import network"},
      {"std.network.https", "use import network"},
      {"std.network.net", "use import network"},
      {"std.network.pack", "use import network"},
      {"std.network.rest", "use import network"},
      {"std.network.rpc", "use import network"},
      {"std.network.serialize", "use import network"},
      {"std.network.tcp", "use import network"},
      {"std.network.udp", "use import network"},
      {"std.network.websocket", "use import network"},
      {"std.nullable", "use import dev"},
      {"std.numerical", "use import science"},
      {"std.optimization", "use import science"},
      {"std.output", "use import io"},
      {"std.overflow", "use import security"},
      {"std.pack", "use import network"},
      {"std.pattern", "use import dev"},
      {"std.perf.incremental", "use import perf"},
      {"std.perf.llvm", "use import perf"},
      {"std.perf.lowmem", "use import perf"},
      {"std.perf.native", "use import perf"},
      {"std.perf.predictable", "use import perf"},
      {"std.perf.simd", "use import perf"},
      {"std.perf.startup", "use import perf"},
      {"std.perf.threads", "use import perf"},
      {"std.perf.vectorize", "use import perf"},
      {"std.permission", "use import security"},
      {"std.physics", "use import science"},
      {"std.pkg", "use import dev"},
      {"std.point", "use import points"},
      {"std.predictable", "use import perf"},
      {"std.proc", "use import proc"},
      {"std.profile", "use import dev"},
      {"std.quat", "use import quaternions"},
      {"std.random", "use import random"},
      {"std.readonly", "use import dev"},
      {"std.rect", "use import rects"},
      {"std.regex", "use import regex"},
      {"std.repl", "use import dev"},
      {"std.rest", "use import network"},
      {"std.rpc", "use import network"},
      {"std.sandbox", "use import security"},
      {"std.science.fft", "use import science"},
      {"std.science.ml", "use import science"},
      {"std.science.numerical", "use import science"},
      {"std.science.optimization", "use import science"},
      {"std.science.physics", "use import science"},
      {"std.science.statistics", "use import science"},
      {"std.sec.crypto", "use import crypto"},
      {"std.secure", "use import security"},
      {"std.security.boundscheck", "use import security"},
      {"std.security.concurrency", "use import security"},
      {"std.security.memsafe", "use import security"},
      {"std.security.overflow", "use import security"},
      {"std.security.permission", "use import security"},
      {"std.security.sandbox", "use import security"},
      {"std.security.secure", "use import security"},
      {"std.serialize", "use import network"},
      {"std.shell", "use import dev"},
      {"std.simd", "use import perf"},
      {"std.startup", "use import perf"},
      {"std.statistics", "use import science"},
      {"std.tcp", "use import network"},
      {"std.terminal", "use import io"},
      {"std.test", "use import test"},
      {"std.threads", "use import perf"},
      {"std.time", "use import time"},
      {"std.transform", "use import transforms"},
      {"std.udp", "use import network"},
      {"std.vec2", "use import vectors"},
      {"std.vec3", "use import vectors"},
      {"std.vec4", "use import vectors"},
      {"std.vectorize", "use import perf"},
      {"std.vectors", "use import vectors"},
      {"std.websocket", "use import network"},
      {"std.xml", "use import xml"},
      {"std.yaml", "use import yaml"},
  };
  auto it = kHints.find(path);
  return it == kHints.end() ? nullptr : it->second;
}

static void rejectLegacyStdlibImport(const std::string& import_path) {
  if (const char* hint = lookupLegacyImportHint(import_path)) {
    throw FarError(std::string("module '") + import_path + "' was removed; " + hint);
  }
  if (import_path.rfind("std.", 0) == 0 || import_path.rfind("core.", 0) == 0 ||
      import_path.rfind("io.", 0) == 0 || import_path.rfind("net.", 0) == 0 ||
      import_path.rfind("sci.", 0) == 0 || import_path.rfind("modern.", 0) == 0 ||
      import_path.rfind("dev.", 0) == 0 ||
      import_path.rfind("sec.", 0) == 0 || import_path.rfind("perf.", 0) == 0) {
    throw FarError("legacy import '" + import_path +
                   "' is not supported; use Python-style import (e.g. import math, import vectors, "
                   "import network)");
  }
}

static std::string normalizeStdlibImport(const std::string& import_path) {
  rejectLegacyStdlibImport(import_path);
  if (import_path.find('.') != std::string::npos) return "";
  if (import_path.empty()) return "";
  return import_path;
}

const char* lookupStdlibModuleSource(const std::string& import_path) {
  std::string primary = normalizeStdlibImport(import_path);
  if (primary.empty()) return nullptr;
  return primaryStdlibModuleSource(primary);
}

bool isStdlibModuleImport(const std::string& import_path) {
  return lookupStdlibModuleSource(import_path) != nullptr;
}

static const std::unordered_set<std::string>& functionStdlibModules() {
  static const std::unordered_set<std::string> k = {
      "bench", "cli", "compress", "crypto", "csv", "date", "env", "fs", "hash", "i18n", "json",
      "log",  "math", "net",  "proc",  "random", "regex", "test", "time", "xml", "yaml",
  };
  return k;
}

static const std::unordered_set<std::string>& typeStdlibModules() {
  static const std::unordered_set<std::string> k = {
      "vectors",   "matrices", "points",     "rects",   "quaternions", "colors", "bounds",
      "transforms", "network",  "io",        "science", "security",    "perf",   "dev",
  };
  return k;
}

bool isStdlibFunctionModule(const std::string& flat_name) {
  return functionStdlibModules().count(flat_name) > 0;
}

bool isStdlibTypeModule(const std::string& flat_name) {
  return typeStdlibModules().count(flat_name) > 0;
}

std::string stdlibModuleFlatName(const std::string& module_full_name) {
  const size_t dot = module_full_name.rfind('.');
  return dot == std::string::npos ? module_full_name : module_full_name.substr(dot + 1);
}

}  // namespace far
