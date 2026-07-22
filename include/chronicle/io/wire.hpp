#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <type_traits>

// docs/06-recording-model.md's on-disk format, v0.2 scope: a small, closed
// vocabulary of "wire" value kinds -- matching how real trace formats
// (Perfetto, Tracy) keep their wire schema small and push domain-specific
// meaning onto the producer, not the format. T is converted to/from a
// WireValue via WireCodec<T>; a T with no WireCodec specialization simply
// can't be serialized -- a compile error exactly at the call site that
// tries (see io/session_writer.hpp), not a constraint on tracked<T> itself.
// Most of chronicle-core has no idea this file exists.
//
// KNOWN LIMITATION: fixed-width fields are written in the host's native
// byte order -- no endian conversion. Every platform in docs/11's CI matrix
// (x86-64, ARM64) is little-endian in practice, so this is a deliberate,
// documented v0.2 simplification, not an oversight. Revisit if a
// big-endian target ever matters.

namespace chronicle::io {

enum class WireKind : std::uint8_t {
    Int64 = 0,
    UInt64 = 1,
    Double = 2,
    Bool = 3,
    String = 4,
};

struct WireValue {
    WireKind kind = WireKind::Int64;
    std::int64_t i = 0;
    std::uint64_t u = 0;
    double d = 0.0;
    bool b = false;
    std::string s;
};

[[nodiscard]] inline bool operator==(WireValue const& a, WireValue const& b) {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
        case WireKind::Int64: return a.i == b.i;
        case WireKind::UInt64: return a.u == b.u;
        case WireKind::Double: return a.d == b.d;
        case WireKind::Bool: return a.b == b.b;
        case WireKind::String: return a.s == b.s;
    }
    return false;
}
[[nodiscard]] inline bool operator!=(WireValue const& a, WireValue const& b) { return !(a == b); }

[[nodiscard]] inline std::string to_display_string(WireValue const& v) {
    switch (v.kind) {
        case WireKind::Int64: return std::to_string(v.i);
        case WireKind::UInt64: return std::to_string(v.u);
        case WireKind::Double: return std::to_string(v.d);
        case WireKind::Bool: return v.b ? "true" : "false";
        case WireKind::String: return "\"" + v.s + "\"";
    }
    return "<?>";
}

// -- low-level primitives --

inline void write_u8(std::ostream& os, std::uint8_t v) { os.put(static_cast<char>(v)); }
inline std::uint8_t read_u8(std::istream& is) {
    char c = 0;
    is.get(c);
    return static_cast<std::uint8_t>(c);
}

inline void write_u64(std::ostream& os, std::uint64_t v) {
    os.write(reinterpret_cast<char const*>(&v), sizeof(v));
}
inline std::uint64_t read_u64(std::istream& is) {
    std::uint64_t v = 0;
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}

inline void write_i64(std::ostream& os, std::int64_t v) {
    os.write(reinterpret_cast<char const*>(&v), sizeof(v));
}
inline std::int64_t read_i64(std::istream& is) {
    std::int64_t v = 0;
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}

inline void write_f64(std::ostream& os, double v) {
    os.write(reinterpret_cast<char const*>(&v), sizeof(v));
}
inline double read_f64(std::istream& is) {
    double v = 0;
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}

inline void write_string(std::ostream& os, std::string const& s) {
    write_u64(os, s.size());
    os.write(s.data(), static_cast<std::streamsize>(s.size()));
}
inline std::string read_string(std::istream& is) {
    auto const len = read_u64(is);
    std::string s(len, '\0');
    is.read(s.data(), static_cast<std::streamsize>(len));
    return s;
}

inline void write_wire_value(std::ostream& os, WireValue const& v) {
    write_u8(os, static_cast<std::uint8_t>(v.kind));
    switch (v.kind) {
        case WireKind::Int64: write_i64(os, v.i); break;
        case WireKind::UInt64: write_u64(os, v.u); break;
        case WireKind::Double: write_f64(os, v.d); break;
        case WireKind::Bool: write_u8(os, v.b ? 1 : 0); break;
        case WireKind::String: write_string(os, v.s); break;
    }
}

inline WireValue read_wire_value(std::istream& is) {
    WireValue v;
    v.kind = static_cast<WireKind>(read_u8(is));
    switch (v.kind) {
        case WireKind::Int64: v.i = read_i64(is); break;
        case WireKind::UInt64: v.u = read_u64(is); break;
        case WireKind::Double: v.d = read_f64(is); break;
        case WireKind::Bool: v.b = read_u8(is) != 0; break;
        case WireKind::String: v.s = read_string(is); break;
    }
    return v;
}

// -- WireCodec<T>: converts a leaf value type to/from WireValue --
// No generic definition: T must be arithmetic, bool, or std::string.
// Anything else is a compile error at the point something tries to
// serialize it -- see the design note in io/session_writer.hpp.

template <typename T, typename = void>
struct WireCodec;

template <typename T>
struct WireCodec<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
    static void write(std::ostream& os, T const& value) {
        WireValue wv;
        if constexpr (std::is_same_v<T, bool>) {
            wv.kind = WireKind::Bool;
            wv.b = value;
        } else if constexpr (std::is_floating_point_v<T>) {
            wv.kind = WireKind::Double;
            wv.d = static_cast<double>(value);
        } else if constexpr (std::is_signed_v<T>) {
            wv.kind = WireKind::Int64;
            wv.i = static_cast<std::int64_t>(value);
        } else {
            wv.kind = WireKind::UInt64;
            wv.u = static_cast<std::uint64_t>(value);
        }
        write_wire_value(os, wv);
    }
    static T read(std::istream& is) {
        WireValue const wv = read_wire_value(is);
        if constexpr (std::is_same_v<T, bool>) {
            return wv.b;
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(wv.d);
        } else if constexpr (std::is_signed_v<T>) {
            return static_cast<T>(wv.i);
        } else {
            return static_cast<T>(wv.u);
        }
    }
};

template <>
struct WireCodec<std::string> {
    static void write(std::ostream& os, std::string const& value) {
        WireValue wv;
        wv.kind = WireKind::String;
        wv.s = value;
        write_wire_value(os, wv);
    }
    static std::string read(std::istream& is) {
        WireValue const wv = read_wire_value(is);
        return wv.s;
    }
};

} // namespace chronicle::io
