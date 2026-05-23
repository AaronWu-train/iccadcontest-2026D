/**
 * @file evaluation.cpp
 * @brief Evaluation implementation unit.
 */

#include "evaluation.hpp"

#include <algorithm>
#include <cmath>

namespace cadd0040 {
namespace {

constexpr double kScoreAlpha = 1.0;
constexpr double kScoreBeta = 1.0;
constexpr double kScoreGamma = 1.0;
constexpr double kZeroTolerance = 1e-12;

}  // namespace

std::ostream& operator<<(std::ostream& os, const Metrics& metrics) {
    os << "tns_ss: " << metrics.tns_ss << ", "
       << "wns_ss: " << metrics.wns_ss << ", "
       << "tns_ff: " << metrics.tns_ff << ", "
       << "wns_ff: " << metrics.wns_ff << ", "
       << "area: " << metrics.area;
    return os;
}

Metrics evaluate(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                 const BufferLibrary& buffer_library) {
    Metrics metrics;
    metrics.area = clock_tree.area(buffer_library);

    for (const auto& edge : data_path_graph.all_edges()) {
        const double ss_skew = clock_tree.clock_skew(
            edge.launch_flip_flop_name, edge.capture_flip_flop_name, buffer_library, Corner::SS);
        const double ss_slack = data_path_graph.clock_period() - data_path_graph.setup_time() -
                                edge.data_delay.ss + ss_skew;
        if (ss_slack < 0.0) {
            metrics.tns_ss += ss_slack;
            metrics.wns_ss = std::min(metrics.wns_ss, ss_slack);
        }

        const double ff_skew = clock_tree.clock_skew(
            edge.launch_flip_flop_name, edge.capture_flip_flop_name, buffer_library, Corner::FF);
        const double ff_slack = edge.data_delay.ff - data_path_graph.hold_time() - ff_skew;
        if (ff_slack < 0.0) {
            metrics.tns_ff += ff_slack;
            metrics.wns_ff = std::min(metrics.wns_ff, ff_slack);
        }
    }

    return metrics;
}

double score(const Metrics& metrics, const Metrics& baseline) {
    auto safe_divide = [](double value, double baseline_value) -> double {
        if (std::fabs(baseline_value) <= kZeroTolerance) {
            return 0.0;
        }
        return 1.0 - (value / baseline_value);
    };

    const double tns_ss_score = safe_divide(metrics.tns_ss, baseline.tns_ss);
    const double wns_ss_score = safe_divide(metrics.wns_ss, baseline.wns_ss);
    const double tns_ff_score = safe_divide(metrics.tns_ff, baseline.tns_ff);
    const double wns_ff_score = safe_divide(metrics.wns_ff, baseline.wns_ff);
    const double area_score = safe_divide(metrics.area, baseline.area);

    const double ss_corner_score = tns_ss_score + wns_ss_score;
    const double ff_corner_score = tns_ff_score + wns_ff_score;

    return kScoreAlpha * ss_corner_score + kScoreBeta * ff_corner_score + kScoreGamma * area_score;
}

}  // namespace cadd0040
