/**
 * @file solver.cpp
 * @brief Solver implementation unit.
 */

#include "solver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "debug_progress.hpp"
#include "optimization/factory.hpp"
#include "optimization/optimizer.hpp"
#include "parser.hpp"

namespace cadd0040 {
namespace {

constexpr std::size_t kDefaultCheckpointInterval = 4096;
constexpr std::size_t kDefaultTraceInterval = 256;

struct VisualTraceFrame {
    std::size_t index = 0;
    OptimizerProgressEvent event;
    std::string tree_text;
};

bool env_flag_enabled(const char* variable_name) {
    const char* value = std::getenv(variable_name);
    if (value == nullptr || *value == '\0') {
        return false;
    }
    const std::string_view flag(value);
    return flag == "1" || flag == "true" || flag == "yes" || flag == "on";
}

std::size_t env_size_or_default(const char* variable_name, std::size_t default_value) {
    const char* value = std::getenv(variable_name);
    if (value == nullptr || *value == '\0') {
        return default_value;
    }
    try {
        return static_cast<std::size_t>(std::stoull(value));
    } catch (const std::exception&) {
        return default_value;
    }
}

std::filesystem::path env_path_or_default(const char* variable_name,
                                          const std::filesystem::path& default_value) {
    const char* value = std::getenv(variable_name);
    if (value == nullptr || std::string(value).empty()) {
        return default_value;
    }
    return std::filesystem::path{value};
}

bool metrics_report_enabled() {
    const char* value = std::getenv("CADD0040_REPORT_METRICS");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

std::size_t checkpoint_interval_from_environment() {
    return env_size_or_default("CADD0040_CHECKPOINT_STEPS", kDefaultCheckpointInterval);
}

std::string clock_tree_text(const ClockTree& clock_tree) {
    std::ostringstream stream;
    stream << clock_tree;
    return stream.str();
}

std::string json_escape(const std::string& value) {
    std::ostringstream stream;
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                stream << "\\\\";
                break;
            case '"':
                stream << "\\\"";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                stream << ch;
                break;
        }
    }
    return stream.str();
}

void write_progress_header(std::ostream& output) {
    output << "optimizer\ttestcase\tstep\telapsed_sec\tphase\tround\tevent\tcurrent_score"
              "\tbest_score\tdelta_score\ttns_ss\twns_ss\ttns_ff\twns_ff\tarea"
              "\taccepted_moves\trejected_moves\tcandidate_policy\n";
}

void write_progress_event(std::ostream& output, const std::string& optimizer_name,
                          const std::string& testcase_name, const OptimizerProgressEvent& event) {
    output << optimizer_name << '\t' << testcase_name << '\t' << event.step << '\t' << std::fixed
           << std::setprecision(6) << event.elapsed_seconds << '\t' << event.phase << '\t'
           << event.round << '\t' << event.event << '\t' << event.current_score << '\t'
           << event.best_score << '\t' << event.delta_score << '\t' << event.metrics.tns_ss << '\t'
           << event.metrics.wns_ss << '\t' << event.metrics.tns_ff << '\t' << event.metrics.wns_ff
           << '\t' << event.metrics.area << '\t' << event.accepted_moves << '\t'
           << event.rejected_moves << '\t' << event.candidate_policy << '\n';
}

void write_visual_frames(const std::filesystem::path& path,
                         const std::vector<VisualTraceFrame>& frames) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open visual frames json file: " + path.string());
    }

    output << std::fixed << std::setprecision(8);
    output << "{\n  \"frames\": [\n";
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        const auto& event = frame.event;
        output << "    {\n";
        output << "      \"index\": " << frame.index << ",\n";
        output << "      \"iteration\": " << event.step << ",\n";
        output << "      \"phase\": \"" << json_escape(event.phase) << "\",\n";
        output << "      \"status\": \"" << json_escape(event.event) << "\",\n";
        output << "      \"move\": \"" << json_escape(event.candidate_policy) << "\",\n";
        output << "      \"score\": " << event.best_score << ",\n";
        output << "      \"tns_ss\": " << event.metrics.tns_ss << ",\n";
        output << "      \"wns_ss\": " << event.metrics.wns_ss << ",\n";
        output << "      \"tns_ff\": " << event.metrics.tns_ff << ",\n";
        output << "      \"wns_ff\": " << event.metrics.wns_ff << ",\n";
        output << "      \"area\": " << event.metrics.area << ",\n";
        output << "      \"tree\": \"" << json_escape(frame.tree_text) << "\"\n";
        output << "    }" << (i + 1 == frames.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

}  // namespace

void Solver::load_input() {
    parse_clock_tree(config_.clk_tree_path, clock_tree_);
    parse_data_path_graph(config_.ff_delay_path, config_.ss_delay_path, data_path_graph_);
    parse_buffer_library(config_.buflib_path, buffer_library_);
}

