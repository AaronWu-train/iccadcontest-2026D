/**
 * @file clock_tree_trace_optimizer.cpp
 * @brief Presentation-oriented optimizer that applies every candidate move to ClockTree.
 */

#include "optimization/visual/clock_tree_trace_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cadd0040 {
namespace {

constexpr std::size_t kDefaultGreedyWarmupIterations = 10;
constexpr std::size_t kDefaultSaIterations = 18;
constexpr double kInitialTemperature = 0.35;
constexpr double kMinTemperature = 1e-4;
constexpr double kCoolingFactor = 0.03;

struct TreeEdgeName {
    std::string parent;
    std::string child;
};

struct AppliedMove {
    enum class Kind {
        Insert,
        Resize,
        Remove,
        None,
    };

    Kind kind = Kind::None;
    std::string description;
    std::string buffer_name;
    std::string parent_name;
    std::string child_name;
    std::string old_cell;
    std::string new_cell;
};

struct TraceFrame {
    std::size_t index = 0;
    std::size_t iteration = 0;
    std::string phase;
    std::string status;
    std::string move;
    Metrics metrics;
    double score = 0.0;
    std::string tree_text;
};

std::size_t configured_count(const char* variable_name, std::size_t default_value) {
    const char* value = std::getenv(variable_name);
    if (value == nullptr) {
        return default_value;
    }
    const auto parsed = std::stoll(value);
    return parsed <= 0 ? 0 : static_cast<std::size_t>(parsed);
}

std::filesystem::path trace_directory() {
    const char* value = std::getenv("CADD0040_VISUAL_TRACE_DIR");
    if (value == nullptr || std::string(value).empty()) {
        return std::filesystem::path{"visual_trace"};
    }
    return std::filesystem::path{value};
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

void write_trace_text(const std::filesystem::path& path, const std::vector<TraceFrame>& frames) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open visual trace text file: " + path.string());
    }

    output << std::fixed << std::setprecision(6);
    for (const auto& frame : frames) {
        output << "=== frame " << frame.index << " | iteration " << frame.iteration << " | "
               << frame.phase << " | " << frame.status << " ===\n";
        output << "move: " << frame.move << '\n';
        output << "score: " << frame.score << '\n';
        output << "metrics: " << frame.metrics << "\n\n";
        output << frame.tree_text;
        if (!frame.tree_text.empty() && frame.tree_text.back() != '\n') {
            output << '\n';
        }
        output << '\n';
    }
}

