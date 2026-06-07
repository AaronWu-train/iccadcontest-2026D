/**
 * @file debug_progress.cpp
 * @brief Debug progress reporting implementation.
 */

#include "debug_progress.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace cadd0040 {
namespace {

constexpr double kDefaultIntervalSeconds = 30.0;

#ifndef NDEBUG
constexpr const char* kEnableEnvVar = "CADD0040_DEBUG_PROGRESS";
constexpr const char* kIntervalEnvVar = "CADD0040_DEBUG_PROGRESS_INTERVAL";

bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return false;
    }

    const std::string_view flag(value);
    return flag == "1" || flag == "true" || flag == "yes" || flag == "on";
}

double env_interval_seconds(const char* name, double default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return default_value;
    }

    try {
        const double parsed = std::stod(value);
        return parsed > 0.0 ? parsed : default_value;
    } catch (...) {
        return default_value;
    }
}
#endif

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

DebugProgress DebugProgress::from_environment() {
#ifdef NDEBUG
    return DebugProgress(false, kDefaultIntervalSeconds);
#else
    return DebugProgress(env_flag_enabled(kEnableEnvVar),
                         env_interval_seconds(kIntervalEnvVar, kDefaultIntervalSeconds));
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
