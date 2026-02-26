#include <gtest/gtest.h>

#include "armatools/p3d.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string shell_quote(const fs::path& p) {
    std::string s = p.string();
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

fs::path test_tmp_dir() {
    const auto base = fs::temp_directory_path() / "arma_tools_ir2mlod_tests";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base;
}

fs::path fixture_root() {
    return fs::path(ARMATOOLS_SOURCE_DIR) / "tests/fixtures/ir2mlod";
}

fs::path tool_path() {
#ifdef _WIN32
    return fs::path(ARMATOOLS_BINARY_DIR) / "tools/ir2mlod/ir2mlod.exe";
#else
    return fs::path(ARMATOOLS_BINARY_DIR) / "tools/ir2mlod/ir2mlod";
#endif
}

fs::path mlod2ir_tool_path() {
#ifdef _WIN32
    return fs::path(ARMATOOLS_BINARY_DIR) / "tools/mlod2ir/mlod2ir.exe";
#else
    return fs::path(ARMATOOLS_BINARY_DIR) / "tools/mlod2ir/mlod2ir";
#endif
}

armatools::p3d::P3DFile read_p3d(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    EXPECT_TRUE(f.is_open()) << p;
    return armatools::p3d::read(f);
}

json read_json(const fs::path& p) {
    std::ifstream f(p);
    EXPECT_TRUE(f.is_open()) << p;
    json j;
    f >> j;
    return j;
}

} // namespace

TEST(ir2mlod_roundtrip, tri_minimal_visual_upgrade_exports) {
    const auto tmp = test_tmp_dir() / "tri_minimal";
    fs::create_directories(tmp);

    const auto out_p3d = tmp / "out.p3d";
    const auto report = tmp / "report.json";

    const auto cmd = shell_quote(tool_path()) + " " +
        shell_quote(fixture_root() / "tri_minimal") +
        " -o " + shell_quote(out_p3d) +
        " --mode visual-upgrade --report " + shell_quote(report);

    EXPECT_EQ(run_command(cmd), 0);

    auto model = read_p3d(out_p3d);
    ASSERT_EQ(model.format, "MLOD");
    ASSERT_EQ(model.lods.size(), 1U);
    EXPECT_EQ(model.lods[0].vertex_count, 3);
    EXPECT_EQ(model.lods[0].face_count, 1);

    const auto rep = read_json(report);
    ASSERT_TRUE(rep.contains("lods"));
    ASSERT_FALSE(rep["lods"].empty());
}

TEST(ir2mlod_roundtrip, strict_mode_rejects_missing_uv_and_materials) {
    const auto tmp = test_tmp_dir() / "strict_fail";
    fs::create_directories(tmp);

    const auto out_p3d = tmp / "out.p3d";

    const auto cmd = shell_quote(tool_path()) + " " +
        shell_quote(fixture_root() / "tri_minimal") +
        " -o " + shell_quote(out_p3d) +
        " --mode strict";

    EXPECT_NE(run_command(cmd), 0);
}

TEST(ir2mlod_roundtrip, quad_selection_survives) {
    const auto tmp = test_tmp_dir() / "quad_selection";
    fs::create_directories(tmp);

    const auto out_p3d = tmp / "out.p3d";
    const auto cmd = shell_quote(tool_path()) + " " +
        shell_quote(fixture_root() / "quad_with_selection") +
        " -o " + shell_quote(out_p3d) + " --mode visual-upgrade --deterministic";

    EXPECT_EQ(run_command(cmd), 0);

    auto model = read_p3d(out_p3d);
    ASSERT_EQ(model.lods.size(), 1U);

    const auto& lod = model.lods[0];
    EXPECT_EQ(lod.vertex_count, 4);
    EXPECT_EQ(lod.face_count, 1);
    EXPECT_NE(std::find(lod.named_selections.begin(), lod.named_selections.end(), "component01"),
              lod.named_selections.end());

    auto it = lod.named_selection_vertices.find("component01");
    ASSERT_NE(it, lod.named_selection_vertices.end());
    EXPECT_EQ(it->second.size(), 4U);
}

