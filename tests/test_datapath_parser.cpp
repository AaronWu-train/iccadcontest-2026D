#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "parser.hpp"

namespace {

std::filesystem::path source_path(const std::filesystem::path& relative_path) {
    return std::filesystem::path{CADD0040_SOURCE_DIR} / relative_path;
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path);
    REQUIRE(output);
    output << contents;
}

}  // namespace

TEST_CASE("parse_data_path_graph reads the real tmp delay reports") {
    cadd0040::DataPathGraph graph;

    cadd0040::parse_data_path_graph(source_path("tmp/FF_delay.rpt"),
                                    source_path("tmp/SS_delay.rpt"), graph);

    CHECK(graph.clock_period() == Catch::Approx(0.3));
    CHECK(graph.setup_time() == Catch::Approx(0.024));
    CHECK(graph.hold_time() == Catch::Approx(0.015));
    CHECK(graph.edge_count() == 11022);

    const auto& first = graph.edge("Path1");
    CHECK(first.id == 0);
    CHECK(first.launch_flip_flop_name == "FF_1354");
    CHECK(first.capture_flip_flop_name == "FF_3517");
    CHECK(first.data_delay.ss == Catch::Approx(0.2377));
    CHECK(first.data_delay.ff == Catch::Approx(0.2461));

    const auto& last = graph.edge("Path11022");
    CHECK(last.launch_flip_flop_name == "FF_2610");
    CHECK(last.capture_flip_flop_name == "FF_3516");
    CHECK(last.data_delay.ss == Catch::Approx(0.1563));
    CHECK(last.data_delay.ff == Catch::Approx(0.1628));

    CHECK(graph.incoming_edges("FF_3547").size() == 10);
    REQUIRE_FALSE(graph.outgoing_edges("FF_1354").empty());
    CHECK(graph.edge(graph.outgoing_edges("FF_1354").front()).path_name == "Path1");
}

TEST_CASE("parse_data_path_graph throws on malformed delay reports") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto ss_path = directory / "cadd0040_bad_ss_delay.rpt";
    const auto ff_path = directory / "cadd0040_bad_ff_delay.rpt";

    write_file(ss_path,
               "Clock Period : 0.3\n"
               "#Path launch DFF -> capture DFF data path delay\n"
               "Path1 : FF_1 -> FF_2 0.10\n");
    write_file(ff_path,
               "Clock Period : 0.3\n"
               "#Path launch DFF -> capture DFF data path delay\n"
               "Path1 : FF_1 FF_2 0.10\n");

    cadd0040::DataPathGraph graph;
    CHECK_THROWS_AS(cadd0040::parse_data_path_graph(ff_path, ss_path, graph), std::runtime_error);
}
