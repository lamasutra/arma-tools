#include "armatools/heightpipe.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace hp = armatools::heightpipe;

struct Cli {
    std::string in_path;
    std::string out_path;
    int in_width = 0;
    int in_height = 0;
    int scale = 2;
    hp::ResampleMethod resample = hp::ResampleMethod::Bicubic;
    hp::CorrectionMode correction_mode = hp::CorrectionMode::Preset;
    hp::CorrectionPreset preset = hp::CorrectionPreset::Sharp;
    bool macro = true;
    bool meso = true;
    bool micro = true;
    uint32_t seed = 1;
    bool dump = false;
    std::string dump_slope;
    std::string dump_curv;
    std::string dump_flow;
};

static void usage() {
    std::cerr
        << "Usage: heightpipe <input.rawf32> <output.rawf32> --in-width N --in-height N\n"
        << "       --scale {2|4|8|16} --resample bicubic|lanczos3\n"
        << "       --correction preset|none|unsharp|curv_gain|residual|guided_sharp|hybrid|terrain_16x\n"
        << "       --macro 0|1 --meso 0|1 --micro 0|1 --seed N\n"
        << "       [--dump slope.raw curvature.raw flow.raw]\n\n"
        << "RAW format: little-endian float32 array, row-major, no header.\n";
}

static bool parse_bool01(const char* s) {
    return std::strcmp(s, "1") == 0 || std::strcmp(s, "true") == 0;
}

static hp::CorrectionPreset parse_preset(const std::string& s) {
    if (s == "none") return hp::CorrectionPreset::None;
    if (s == "sharp") return hp::CorrectionPreset::Sharp;
    if (s == "retain_detail") return hp::CorrectionPreset::RetainDetail;
    return hp::CorrectionPreset::Terrain16x;
}

static hp::CorrectionMode parse_mode(const std::string& s, hp::CorrectionPreset* preset) {
    if (s == "preset") {
        *preset = hp::CorrectionPreset::Sharp;
        return hp::CorrectionMode::Preset;
    }
    if (s == "terrain_16x") {
        *preset = hp::CorrectionPreset::Terrain16x;
        return hp::CorrectionMode::Preset;
    }
    if (s == "none") return hp::CorrectionMode::None;
    if (s == "unsharp") return hp::CorrectionMode::Unsharp;
    if (s == "curv_gain") return hp::CorrectionMode::CurvatureGain;
    if (s == "residual") return hp::CorrectionMode::Residual;
    if (s == "guided_sharp") return hp::CorrectionMode::GuidedSharp;
    return hp::CorrectionMode::Hybrid;
}

static hp::ResampleMethod parse_resample(const std::string& s) {
    return s == "lanczos3" ? hp::ResampleMethod::Lanczos3 : hp::ResampleMethod::Bicubic;
}

static int parse_cli(int argc, char** argv, Cli& cli) {
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--in-width") == 0 && i + 1 < argc) cli.in_width = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--in-height") == 0 && i + 1 < argc) cli.in_height = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) cli.scale = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--resample") == 0 && i + 1 < argc) cli.resample = parse_resample(argv[++i]);
        else if (std::strcmp(argv[i], "--correction") == 0 && i + 1 < argc) cli.correction_mode = parse_mode(argv[++i], &cli.preset);
        else if (std::strcmp(argv[i], "--preset") == 0 && i + 1 < argc) cli.preset = parse_preset(argv[++i]);
        else if (std::strcmp(argv[i], "--macro") == 0 && i + 1 < argc) cli.macro = parse_bool01(argv[++i]);
        else if (std::strcmp(argv[i], "--meso") == 0 && i + 1 < argc) cli.meso = parse_bool01(argv[++i]);
        else if (std::strcmp(argv[i], "--micro") == 0 && i + 1 < argc) cli.micro = parse_bool01(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) cli.seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (std::strcmp(argv[i], "--dump") == 0 && i + 3 < argc) {
            cli.dump = true;
            cli.dump_slope = argv[++i];
            cli.dump_curv = argv[++i];
            cli.dump_flow = argv[++i];
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            usage();
            return 1;
        } else {
            pos.emplace_back(argv[i]);
        }
    }

    if (pos.size() != 2) {
        usage();
        return -1;
    }
    cli.in_path = pos[0];
    cli.out_path = pos[1];

    if ((cli.scale != 2 && cli.scale != 4 && cli.scale != 8 && cli.scale != 16) || cli.in_width <= 0 || cli.in_height <= 0) {
        usage();
        return -1;
    }
    return 0;
}

static hp::Heightmap read_raw(const std::string& path, int w, int h) {
    hp::Heightmap hm(w, h, 0.0f);
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input: " + path);
    in.read(reinterpret_cast<char*>(hm.data.data()), static_cast<std::streamsize>(hm.data.size() * sizeof(float)));
    if (in.gcount() != static_cast<std::streamsize>(hm.data.size() * sizeof(float))) {
        throw std::runtime_error("input size mismatch for " + path);
    }
    return hm;
}

static void write_raw(const std::string& path, const hp::Heightmap& hm) {
    fs::path p(path);
    if (!p.parent_path().empty()) fs::create_directories(p.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write output: " + path);
    out.write(reinterpret_cast<const char*>(hm.data.data()), static_cast<std::streamsize>(hm.data.size() * sizeof(float)));
    if (!out) throw std::runtime_error("failed while writing: " + path);
}

int main(int argc, char** argv) {
    Cli cli;
    const int parse_result = parse_cli(argc, argv, cli);
    if (parse_result != 0) {
        if (parse_result > 0) return 0;
        return 1;
    }

    try {
        const hp::Heightmap in = read_raw(cli.in_path, cli.in_width, cli.in_height);

        hp::PipelineOptions opt;
        opt.scale = cli.scale;
        opt.resample = cli.resample;
        opt.seed = cli.seed;
        opt.dump_slope = cli.dump;
        opt.dump_curvature = cli.dump;
        opt.dump_flow = cli.dump;

        if (cli.correction_mode == hp::CorrectionMode::Preset) {
            opt.correction = hp::correction_preset_for_scale(cli.scale, cli.preset);
        } else {
            opt.correction = hp::correction_preset_for_scale(cli.scale, hp::CorrectionPreset::Sharp);
            opt.correction.enable_unsharp = false;
            opt.correction.enable_curvature = false;
            opt.correction.enable_residual = false;
            opt.correction.enable_guided_sharp = false;
            opt.correction.enable_noise = false;
            opt.correction.mode = cli.correction_mode;
        }

        opt.erosion = hp::erosion_preset_for_scale(cli.scale);
        opt.erosion.enable_macro = cli.macro;
        opt.erosion.enable_meso = cli.meso;
        opt.erosion.enable_micro = cli.micro;

        const hp::PipelineOutputs outputs = hp::run_pipeline(in, opt);

        write_raw(cli.out_path, outputs.out);
        if (cli.dump) {
            if (outputs.slope) write_raw(cli.dump_slope, *outputs.slope);
            if (outputs.curvature) write_raw(cli.dump_curv, *outputs.curvature);
            if (outputs.flow) write_raw(cli.dump_flow, *outputs.flow);
        }

        std::cerr << "heightpipe: " << in.width << "x" << in.height
                  << " -> " << outputs.out.width << "x" << outputs.out.height
                  << " (scale " << cli.scale << ")\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
