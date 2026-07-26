#pragma once

#include <cstddef>
#include <ostream>
#include <sstream>
#include <vector>

#include "chronicle/derived.hpp"
#include "chronicle/io/format.hpp"
#include "chronicle/io/wire.hpp"
#include "chronicle/provenance.hpp"
#include "chronicle/session.hpp"
#include "chronicle/tracked.hpp"
#include "chronicle/tracked_map.hpp"
#include "chronicle/tracked_vector.hpp"

// Writes tracked fields to the on-disk .chronicle format
// (docs/06-recording-model.md). Deliberately NOT a Session::save_all() or a
// StreamBase virtual method: see
// docs/adr/0008-cli-avoids-streambase-virtual-dispatch.md for why. Short
// version: making this virtual would force the compiler to instantiate
// write_wire's body for every Stream<T> the moment T is used at all (C++'s
// rules for virtual members of class templates), which either hard-fails to
// compile for any T without a WireCodec or (depending on the compiler)
// produces a link error -- either way, breaking `tracked<T>` for types
// nobody ever tries to serialize. A plain function template has none of
// that risk: write<T>() is only ever instantiated for the T the caller
// actually passes to it.
//
// Format: magic + version header, then a sequence of stream blocks running
// to EOF (no leading stream count -- keeps the writer single-pass and each
// block self-contained/"chunked" per docs/06's format goal). Each block:
// name, shape tag, event count, then events.
//
// Optional compression (docs/adr/0014-storage-engine-compression.md): if
// constructed with a CompressionCodec, every write<T>() call goes to an
// internal buffer instead of `os` directly, and the destructor compresses
// the *entire* buffered payload as one blob and writes
// `[compressed_size][compressed bytes]` to `os` after the header -- see the
// ADR for why whole-payload, not per-stream/per-event. Passing no codec
// (the default) writes exactly as before this feature existed: directly to
// `os`, no buffering, no new dependency, byte-identical to pre-v3 writers
// modulo the one new header byte.

namespace chronicle::io {

class SessionWriter {
public:
    explicit SessionWriter(std::ostream& os, Session const& session,
                            CompressionCodec const* codec = nullptr)
        : os_(os), start_(session.start()), codec_(codec) {
        write_header(os_, codec_ == nullptr ? CompressionKind::None : codec_->kind);
    }

    // Not copyable (references an ostream and, when compressing, owns a
    // buffer the destructor depends on) and not meant to be moved either --
    // construct one per file, write everything, let it go out of scope.
    SessionWriter(SessionWriter const&) = delete;
    SessionWriter& operator=(SessionWriter const&) = delete;

    // Writes the v5 extended-sections terminator + provenance/derivation
    // tables (always, even when both are empty -- every v5 file has the
    // same shape), then -- unchanged from before -- compresses (if a
    // codec was supplied) and flushes the buffered payload to `os`. Same
    // convention as the rest of this file: no exceptions escape a
    // destructor, so a write failure here surfaces the same way every
    // other write in this class already does -- via `os`'s own failbit,
    // checkable by the caller through their own reference to it.
    ~SessionWriter() {
        auto& out = target();
        write_u64(out, kExtendedSectionsMarker);
        write_u64(out, pending_provenance_.size());
        for (auto const& entry : pending_provenance_) {
            write_provenance_entry(out, entry);
        }
        write_u64(out, pending_derivation_.size());
        for (auto const& entry : pending_derivation_) {
            write_derivation_entry(out, entry);
        }

        if (codec_ == nullptr) {
            return; // wrote directly to os_ all along; nothing to flush
        }
        std::string const compressed = codec_->compress(buffer_.str());
        write_u64(os_, compressed.size());
        os_.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
    }

    template <typename T>
    void write(tracked<T> const& field) {
        auto* stream = field.stream();
        if (stream == nullptr) {
            return; // untracked field: nothing recorded, nothing to save
        }
        auto& out = target();
        write_string(out, stream->name());
        write_u8(out, static_cast<std::uint8_t>(StreamShape::Scalar));
        auto const hx = stream->history();
        write_u64(out, hx.size());
        for (auto const& record : hx) {
            write_event_header(out, record.version, record.timestamp, record.thread_id, record.call_site,
                                record.hlc);
            WireCodec<T>::write(out, record.value);
        }
    }

