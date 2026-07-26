#pragma once

#include <cstddef>
#include <fstream>
#include <ios>
#include <string>

#include "chronicle/tracked.hpp"

// docs/13-vision.md Layer 9 ("runtime knowledge graph"), docs/adr/0037-filesystem-bridge.md.
//
// Layer 9 asks for network/GPU/filesystem/coroutine activity as
// Chronicle streams, achieved by *bridging* to real systems (the way the
// Tracy and Perfetto bridges already do), never by Chronicle
// reimplementing an APM platform. This is the one bridge actually built
// and verified this cycle: filesystem I/O, chosen specifically because
// it needs no external dependency and no hardware/OS feature this
// project's CI matrix can't also have (unlike GPU or network activity,
// which would need a real GPU/network stack to verify against honestly
// -- not attempted here, left as real, separately-scoped future work per
// the ADR).
//
// Built on the same primitive as everything else: `tracked<FileOp>`. Each
// real file operation (open/read/write/close) is recorded as one event,
// so `history(file.activity())` gives the file's full operation log for
// free, with no new recording mechanism.

namespace chronicle::bridges {

enum class FileOpKind {
    Open,
    Read,
    Write,
    Close,
};

struct FileOp {
    FileOpKind kind = FileOpKind::Open;
    std::string path;
    std::size_t byte_count = 0; // 0 for Open/Close, real byte count for Read/Write
};

// Wraps a real std::fstream. Not a drop-in fstream replacement (no
// operator<</operator>>, no seek/tell) -- a deliberately small surface
// covering exactly the operations that make sense as discrete, countable
// events (open, read N bytes, write N bytes, close), matching this
// project's "small closed vocabulary" preference elsewhere (wire.hpp's
// WireKind).
class TrackedFile {
public:
    // activity_ is seeded with the real Open event *before* track() runs,
    // not recorded via a separate chronicle::set() call afterward --
    // track() itself already records a field's current value as version 0
    // (same rationale tracked<T>::track() documents: history() should
    // never start empty). A second explicit set() call here would have
    // recorded the Open event twice -- a real duplicate-event bug found
    // by actually running this test, not by inspection.
    TrackedFile(Session& session, std::string const& name, std::string path, std::ios::openmode mode)
        : activity_(FileOp{FileOpKind::Open, path, 0}), file_(path, mode) {
        track(activity_, session, name);
    }

    TrackedFile(TrackedFile const&) = delete;
    TrackedFile& operator=(TrackedFile const&) = delete;

    [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }

    std::size_t read(char* buffer, std::size_t count) {
        file_.read(buffer, static_cast<std::streamsize>(count));
        auto const actually_read = static_cast<std::size_t>(file_.gcount());
        chronicle::set(activity_, FileOp{FileOpKind::Read, activity_.get().path, actually_read});
        return actually_read;
    }

    void write(char const* buffer, std::size_t count) {
        file_.write(buffer, static_cast<std::streamsize>(count));
        chronicle::set(activity_, FileOp{FileOpKind::Write, activity_.get().path, count});
    }

    void close() {
        if (file_.is_open()) {
            file_.close();
            chronicle::set(activity_, FileOp{FileOpKind::Close, activity_.get().path, 0});
        }
    }

    ~TrackedFile() { close(); }

    // history(file.activity())/last_writer(file.activity())/etc. all just
    // work -- this is an ordinary tracked<FileOp> underneath.
    [[nodiscard]] tracked<FileOp> const& activity() const noexcept { return activity_; }

private:
    tracked<FileOp> activity_;
    std::fstream file_;
};

} // namespace chronicle::bridges