void write_frames_json(const std::filesystem::path& path, const std::vector<TraceFrame>& frames) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open visual frames json file: " + path.string());
    }

    output << std::fixed << std::setprecision(8);
    output << "{\n  \"frames\": [\n";
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        output << "    {\n";
        output << "      \"index\": " << frame.index << ",\n";
        output << "      \"iteration\": " << frame.iteration << ",\n";
        output << "      \"phase\": \"" << json_escape(frame.phase) << "\",\n";
        output << "      \"status\": \"" << json_escape(frame.status) << "\",\n";
        output << "      \"move\": \"" << json_escape(frame.move) << "\",\n";
        output << "      \"score\": " << frame.score << ",\n";
        output << "      \"tns_ss\": " << frame.metrics.tns_ss << ",\n";
        output << "      \"wns_ss\": " << frame.metrics.wns_ss << ",\n";
        output << "      \"tns_ff\": " << frame.metrics.tns_ff << ",\n";
        output << "      \"wns_ff\": " << frame.metrics.wns_ff << ",\n";
        output << "      \"area\": " << frame.metrics.area << ",\n";
        output << "      \"tree\": \"" << json_escape(frame.tree_text) << "\"\n";
        output << "    }" << (i + 1 == frames.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

bool cell_supports_fanout(const BufferCell& cell, std::size_t fanout) {
    return fanout > 0 && fanout <= cell.ss_delays_by_fanout.size() &&
           fanout <= cell.ff_delays_by_fanout.size();
}

std::vector<std::string> sorted_cells_for_fanout(const BufferLibrary& buffer_library,
                                                 std::size_t fanout) {
    std::vector<std::string> names;
    for (const auto& [name, cell] : buffer_library) {
        if (cell_supports_fanout(cell, fanout)) {
            names.push_back(name);
        }
    }

    std::sort(names.begin(), names.end(), [&](const std::string& lhs, const std::string& rhs) {
        const auto& lhs_cell = buffer_library.at(lhs);
        const auto& rhs_cell = buffer_library.at(rhs);
        if (lhs_cell.area != rhs_cell.area) {
            return lhs_cell.area < rhs_cell.area;
        }
        return lhs < rhs;
    });
    return names;
}

std::vector<std::string> active_buffer_names(const ClockTree& clock_tree) {
    std::vector<std::string> names;
    for (const auto& entry : clock_tree.preorder_with_depth()) {
        const auto& node = clock_tree.node(entry.node_name);
        if (node.kind == NodeKind::Buffer) {
            names.push_back(node.name);
        }
    }
    return names;
}

std::vector<TreeEdgeName> active_edges(const ClockTree& clock_tree) {
    std::vector<TreeEdgeName> edges;
    for (const auto& entry : clock_tree.preorder_with_depth()) {
        const auto& parent = clock_tree.node(entry.node_name);
        for (const NodeId child_id : parent.child_ids) {
            const auto& child = clock_tree.nodes().at(child_id);
            edges.push_back(TreeEdgeName{parent.name, child.name});
        }
    }
    return edges;
}

TreeEdgeName edge_before_node(const ClockTree& clock_tree, const std::string& node_name) {
    if (!clock_tree.contains_name(node_name)) {
        const auto edges = active_edges(clock_tree);
        return edges.empty() ? TreeEdgeName{} : edges.front();
    }

    const auto& node = clock_tree.node(node_name);
    if (node.parent_id == kInvalidNodeId) {
        const auto edges = active_edges(clock_tree);
        return edges.empty() ? TreeEdgeName{} : edges.front();
    }

    const auto& parent = clock_tree.nodes().at(node.parent_id);
    return TreeEdgeName{parent.name, node.name};
}

std::string next_cell_for_buffer(const ClockTree& clock_tree, const BufferLibrary& buffer_library,
                                 const std::string& buffer_name) {
    const auto& node = clock_tree.node(buffer_name);
    const auto cells = sorted_cells_for_fanout(buffer_library, node.child_ids.size());
    if (cells.empty()) {
        return "";
    }

    const auto current_it = std::find(cells.begin(), cells.end(), node.cell_type);
    if (current_it == cells.end()) {
        return cells.front();
    }
    const std::size_t next_idx =
        (static_cast<std::size_t>(current_it - cells.begin()) + 1) % cells.size();
    return cells[next_idx] == node.cell_type && cells.size() == 1 ? "" : cells[next_idx];
}

void record_frame(std::vector<TraceFrame>& frames, ClockTree& clock_tree,
                  const DataPathGraph& data_path_graph, const BufferLibrary& buffer_library,
                  const Metrics& baseline_metrics, std::size_t iteration, std::string phase,
                  std::string status, std::string move) {
    const Metrics metrics = evaluate(clock_tree, data_path_graph, buffer_library);
    frames.push_back(TraceFrame{frames.size(), iteration, std::move(phase), std::move(status),
                                std::move(move), metrics, score(metrics, baseline_metrics),
                                clock_tree_text(clock_tree)});
}

AppliedMove apply_insert_move(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                              const BufferLibrary& buffer_library, std::size_t iteration,
                              std::size_t& next_buffer_id) {
    const auto fanout1_cells = sorted_cells_for_fanout(buffer_library, 1);
    if (fanout1_cells.empty() || data_path_graph.edge_count() == 0) {
        return AppliedMove{};
    }

    const auto& path = data_path_graph.all_edges()[iteration % data_path_graph.edge_count()];
    const std::string target =
        iteration % 2 == 0 ? path.capture_flip_flop_name : path.launch_flip_flop_name;
    const TreeEdgeName edge = edge_before_node(clock_tree, target);
    if (edge.parent.empty() || edge.child.empty()) {
        return AppliedMove{};
    }

    std::string buffer_name;
    do {
        buffer_name = "NEW_VIS_BUF_" + std::to_string(next_buffer_id++);
    } while (clock_tree.contains_name(buffer_name));

    const std::string& cell_name = fanout1_cells[iteration % fanout1_cells.size()];
    if (!clock_tree.insert_buffer(edge.parent, edge.child, buffer_name, cell_name,
                                  buffer_library)) {
        return AppliedMove{};
    }

    AppliedMove move;
    move.kind = AppliedMove::Kind::Insert;
    move.buffer_name = buffer_name;
    move.parent_name = edge.parent;
    move.child_name = edge.child;
    move.new_cell = cell_name;
    move.description = "insert " + buffer_name + " (" + cell_name + ") between " + edge.parent +
                       " and " + edge.child;
    return move;
}

AppliedMove apply_resize_move(ClockTree& clock_tree, const BufferLibrary& buffer_library,
                              std::size_t iteration) {
    const auto buffers = active_buffer_names(clock_tree);
    if (buffers.empty()) {
        return AppliedMove{};
    }

    for (std::size_t attempt = 0; attempt < buffers.size(); ++attempt) {
        const std::string& buffer_name = buffers[(iteration + attempt) % buffers.size()];
        const auto& node = clock_tree.node(buffer_name);
        const std::string next_cell = next_cell_for_buffer(clock_tree, buffer_library, buffer_name);
        if (next_cell.empty() || next_cell == node.cell_type) {
            continue;
        }

        const std::string old_cell = node.cell_type;
        if (!clock_tree.resize_buffer(buffer_name, next_cell, buffer_library)) {
            continue;
        }

        AppliedMove move;
        move.kind = AppliedMove::Kind::Resize;
        move.buffer_name = buffer_name;
        move.old_cell = old_cell;
        move.new_cell = next_cell;
        move.description = "resize " + buffer_name + " from " + old_cell + " to " + next_cell;
        return move;
    }

    return AppliedMove{};
}

AppliedMove apply_remove_move(ClockTree& clock_tree, std::vector<std::string>& inserted_buffers) {
    while (!inserted_buffers.empty() && !clock_tree.contains_name(inserted_buffers.back())) {
        inserted_buffers.pop_back();
    }
    if (inserted_buffers.empty()) {
        return AppliedMove{};
    }

    const std::string buffer_name = inserted_buffers.back();
    const auto& node = clock_tree.node(buffer_name);
    if (node.child_ids.size() != 1 || node.parent_id == kInvalidNodeId) {
        return AppliedMove{};
    }

    const std::string parent_name = clock_tree.nodes().at(node.parent_id).name;
    const std::string child_name = clock_tree.nodes().at(node.child_ids.front()).name;
    const std::string cell_name = node.cell_type;
    if (!clock_tree.remove_buffer(buffer_name)) {
        return AppliedMove{};
    }

    AppliedMove move;
    move.kind = AppliedMove::Kind::Remove;
    move.buffer_name = buffer_name;
    move.parent_name = parent_name;
    move.child_name = child_name;
    move.old_cell = cell_name;
    move.description =
        "remove " + buffer_name + " and reconnect " + parent_name + " to " + child_name;
    return move;
}

bool rollback_move(ClockTree& clock_tree, const BufferLibrary& buffer_library,
                   const AppliedMove& move) {
    switch (move.kind) {
        case AppliedMove::Kind::Insert:
            return clock_tree.remove_buffer(move.buffer_name);
        case AppliedMove::Kind::Resize:
            return clock_tree.resize_buffer(move.buffer_name, move.old_cell, buffer_library);
        case AppliedMove::Kind::Remove:
            return clock_tree.insert_buffer(move.parent_name, move.child_name, move.buffer_name,
                                            move.old_cell, buffer_library);
        case AppliedMove::Kind::None:
            return true;
    }
    return false;
}

AppliedMove apply_visual_candidate(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                                   const BufferLibrary& buffer_library,
                                   std::vector<std::string>& accepted_inserted_buffers,
                                   std::size_t move_seed, std::size_t& next_buffer_id) {
    AppliedMove move;
    const std::size_t mode = move_seed % 4;
    if (mode == 0 || mode == 2) {
        move = apply_insert_move(clock_tree, data_path_graph, buffer_library, move_seed,
                                 next_buffer_id);
    } else if (mode == 1) {
        move = apply_resize_move(clock_tree, buffer_library, move_seed);
    } else {
        move = apply_remove_move(clock_tree, accepted_inserted_buffers);
        if (move.kind == AppliedMove::Kind::None) {
            move = apply_resize_move(clock_tree, buffer_library, move_seed);
        }
    }
    return move;
}

void remember_accepted_move(const AppliedMove& move,
                            std::vector<std::string>& accepted_inserted_buffers) {
    if (move.kind == AppliedMove::Kind::Insert) {
        accepted_inserted_buffers.push_back(move.buffer_name);
        return;
    }
    if (move.kind == AppliedMove::Kind::Remove && !accepted_inserted_buffers.empty() &&
        accepted_inserted_buffers.back() == move.buffer_name) {
        accepted_inserted_buffers.pop_back();
    }
}

bool accept_sa_move(double delta, double temperature, std::mt19937& rng) {
    if (delta >= 0.0) {
        return true;
    }

    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double probability = std::exp(delta / temperature);
    return dist(rng) < probability;
}

std::string with_sa_note(const std::string& description, double delta, double temperature,
                         bool accepted) {
    std::ostringstream stream;
    stream << description << " [SA delta=" << std::fixed << std::setprecision(4) << delta
           << ", temp=" << temperature;
    if (delta < 0.0) {
        stream << (accepted ? ", downhill accepted" : ", downhill rejected");
    }
    stream << ']';
    return stream.str();
}

}  // namespace

void ClockTreeTraceOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                                  const BufferLibrary& buffer_library,
                                  const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    const std::filesystem::path trace_dir = trace_directory();
    std::filesystem::create_directories(trace_dir);

    std::vector<TraceFrame> frames;
    std::vector<std::string> accepted_inserted_buffers;
    std::size_t next_buffer_id = 0;

    double current_score = score(baseline_metrics, baseline_metrics);
    record_frame(frames, clock_tree, data_path_graph, buffer_library, baseline_metrics, 0,
                 "baseline", "kept", "initial clock tree");

    const std::size_t greedy_iterations =
        configured_count("CADD0040_VISUAL_GREEDY_WARMUP", kDefaultGreedyWarmupIterations);
    const std::size_t sa_iterations =
        configured_count("CADD0040_VISUAL_SA_ITERATIONS",
                         configured_count("CADD0040_VISUAL_ITERATIONS", kDefaultSaIterations));

    std::size_t global_iteration = 0;
    for (std::size_t iteration = 0; iteration < greedy_iterations; ++iteration) {
        AppliedMove move =
            apply_visual_candidate(clock_tree, data_path_graph, buffer_library,
                                   accepted_inserted_buffers, global_iteration, next_buffer_id);

        if (move.kind == AppliedMove::Kind::None) {
            record_frame(frames, clock_tree, data_path_graph, buffer_library, baseline_metrics,
                         global_iteration + 1, "greedy_warmup", "skipped",
                         "no legal direct ClockTree move");
            ++global_iteration;
            continue;
        }

        const Metrics candidate_metrics = evaluate(clock_tree, data_path_graph, buffer_library);
        const double candidate_score = score(candidate_metrics, baseline_metrics);
        const bool accept = candidate_score >= current_score;
        record_frame(frames, clock_tree, data_path_graph, buffer_library, baseline_metrics,
                     global_iteration + 1, "greedy_warmup", accept ? "accepted" : "rejected",
                     move.description);

        if (accept) {
            current_score = candidate_score;
            remember_accepted_move(move, accepted_inserted_buffers);
            ++global_iteration;
            continue;
        }

        if (!rollback_move(clock_tree, buffer_library, move)) {
            throw std::runtime_error("Failed to rollback visual optimizer move: " +
                                     move.description);
        }
        record_frame(frames, clock_tree, data_path_graph, buffer_library, baseline_metrics,
                     global_iteration + 1, "greedy_rollback", "kept",
                     "rollback after rejected " + move.description);
        ++global_iteration;
    }

    std::mt19937 rng(2026);
    for (std::size_t iteration = 0; iteration < sa_iterations; ++iteration) {
        AppliedMove move =
            apply_visual_candidate(clock_tree, data_path_graph, buffer_library,
                                   accepted_inserted_buffers, global_iteration, next_buffer_id);

        if (move.kind == AppliedMove::Kind::None) {
            record_frame(frames, clock_tree, data_path_graph, buffer_library, baseline_metrics,
                         global_iteration + 1, "simulated_annealing", "skipped",
                         "no legal direct ClockTree move");
            ++global_iteration;
            continue;
        }

        const Metrics candidate_metrics = evaluate(clock_tree, data_path_graph, buffer_library);
        const double candidate_score = score(candidate_metrics, baseline_metrics);
        const double delta = candidate_score - current_score;
        const double progress = sa_iterations <= 1 ? 1.0
                                                   : static_cast<double>(iteration) /
                                                         static_cast<double>(sa_iterations - 1);
        const double temperature =
            std::max(kMinTemperature, kInitialTemperature * std::pow(kCoolingFactor, progress));
        const bool accept = accept_sa_move(delta, temperature, rng);
        const std::string move_text = with_sa_note(move.description, delta, temperature, accept);

        record_frame(frames, clock_tree, data_path_graph, buffer_library, baseline_metrics,
                     global_iteration + 1, "simulated_annealing", accept ? "accepted" : "rejected",
                     move_text);

        if (accept) {
            current_score = candidate_score;
            remember_accepted_move(move, accepted_inserted_buffers);
            ++global_iteration;
            continue;
        }

        if (!rollback_move(clock_tree, buffer_library, move)) {
            throw std::runtime_error("Failed to rollback visual optimizer move: " +
                                     move.description);
        }
        record_frame(frames, clock_tree, data_path_graph, buffer_library, baseline_metrics,
                     global_iteration + 1, "sa_rollback", "kept",
                     "rollback after rejected " + move.description);
        ++global_iteration;
    }

    write_trace_text(trace_dir / "trace.txt", frames);
    write_frames_json(trace_dir / "frames.json", frames);

    context.debug_progress.log([&](std::ostream& os) {
        os << "ClockTreeTraceOptimizer: wrote " << frames.size() << " frames to "
           << trace_dir.string() << '\n';
    });
}

}  // namespace cadd0040