void Solver::write_output(const ClockTree& clock_tree, const std::filesystem::path& output_path) {
    const auto parent_path = output_path.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
    }

    std::ofstream output(output_path);

    if (!output) {
        throw std::runtime_error("Failed to open output file: " + output_path.string());
    }

    output << clock_tree;

    if (!output) {
        throw std::runtime_error("Failed to write output file: " + output_path.string());
    }
}

void Solver::write_output_atomically(const ClockTree& clock_tree,
                                     const std::filesystem::path& output_path) {
    std::filesystem::path temp_path = output_path;
    temp_path += ".tmp";
    write_output(clock_tree, temp_path);
    std::filesystem::rename(temp_path, output_path);
}

int Solver::run() {
    try {
        load_input();

        DebugProgress debug_progress = DebugProgress::from_environment();
        const Metrics baseline_metrics = evaluate(clock_tree_, data_path_graph_, buffer_library_);
        const bool report_metrics = metrics_report_enabled();

        if (report_metrics) {
            std::cout << "Initial baseline metrics: " << baseline_metrics << '\n';
            std::cout << "Initial Score = " << score(baseline_metrics, baseline_metrics) << '\n';
        }

        write_output(clock_tree_, config_.output_file);

        const std::string optimizer_name = config_.optimizer_name;
        const std::string testcase_name = config_.testcase_dir.filename().string();
        const std::size_t checkpoint_interval = checkpoint_interval_from_environment();
        auto checkpoint_writer = [&](const ClockTree& checkpoint_tree) {
            try {
                write_output_atomically(checkpoint_tree, config_.output_file);
            } catch (const std::exception& e) {
                debug_progress.log([&](std::ostream& os) {
                    os << "Solver: checkpoint write failed: " << e.what() << '\n';
                });
            }
        };

        std::ofstream progress_output;
        const bool progress_trace_enabled = env_flag_enabled("CADD0040_PROGRESS_TRACE");
        const std::size_t progress_interval =
            env_size_or_default("CADD0040_PROGRESS_STEPS", kDefaultTraceInterval);
        if (progress_trace_enabled) {
            const auto default_progress_dir =
                std::filesystem::path{"progress_trace"} / optimizer_name / testcase_name;
            const auto progress_dir =
                env_path_or_default("CADD0040_PROGRESS_DIR", default_progress_dir);
            std::filesystem::create_directories(progress_dir);
            progress_output.open(progress_dir / "progress.tsv");
            if (!progress_output) {
                throw std::runtime_error("Failed to open progress trace file: " +
                                         (progress_dir / "progress.tsv").string());
            }
            write_progress_header(progress_output);
        }

        std::vector<VisualTraceFrame> visual_frames;
        const bool visual_trace_enabled = env_flag_enabled("CADD0040_VISUAL_TRACE");
        const std::size_t visual_interval =
            env_size_or_default("CADD0040_VISUAL_TRACE_STEPS", kDefaultTraceInterval);
        const auto default_visual_dir =
            std::filesystem::path{"visual_trace"} / optimizer_name / testcase_name;
        const auto visual_dir =
            env_path_or_default("CADD0040_VISUAL_TRACE_DIR", default_visual_dir);
        if (visual_trace_enabled) {
            std::filesystem::create_directories(visual_dir);
        }

        auto progress_writer = [&](const OptimizerProgressEvent& event) {
            if (progress_output) {
                write_progress_event(progress_output, optimizer_name, testcase_name, event);
            }
        };
        auto visual_writer = [&](const ClockTree& trace_tree, const OptimizerProgressEvent& event) {
            visual_frames.push_back(
                VisualTraceFrame{visual_frames.size(), event, clock_tree_text(trace_tree)});
        };

        std::function<void(const OptimizerProgressEvent&)> progress_callback;
        if (progress_trace_enabled) {
            progress_callback = progress_writer;
        }
        std::function<void(const ClockTree&, const OptimizerProgressEvent&)> visual_callback;
        if (visual_trace_enabled) {
            visual_callback = visual_writer;
        }

        OptimizerContext optimizer_context{baseline_metrics,
                                           debug_progress,
                                           optimizer_name,
                                           testcase_name,
                                           checkpoint_interval,
                                           checkpoint_writer,
                                           progress_trace_enabled ? progress_interval : 0,
                                           progress_callback,
                                           visual_trace_enabled ? visual_interval : 0,
                                           visual_callback};

        auto optimizer = make_optimizer(config_.optimizer_name);
        optimizer->run(clock_tree_, data_path_graph_, buffer_library_, optimizer_context);

        if (report_metrics) {
            const Metrics final_metrics = evaluate(clock_tree_, data_path_graph_, buffer_library_);
            std::cout << "======================================\n";
            std::cout << "Final metrics: " << final_metrics << '\n';
            std::cout << "Final Score = " << score(final_metrics, baseline_metrics) << '\n';
        }

        write_output_atomically(clock_tree_, config_.output_file);
        if (visual_trace_enabled) {
            write_visual_frames(visual_dir / "frames.json", visual_frames);
        }

        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "Solver failed: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}

}  // namespace cadd0040
