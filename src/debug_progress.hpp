/**
 * @file debug_progress.hpp
 * @brief Optional debug output for local development (off in Release builds).
 */

#pragma once

#include <iostream>
#include <utility>

#include "evaluation.hpp"

namespace cadd0040 {

/**
 * @brief Controls optional stderr debug output.
 *
 * Release builds (NDEBUG) are always disabled.
 * Debug builds honor CADD0040_DEBUG_PROGRESS=1 and CADD0040_DEBUG_PROGRESS_INTERVAL.
 */
class DebugProgress {
public:
    static DebugProgress from_environment();

    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] double interval_seconds() const { return interval_seconds_; }

    template <typename WriteFn>
    void log(WriteFn&& write) const {
        if (!enabled_) {
            return;
        }
        write(std::cerr);
    }

    bool report_if_due(double elapsed_seconds, double best_score, double current_score);

    bool report_if_due(double elapsed_seconds, const Metrics& best_metrics, const Metrics& baseline,
                       double current_score);

private:
    DebugProgress(bool enabled, double interval_seconds);

    bool enabled_ = false;
    double interval_seconds_ = 30.0;
    double last_report_elapsed_ = -1.0;
};

}  // namespace cadd0040
