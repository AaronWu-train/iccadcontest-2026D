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

}  // namespace cadd0040
