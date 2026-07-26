#pragma once

#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "chronicle/container_op.hpp"
#include "chronicle/timeline.hpp"

// docs/12-future-research-topics.md topic 8, docs/adr/0026-anomaly-detection.md:
// "flag this field's value is outside its historically observed range/rate
// of change." "Machine-assisted" here means real, honest statistical
// scoring (an online z-score against the field's own running mean/stddev),
// not a trained ML model -- the same honesty bar
// docs/04-technical-limitations.md holds every other feature to: never
// claim more than the mechanism actually does.
//
// Deliberately causal/online, not a whole-history batch computation: each
// event is scored only against the mean/stddev of events *before* it, the
// same way a live monitor watching a running program would see it. Scoring
// event i using statistics that include event i (or events after it) would
// be hindsight bias -- an anomaly at position 0 could never look anomalous
// if it's averaged in with the "normal" values that surround it in a
// full-history computation, but a live monitor never gets to see the
// future.

namespace chronicle {

template <typename T>
struct RangeAnomaly {
    std::size_t index = 0;  // position within the Timeline<T> passed in
    T value{};
    double running_mean = 0.0;
    double z_score = 0.0; // (value - running_mean) / running_stddev at the time of this event
};

// Welford's online algorithm: numerically stable running mean/variance
// without storing the whole history twice or re-summing on every call --
// standard, well-understood, not a novel statistical claim.
template <typename T>
[[nodiscard]] std::vector<RangeAnomaly<T>> range_anomalies(Timeline<T> const& history,
                                                            double z_threshold = 3.0,
                                                            std::size_t min_samples = 3) {
    static_assert(std::is_arithmetic_v<T>,
                  "range_anomalies() needs a running mean/stddev, which only makes sense for "
                  "arithmetic tracked<T> fields -- see docs/adr/0026 for why strings/structs "
                  "aren't scored this way.");
    std::vector<RangeAnomaly<T>> result;
    double mean = 0.0;
    double m2 = 0.0; // sum of squared deviations from the running mean (Welford)
    std::size_t n = 0;

    for (std::size_t i = 0; i < history.size(); ++i) {
        double const value = static_cast<double>(history[i].value);

        if (n >= min_samples) {
            double const variance = m2 / static_cast<double>(n);
            double const stddev = std::sqrt(variance);
            if (stddev > 0.0) {
                double const z = (value - mean) / stddev;
                if (std::fabs(z) >= z_threshold) {
                    result.push_back(RangeAnomaly<T>{i, history[i].value, mean, z});
                }
            } else if (value != mean) {
                // Every prior sample was identical (zero observed variance)
                // and this one differs: a real, unambiguous "outside its
                // historically observed range" case that a z-score can't
                // express (division by zero), not a missed detection.
                result.push_back(RangeAnomaly<T>{i, history[i].value, mean, 0.0});
            }
        }

        // Update running statistics with this event *after* scoring it,
        // so event i is always judged against strictly-prior history.
        ++n;
        double const delta = value - mean;
        mean += delta / static_cast<double>(n);
        double const delta2 = value - mean;
        m2 += delta * delta2;
    }
    return result;
}

// docs/13-vision.md Layer 8's own example: "size always grows, never
// shrinks... possible leak." A structural (shape) anomaly over a
// tracked_vector<T>'s ContainerOp<T> history, complementing
// range_anomalies() above (which only makes sense for a single numeric
// value, not a container's size). Real replay of Insert/Erase/Clear ops
// into a running size counter -- the same fold `chronicle::apply()`
// (container_op.hpp) already does for full materialization, just
// tracking size instead of full contents, since size is all a growth
// check needs.
template <typename T>
struct ContainerGrowthReport {
    std::size_t max_size = 0;
    std::size_t final_size = 0;
    std::size_t insert_count = 0;
    std::size_t erase_or_clear_count = 0;
    bool ever_shrunk = false; // true if any Erase/Clear was ever recorded
};

template <typename T>
[[nodiscard]] ContainerGrowthReport<T> container_growth_report(Timeline<ContainerOp<T>> const& history) {
    ContainerGrowthReport<T> report;
    std::size_t size = 0;
    for (auto const& record : history) {
        switch (record.value.kind) {
            case ContainerOpKind::Insert:
                ++size;
                ++report.insert_count;
                break;
            case ContainerOpKind::Erase:
                if (size > 0) {
                    --size;
                }
                ++report.erase_or_clear_count;
                report.ever_shrunk = true;
                break;
            case ContainerOpKind::Update:
                break; // size-neutral
            case ContainerOpKind::Clear:
                if (size > 0) {
                    report.ever_shrunk = true;
                }
                size = 0;
                ++report.erase_or_clear_count;
                break;
        }
        report.max_size = size > report.max_size ? size : report.max_size;
    }
    report.final_size = size;
    return report;
}

// A named, explicit heuristic, not a hidden threshold buried in the
// report struct: "never observed to shrink, and currently holds at least
// `min_size_to_flag` elements" -- real risk of false positives for a
// container that simply hasn't been erased from *yet* (a genuine leak
// looks identical to "still warming up" from this data alone), stated
// honestly rather than papered over with confident-sounding language.
template <typename T>
[[nodiscard]] bool is_likely_leak(ContainerGrowthReport<T> const& report, std::size_t min_size_to_flag = 10) {
    return !report.ever_shrunk && report.final_size >= min_size_to_flag;
}

} // namespace chronicle
