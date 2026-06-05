#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <limits>

#include "debug_progress.hpp"

namespace {

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

}  // namespace

TEST_CASE("DebugProgress is disabled by default") {
    unset_env("CADD0040_DEBUG_PROGRESS");
    unset_env("CADD0040_DEBUG_PROGRESS_INTERVAL");

    auto reporter = cadd0040::DebugProgress::from_environment();

    CHECK_FALSE(reporter.enabled());
    CHECK(reporter.interval_seconds() == 30.0);
    CHECK_FALSE(reporter.report_if_due(60.0, 0.5, std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("DebugProgress enables with CADD0040_DEBUG_PROGRESS") {
    set_env("CADD0040_DEBUG_PROGRESS", "1");
    unset_env("CADD0040_DEBUG_PROGRESS_INTERVAL");

    const auto reporter = cadd0040::DebugProgress::from_environment();

    CHECK(reporter.enabled());
}

TEST_CASE("DebugProgress respects custom interval") {
    set_env("CADD0040_DEBUG_PROGRESS", "yes");
    set_env("CADD0040_DEBUG_PROGRESS_INTERVAL", "10");

    const auto reporter = cadd0040::DebugProgress::from_environment();

    CHECK(reporter.enabled());
    CHECK(reporter.interval_seconds() == 10.0);
}
