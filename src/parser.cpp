/**
 * @file parser.cpp
 * @brief Implementation of the parser for reading input files.
 */

#include "parser.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cadd0040 {
namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::runtime_error parse_error(const std::filesystem::path& path, std::size_t line_number,
                               const std::string& message) {
    std::ostringstream stream;
    stream << path << ':' << line_number << ": " << message;
    return std::runtime_error(stream.str());
}

std::vector<double> parse_delay_values(const std::filesystem::path& path, std::size_t line_number,
                                       const std::string& values_text) {
    std::istringstream stream(values_text);
    std::vector<double> values;
    double value = 0.0;
    while (stream >> value) {
        values.push_back(value);
    }

    if (!stream.eof()) {
        throw parse_error(path, line_number, "Invalid delay value");
    }
    if (values.empty()) {
        throw parse_error(path, line_number, "Delay table must contain at least one value");
    }

    return values;
}

}  // namespace

void parse_clock_tree(const std::filesystem::path& path, ClockTree& clock_tree) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open clock tree file: " + path.string());
    }

    const std::regex root_pattern{R"(^\s*Root:\s*(\S+)\s*$)"};
    const std::regex node_pattern{
        R"(^\s*\[(\d+)\]\s+(\S+)\s+\(([^()]*)\)(?:\s+\(SINK\))?\s*$)"};

    std::vector<std::string> node_name_at_depth;
    std::string line;
    std::size_t line_number = 0;
    bool saw_root = false;

    while (std::getline(input, line)) {
        ++line_number;
        if (trim(line).empty()) {
            continue;
        }

        std::smatch match;
        if (std::regex_match(line, match, root_pattern)) {
            if (saw_root) {
                throw parse_error(path, line_number, "Duplicate root line");
            }

            const std::string root_name = match[1].str();
            clock_tree.add_root(root_name);
            node_name_at_depth.assign(1, root_name);
            saw_root = true;
            continue;
        }

        if (!saw_root) {
            throw parse_error(path, line_number, "Node appears before root");
        }

        if (!std::regex_match(line, match, node_pattern)) {
            throw parse_error(path, line_number, "Invalid clock tree line format");
        }

        std::size_t depth = 0;
        try {
            depth = static_cast<std::size_t>(std::stoul(match[1].str()));
        } catch (const std::out_of_range&) {
            throw parse_error(path, line_number, "Clock tree node depth is out of range");
        } catch (const std::invalid_argument&) {
            throw parse_error(path, line_number, "Invalid clock tree node depth");
        }

        if (depth == 0) {
            throw parse_error(path, line_number, "Clock tree node depth must be at least 1");
        }
        if (depth > node_name_at_depth.size() || node_name_at_depth[depth - 1].empty()) {
            throw parse_error(path, line_number, "Missing parent at previous depth");
        }

        const std::string node_name = match[2].str();
        const std::string cell_type = trim(match[3].str());
        const bool is_sink = line.find("(SINK)") != std::string::npos;
        const NodeKind node_kind = is_sink ? NodeKind::FlipFlop : NodeKind::Buffer;

        const std::string parent_name = node_name_at_depth[depth - 1];
        clock_tree.add_node(node_name, cell_type, node_kind, parent_name);

        if (node_name_at_depth.size() <= depth) {
            node_name_at_depth.resize(depth + 1);
        }
        node_name_at_depth[depth] = node_name;
        node_name_at_depth.resize(depth + 1);
    }

    if (!saw_root) {
        throw std::runtime_error("Clock tree file has no root line: " + path.string());
    }
}

void parse_buffer_library(const std::filesystem::path& path, BufferLibrary& buffer_library) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open buffer library file: " + path.string());
    }

    const std::regex cell_begin_pattern{R"(^\s*cell\s+\(([^()]*)\)\s*\{\s*$)"};
    const std::regex size_pattern{
        R"(^\s*SIZE\s+([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s+BY\s+([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*$)"};
    const std::regex ss_delay_pattern{R"(^\s*SS_DELAY\s+(.+?)\s*$)"};
    const std::regex ff_delay_pattern{R"(^\s*FF_DELAY\s+(.+?)\s*$)"};
    const std::regex cell_end_pattern{R"(^\s*\}\s*$)"};

    BufferCell current_cell;
    bool in_cell = false;
    bool saw_size = false;
    bool saw_ss_delay = false;
    bool saw_ff_delay = false;

    auto finish_cell = [&](std::size_t line_number) {
        if (!saw_size) {
            throw parse_error(path, line_number, "Buffer cell missing SIZE");
        }
        if (!saw_ss_delay) {
            throw parse_error(path, line_number, "Buffer cell missing SS_DELAY");
        }
        if (!saw_ff_delay) {
            throw parse_error(path, line_number, "Buffer cell missing FF_DELAY");
        }
        if (current_cell.ss_delays_by_fanout.size() != current_cell.ff_delays_by_fanout.size()) {
            throw parse_error(path, line_number, "SS_DELAY and FF_DELAY table sizes differ");
        }
        if (buffer_library.find(current_cell.name) != buffer_library.end()) {
            throw parse_error(path, line_number, "Duplicate buffer cell: " + current_cell.name);
        }

        current_cell.area = current_cell.width * current_cell.height;
        buffer_library.emplace(current_cell.name, current_cell);
    };

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (trim(line).empty()) {
            continue;
        }

        std::smatch match;
        if (!in_cell) {
            if (!std::regex_match(line, match, cell_begin_pattern)) {
                throw parse_error(path, line_number, "Expected buffer cell declaration");
            }

            current_cell = BufferCell{};
            current_cell.name = trim(match[1].str());
            if (current_cell.name.empty()) {
                throw parse_error(path, line_number, "Buffer cell name cannot be empty");
            }
            in_cell = true;
            saw_size = false;
            saw_ss_delay = false;
            saw_ff_delay = false;
            continue;
        }

        if (std::regex_match(line, match, size_pattern)) {
            if (saw_size) {
                throw parse_error(path, line_number, "Duplicate SIZE line");
            }
            try {
                current_cell.width = std::stod(match[1].str());
                current_cell.height = std::stod(match[2].str());
            } catch (const std::invalid_argument&) {
                throw parse_error(path, line_number, "Buffer SIZE values must be valid numbers");
            } catch (const std::out_of_range&) {
                throw parse_error(path, line_number, "Buffer SIZE values are out of range");
            }
            if (current_cell.width < 0.0 || current_cell.height < 0.0) {
                throw parse_error(path, line_number, "Buffer SIZE values must be non-negative");
            }
            saw_size = true;
            continue;
        }

        if (std::regex_match(line, match, ss_delay_pattern)) {
            if (saw_ss_delay) {
                throw parse_error(path, line_number, "Duplicate SS_DELAY line");
            }
            current_cell.ss_delays_by_fanout = parse_delay_values(path, line_number, match[1].str());
            saw_ss_delay = true;
            continue;
        }

        if (std::regex_match(line, match, ff_delay_pattern)) {
            if (saw_ff_delay) {
                throw parse_error(path, line_number, "Duplicate FF_DELAY line");
            }
            current_cell.ff_delays_by_fanout = parse_delay_values(path, line_number, match[1].str());
            saw_ff_delay = true;
            continue;
        }

        if (std::regex_match(line, cell_end_pattern)) {
            finish_cell(line_number);
            in_cell = false;
            continue;
        }

        throw parse_error(path, line_number, "Invalid buffer library line format");
    }

    if (in_cell) {
        throw parse_error(path, line_number, "Buffer library file ended before closing cell");
    }
}

}  // namespace cadd0040
