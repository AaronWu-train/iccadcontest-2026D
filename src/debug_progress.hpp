/**
 * @file debug_progress.hpp
 * @brief Optional periodic best-solution reporting for local debugging.
 */

#pragma once

#include "evaluation.hpp"

namespace cadd0040 {

/**
 * @brief Emits periodic progress lines to stderr when enabled via environment.
 *
 * Enable with CADD0040_DEBUG_PROGRESS=1 (competition evaluators do not set this).
 * Interval defaults to 30 seconds; override with CADD0040_DEBUG_PROGRESS_INTERVAL.
 */
class DebugProgress {
public:
    static DebugProgress from_environment();

    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] double interval_seconds() const { return interval_seconds_; }

    /**
     * @brief Prints a progress line when the elapsed interval has been reached.
     *
     * Output prefix is "Progress" so batch scripts can distinguish it from
     * "Final Score".
     *
     * @return true if a line was emitted.
     */
    bool report_if_due(double elapsed_seconds, double best_score, double current_score);

    /**
     * @brief Like the score-only overload, but also prints best metrics.
     */
    bool report_if_due(double elapsed_seconds, const Metrics& best_metrics, const Metrics& baseline,
                       double current_score);

private:
    DebugProgress(bool enabled, double interval_seconds);

    bool enabled_ = false;
    double interval_seconds_ = 30.0;
    double last_report_elapsed_ = -1.0;
};

}  // namespace cadd0040
