/**
 * @file debug_progress.cpp
 * @brief Debug progress reporting implementation.
 */

#include "debug_progress.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace cadd0040 {
namespace {

constexpr double kDefaultIntervalSeconds = 30.0;

bool should_emit_report(const DebugProgress& reporter, double elapsed_seconds) {
    if (!reporter.enabled()) {
        return false;
    }

    if (elapsed_seconds < reporter.interval_seconds()) {
        return false;
    }

    return true;
}

}  // namespace

DebugProgress::DebugProgress(bool enabled, double interval_seconds)
    : enabled_(enabled), interval_seconds_(interval_seconds) {}

DebugProgress DebugProgress::from_debug_flag(bool enabled) {
#ifdef NDEBUG
    (void)enabled;
    return DebugProgress(false, kDefaultIntervalSeconds);
#else
    return DebugProgress(enabled, kDefaultIntervalSeconds);
#endif
}

bool DebugProgress::report_if_due(double elapsed_seconds, double best_score, double current_score) {
    if (!should_emit_report(*this, elapsed_seconds)) {
        return false;
    }

    if (last_report_elapsed_ >= 0.0 && elapsed_seconds - last_report_elapsed_ < interval_seconds_) {
        return false;
    }

    last_report_elapsed_ = elapsed_seconds;

    std::cerr << "Progress elapsed=" << elapsed_seconds << "s"
              << " best_score=" << best_score;

    if (!std::isnan(current_score)) {
        std::cerr << " current_score=" << current_score;
    }

    std::cerr << '\n';
    return true;
}

bool DebugProgress::report_if_due(double elapsed_seconds, const Metrics& best_metrics,
                                  const Metrics& baseline, double current_score) {
    if (!should_emit_report(*this, elapsed_seconds)) {
        return false;
    }

    if (last_report_elapsed_ >= 0.0 && elapsed_seconds - last_report_elapsed_ < interval_seconds_) {
        return false;
    }

    last_report_elapsed_ = elapsed_seconds;

    const double best_score = score(best_metrics, baseline);

    std::cerr << "Progress elapsed=" << elapsed_seconds << "s"
              << " best_score=" << best_score << " best_metrics=" << best_metrics;

    if (!std::isnan(current_score)) {
        std::cerr << " current_score=" << current_score;
    }

    std::cerr << '\n';
    return true;
}

}  // namespace cadd0040