    template <typename T>
    void write(tracked_vector<T> const& field) {
        auto* stream = field.stream();
        if (stream == nullptr) {
            return;
        }
        auto& out = target();
        write_string(out, stream->name());
        write_u8(out, static_cast<std::uint8_t>(StreamShape::IndexedOp));
        auto const hx = stream->history();
        write_u64(out, hx.size());
        for (auto const& record : hx) {
            write_event_header(out, record.version, record.timestamp, record.thread_id, record.call_site,
                                record.hlc);
            write_u8(out, static_cast<std::uint8_t>(record.value.kind));
            WireCodec<std::size_t>::write(out, record.value.index);
            WireCodec<T>::write(out, record.value.value);
        }
    }

    template <typename K, typename V>
    void write(tracked_map<K, V> const& field) {
        auto* stream = field.stream();
        if (stream == nullptr) {
            return;
        }
        auto& out = target();
        write_string(out, stream->name());
        write_u8(out, static_cast<std::uint8_t>(StreamShape::KeyedOp));
        auto const hx = stream->history();
        write_u64(out, hx.size());
        for (auto const& record : hx) {
            write_event_header(out, record.version, record.timestamp, record.thread_id, record.call_site,
                                record.hlc);
            write_u8(out, static_cast<std::uint8_t>(record.value.kind));
            WireCodec<K>::write(out, record.value.key);
            WireCodec<V>::write(out, record.value.value);
        }
    }

    // docs/adr/0039-persist-provenance-and-derivation.md: explicit and
    // opt-in, same as write<T>() itself -- calling this is how a caller
    // says "yes, persist this field's captured call chains," not
    // something write<T>() does automatically (most fields never call
    // chronicle::set_with_stacktrace() at all, so there'd be nothing to
    // persist for them anyway). Walks the field's own history and looks
    // up each version in the in-process provenance::Registry (keyed by
    // Stream<T>::id(), ADR 0032), translating to the field's on-disk
    // stream name -- the identifier that actually survives a save/load
    // round trip. Buffered, not written immediately: the extended
    // sections must come after every stream block, regardless of what
    // order the caller calls write()/write_provenance()/write_derived()
    // in.
    template <typename T>
    void write_provenance(tracked<T> const& field) {
        auto* stream = field.stream();
        if (stream == nullptr) {
            return;
        }
        for (auto const& record : stream->history()) {
            auto trace = provenance::Registry::instance().find(stream->id(), record.version);
            if (trace.has_value()) {
                pending_provenance_.push_back(ProvenanceEntry{stream->name(), record.version, std::move(*trace)});
            }
        }
    }

    // Same shape as write_provenance() above, against
    // chronicle::derived::Registry (ADR 0033). Called on the *target* of
    // a derive() binding -- the field a DependencyChange explanation is
    // actually keyed on -- not on any of its dependencies.
    template <typename T>
    void write_derived(tracked<T> const& target_field) {
        auto* stream = target_field.stream();
        if (stream == nullptr) {
            return;
        }
        for (auto const& record : stream->history()) {
            auto changes = derived::Registry::instance().find(stream->id(), record.version);
            if (changes.has_value()) {
                pending_derivation_.push_back(
                    DerivationEntry{stream->name(), record.version, std::move(*changes)});
            }
        }
    }

private:
    // Every write<T>() overload goes through here rather than touching os_
    // directly, so compression is a one-line difference (which stream) not
    // a duplicated branch in every overload.
    std::ostream& target() { return codec_ == nullptr ? os_ : static_cast<std::ostream&>(buffer_); }

    void write_event_header(std::ostream& out, std::uint64_t version,
                             std::chrono::steady_clock::time_point timestamp, std::thread::id thread_id,
                             std::source_location const& call_site, HlcTimestamp const& hlc) {
        write_u64(out, version);
        write_i64(out, elapsed_ns(timestamp, start_));
        write_u64(out, thread_hash(thread_id));
        write_call_site(out, call_site);
        write_hlc(out, hlc);
    }

    std::ostream& os_;
    std::chrono::steady_clock::time_point start_;
    CompressionCodec const* codec_;
    std::ostringstream buffer_; // only written to/read from when codec_ != nullptr
    std::vector<ProvenanceEntry> pending_provenance_;
    std::vector<DerivationEntry> pending_derivation_;
};

} // namespace chronicle::io
