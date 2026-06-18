#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "parser.hpp"

namespace {

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path);
    REQUIRE(output);
    output << contents;
}

}  // namespace

TEST_CASE("parse_data_path_graph reads SS and FF delay reports") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto ss_path = directory / "cadd0040_ss_delay.rpt";
    const auto ff_path = directory / "cadd0040_ff_delay.rpt";

    write_file(ss_path,
               "Clock Period : 0.3\n"
               "#Path launch DFF -> capture DFF data path delay\n"
               "----------------------------------------------------------------------------\n"
               "Path1 : FF_1 -> FF_2 0.2377\n"
               "Path2 : FF_1 -> FF_3 0.1971\n"
               "Path3 : FF_4 -> FF_2 0.2238\n");
    write_file(ff_path,
               "Clock Period : 0.3\n"
               "#Path launch DFF -> capture DFF data path delay\n"
               "----------------------------------------------------------------------------\n"
               "Path1 : FF_1 -> FF_2 0.2461\n"
               "Path2 : FF_1 -> FF_3 0.1622\n"
               "Path3 : FF_4 -> FF_2 0.1641\n");

    cadd0040::DataPathGraph graph;

    cadd0040::parse_data_path_graph(ff_path, ss_path, graph);

    CHECK(graph.clock_period() == Catch::Approx(0.3));
    CHECK(graph.setup_time() == Catch::Approx(0.024));
    CHECK(graph.hold_time() == Catch::Approx(0.015));
    CHECK(graph.edge_count() == 3);

    const auto& first = graph.edge("Path1");
    CHECK(first.id == 0);
    CHECK(first.launch_flip_flop_name == "FF_1");
    CHECK(first.capture_flip_flop_name == "FF_2");
    CHECK(first.data_delay.ss == Catch::Approx(0.2377));
    CHECK(first.data_delay.ff == Catch::Approx(0.2461));

    CHECK(graph.incoming_edges("FF_2").size() == 2);
    CHECK(graph.outgoing_edges("FF_1").size() == 2);
    CHECK(graph.edge("Path1").id == graph.outgoing_edges("FF_1").front());
    CHECK(graph.incoming_edges("FF_MISSING").empty());
    CHECK(graph.outgoing_edges("FF_MISSING").empty());
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