TEST(ir2mlod_roundtrip, multi_lod_and_shadow_roundtrip) {
    const auto tmp = test_tmp_dir() / "multi_and_shadow";
    fs::create_directories(tmp);

    const auto out_multi = tmp / "multi.p3d";
    const auto cmd_multi = shell_quote(tool_path()) + " " +
        shell_quote(fixture_root() / "multi_lod") +
        " -o " + shell_quote(out_multi);
    EXPECT_EQ(run_command(cmd_multi), 0);

    auto multi = read_p3d(out_multi);
    ASSERT_EQ(multi.lods.size(), 2U);
    EXPECT_FLOAT_EQ(multi.lods[0].resolution, 0.0f);
    EXPECT_FLOAT_EQ(multi.lods[1].resolution, 1.0f);

    const auto out_shadow = tmp / "shadow.p3d";
    const auto cmd_shadow = shell_quote(tool_path()) + " " +
        shell_quote(fixture_root() / "shadow_present") +
        " -o " + shell_quote(out_shadow);
    EXPECT_EQ(run_command(cmd_shadow), 0);

    auto shadow = read_p3d(out_shadow);
    ASSERT_EQ(shadow.lods.size(), 2U);
    EXPECT_TRUE(shadow.lods[1].resolution_name.rfind("ShadowVolume", 0) == 0);
}

TEST(ir2mlod_roundtrip, missing_uv_with_materials_emits_warning_report) {
    const auto tmp = test_tmp_dir() / "missing_uv_materials";
    fs::create_directories(tmp);

    const auto out_p3d = tmp / "out.p3d";
    const auto report = tmp / "report.json";

    const auto cmd = shell_quote(tool_path()) + " " +
        shell_quote(fixture_root() / "missing_uv_has_materials") +
        " -o " + shell_quote(out_p3d) +
        " --mode visual-upgrade --report " + shell_quote(report);

    EXPECT_EQ(run_command(cmd), 0);

    const auto rep = read_json(report);
    ASSERT_TRUE(rep.contains("warnings"));

    bool saw_uv_warning = false;
    for (const auto& w : rep["warnings"]) {
        if (w.is_string() && w.get<std::string>().find("UV missing") != std::string::npos) {
            saw_uv_warning = true;
            break;
        }
    }
    EXPECT_TRUE(saw_uv_warning);
}

TEST(ir2mlod_roundtrip, mlod2ir_ir2mlod_roundtrip_geometry_and_selection) {
    const auto tmp = test_tmp_dir() / "roundtrip_chain";
    fs::create_directories(tmp);

    const auto first_p3d = tmp / "first.p3d";
    const auto ir_json = tmp / "roundtrip_ir.json";
    const auto second_p3d = tmp / "second.p3d";

    const auto cmd_first = shell_quote(tool_path()) + " " +
        shell_quote(fixture_root() / "quad_with_selection") +
        " -o " + shell_quote(first_p3d) + " --mode visual-upgrade";
    EXPECT_EQ(run_command(cmd_first), 0);

    const auto cmd_ir = shell_quote(mlod2ir_tool_path()) + " " +
        shell_quote(first_p3d) +
        " --object-type PROXY_MODEL --json > " + shell_quote(ir_json);
    EXPECT_EQ(run_command(cmd_ir), 0);

    const auto cmd_second = shell_quote(tool_path()) + " " +
        shell_quote(ir_json) + " -o " + shell_quote(second_p3d) +
        " --mode visual-upgrade";
    EXPECT_EQ(run_command(cmd_second), 0);

    const auto first = read_p3d(first_p3d);
    const auto second = read_p3d(second_p3d);

    ASSERT_FALSE(first.lods.empty());
    ASSERT_FALSE(second.lods.empty());
    EXPECT_EQ(first.lods[0].vertex_count, second.lods[0].vertex_count);
    EXPECT_EQ(first.lods[0].face_count, second.lods[0].face_count);

    const auto it = second.lods[0].named_selection_vertices.find("component01");
    EXPECT_NE(it, second.lods[0].named_selection_vertices.end());
    if (it != second.lods[0].named_selection_vertices.end()) {
        EXPECT_EQ(it->second.size(), 4U);
    }
}
