#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "debug_progress.hpp"

TEST_CASE("DebugProgress is disabled when not requested") {
    auto reporter = cadd0040::DebugProgress::from_debug_flag(false);

    CHECK_FALSE(reporter.enabled());
    CHECK(reporter.interval_seconds() == 30.0);
    CHECK_FALSE(reporter.report_if_due(60.0, 0.5, std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("DebugProgress can be enabled explicitly") {
    const auto reporter = cadd0040::DebugProgress::from_debug_flag(true);

#ifdef NDEBUG
    CHECK_FALSE(reporter.enabled());
#else
    CHECK(reporter.enabled());
#endif
    CHECK(reporter.interval_seconds() == 30.0);
}
