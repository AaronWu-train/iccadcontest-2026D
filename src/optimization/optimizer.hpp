/**
 * @file optimizer.hpp
 * @brief Abstract interface for clock tree optimization strategies.
 */

#pragma once

#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "debug_progress.hpp"
#include "evaluation.hpp"
#include "types.hpp"

namespace cadd0040 {

struct OptimizerContext {
    const Metrics& baseline_metrics;
    DebugProgress& debug_progress;
};

/**
 * @brief Abstract base class for all clock tree optimization strategies.
 *
 * An optimizer modifies the given ClockTree in place according to the timing
 * information in DataPathGraph and the available cells in BufferLibrary.
 */
class Optimizer {
public:
    virtual ~Optimizer() = default;

    /**
     * @brief Runs the optimization flow.
     *
     * @param clock_tree The clock tree to be optimized. Modified in place.
     * @param data_path_graph Fixed data path timing information.
     * @param buffer_library Available buffer cells and delay/area information.
     * @param context Baseline metrics and optional local debug progress reporter.
     */
    virtual void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                     const BufferLibrary& buffer_library, const OptimizerContext& context) = 0;
};

enum class OptimizerType {
    Dummy,
};

class DummyOptimizer : public Optimizer {
public:
    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;
};

}  // namespace cadd0040
