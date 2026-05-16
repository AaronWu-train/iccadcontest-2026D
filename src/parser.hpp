/**
 * @file parser.hpp
 * @brief Parser for reading input files.
 */

#pragma once

namespace cadd0040 {
// TODO: @liuchengLYC implement this function in parser.cpp to read the clock tree input file and
// populate the ClockTree data structure.
void parse_clock_tree(const std::filesystem::path& path, ClockTree& clock_tree);

// TODO: @BenLai95 implement this function in parser.cpp to read the data path graph input files and
// populate the DataPathGraph data structure.
void parse_data_path_graph(const std::filesystem::path& ff_delay_path,
                           const std::filesystem::path& ss_delay_path,
                           DataPathGraph& data_path_graph);

// TODO: @BenLai95 implement this function in parser.cpp to read the buffer library input file and
// populate the BufferLibrary data structure.
void parse_buffer_library(const std::filesystem::path& path, BufferLibrary& buffer_library);

}  // namespace cadd0040