/**
 * @file optimizer.hpp
 * @brief Abstract interface for clock tree optimization strategies.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <string>

#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "debug_progress.hpp"
#include "evaluation.hpp"
#include "types.hpp"

namespace cadd0040 {

struct OptimizerConfigFile;

struct OptimizerProgressEvent {
    std::size_t step = 0;
    double elapsed_seconds = 0.0;
    std::string phase;
    int round = -1;
    std::string event;
    double current_score = std::numeric_limits<double>::quiet_NaN();
    double best_score = std::numeric_limits<double>::quiet_NaN();
    double delta_score = std::numeric_limits<double>::quiet_NaN();
    Metrics metrics;
    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    std::string candidate_policy;
    std::string accept_policy;
};

struct OptimizerContext {
    const Metrics& baseline_metrics;
    DebugProgress& debug_progress;
    std::string optimizer_name;
    std::string testcase_name;
    const OptimizerConfigFile* optimizer_config = nullptr;
    std::size_t checkpoint_interval = 0;
    std::function<void(const ClockTree&)> checkpoint_writer;
    std::size_t progress_interval = 0;
    std::function<void(const OptimizerProgressEvent&)> progress_writer;
    std::size_t visual_interval = 0;
    std::function<void(const ClockTree&, const OptimizerProgressEvent&)> visual_writer;

    void write_checkpoint(const ClockTree& clock_tree) const {
        if (checkpoint_writer) {
            checkpoint_writer(clock_tree);
        }
    }

    void maybe_checkpoint(const ClockTree& clock_tree, std::size_t step) const {
        if (checkpoint_interval > 0 && step > 0 && step % checkpoint_interval == 0) {
            write_checkpoint(clock_tree);
        }
    }

    void record_progress(const OptimizerProgressEvent& event) const {
        if (progress_writer) {
            progress_writer(event);
        }
    }

    void maybe_record_progress(const OptimizerProgressEvent& event, bool force) const {
        if (!progress_writer) {
            return;
        }
        if (force || (progress_interval > 0 && event.step % progress_interval == 0)) {
            progress_writer(event);
        }
    }

    void maybe_record_visual(const ClockTree& clock_tree, const OptimizerProgressEvent& event,
                             bool force) const {
        if (!visual_writer) {
            return;
        }
        if (force || (visual_interval > 0 && event.step % visual_interval == 0)) {
            visual_writer(clock_tree, event);
        }
    }
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
