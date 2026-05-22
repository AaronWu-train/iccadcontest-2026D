/**
 * @file evaluation.cpp
 * @brief Evaluation implementation unit.
 */

#include "evaluation.hpp"

namespace cadd0040 {
std::ostream& operator<<(std::ostream& os, const Metrics& metrics) {
    os << "tns_ss: " << metrics.tns_ss << ", "
       << "wns_ss: " << metrics.wns_ss << ", "
       << "tns_ff: " << metrics.tns_ff << ", "
       << "wns_ff: " << metrics.wns_ff << ", "
       << "area: " << metrics.area;
    return os;
}

Metrics evaluate(const ClockTree&, const DataPathGraph&, const BufferLibrary&) { return {}; }

double score(const Metrics& metrics, const Metrics& baseline) {
    constexpr double alpha = 1.0;
    constexpr double beta = 1.0;
    constexpr double gamma = 1.0;

    double ss_corner_score =
        (1 - (metrics.tns_ss / baseline.tns_ss)) + (1 - (metrics.wns_ss / baseline.wns_ss));
    double ff_corner_score =
        (1 - (metrics.tns_ff / baseline.tns_ff)) * (1 + (metrics.wns_ff / baseline.wns_ff));
    double area_score = 1 - (metrics.area / baseline.area);

    // Combine the corner scores and area score into a single score.
    // The exact formula can be tuned based on the desired trade-offs.
    return alpha * ss_corner_score + beta * ff_corner_score + gamma * area_score;
}

}  // namespace cadd0040
