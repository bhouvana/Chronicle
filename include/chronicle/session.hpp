#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "chronicle/fwd.hpp"
#include "chronicle/retention.hpp"

// docs/05-architecture.md: a Session "owns a group of StateStreams and their
// shared retention/storage policy. RAII-scoped." docs/07-api-design.md's
// `chronicle::session(...)` sketch is realized here as a concrete class
// rather than a free function returning an opaque handle, since v0.1 has no
// need yet to hide the type behind a factory (nothing polymorphic to hide).

namespace chronicle {

// Non-template base every Stream<T> implements, so Session can own and drain
// heterogeneous stream types without the core needing to know what T is --
// the type-erasure boundary docs/05 calls for, applied only where ownership
// bookkeeping actually needs it (never on the hot record() path, which stays
// fully templated/inlinable per docs/09's zero-cost requirement).
class StreamBase {
public:
    virtual ~StreamBase() = default;
    virtual void drain() = 0;
    [[nodiscard]] virtual std::string const& name() const noexcept = 0;
};

class Session {
public:
    struct Config {
        RetentionPolicy retention = RetentionPolicy::ring_window(1024);
        OverflowPolicy overflow = OverflowPolicy::DropOldest;
        std::size_t snapshot_interval = 64;
    };

    Session() : Session(Config{}) {}
    explicit Session(Config config) : config_(config), start_(std::chrono::steady_clock::now()) {}

    Session(Session const&) = delete;
    Session& operator=(Session const&) = delete;

    [[nodiscard]] Config const& config() const noexcept { return config_; }

    // Reference point for on-disk timestamps (chronicle/io/session_writer.hpp
    // writes events as elapsed time since this, not absolute time -- a
    // steady_clock::time_point's raw value is meaningless outside the
    // process that produced it, so persisting it directly would be no more
    // portable than persisting a raw pointer).
    [[nodiscard]] std::chrono::steady_clock::time_point start() const noexcept { return start_; }

    // Defined out-of-line in stream.hpp, once Stream<T> is a complete type.
    template <typename T>
    Stream<T>& create_stream(std::string name);

    // Drains every stream owned by this session (see docs/rfc/0001's
    // "manual drain, decoupled from concurrency complexity" decision).
    void drain_all() {
        for (auto& stream : streams_) {
            stream->drain();
        }
    }

    [[nodiscard]] std::size_t stream_count() const noexcept { return streams_.size(); }

private:
    Config config_;
    std::chrono::steady_clock::time_point start_;
    std::vector<std::unique_ptr<StreamBase>> streams_;
};

} // namespace chronicle
