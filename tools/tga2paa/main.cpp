#include "armatools/paa.h"
#include "armatools/tga.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void print_usage() {
    std::cerr << "Usage: tga2paa [flags] <input.tga>\n\n"
              << "Converts TGA to PAA/PAC.\n"
              << "Validates that TGA dimensions are power-of-two.\n\n"
              << "Flags:\n"
              << "  -o <path>       Output PAA/PAC path\n"
              << "  -format <fmt>   DXT format: auto|dxt1|dxt3|dxt5 (default: auto)\n";
}

static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool is_pow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

int main(int argc, char* argv[]) {
    std::string output;
    std::string format = "auto";
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strcmp(argv[i], "-format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() != 1) {
        print_usage();
        return 2;
    }

    std::string in_path = positional[0];
    if (to_lower(fs::path(in_path).extension().string()) != ".tga") {
        std::cerr << "Error: input must be .tga: " << in_path << '\n';
        return 1;
    }

    std::string out_path = output;
    if (out_path.empty()) {
        fs::path p(in_path);
        out_path = (p.parent_path() / p.stem()).string() + ".paa";
    }
    std::string out_ext = to_lower(fs::path(out_path).extension().string());
    if (out_ext != ".paa" && out_ext != ".pac") {
        std::cerr << "Error: output must use .paa or .pac extension: " << out_path << '\n';
        return 1;
    }
    if (fs::exists(out_path)) {
        std::cerr << "Error: output already exists: " << out_path << '\n';
        return 1;
    }

    std::ifstream in(in_path, std::ios::binary);
    if (!in) {
        std::cerr << "Error: opening input: " << in_path << '\n';
        return 1;
    }

    armatools::tga::Image tga_img;
    try {
        tga_img = armatools::tga::decode(in);
    } catch (const std::exception& e) {
        std::cerr << "Error: decoding TGA: " << e.what() << '\n';
        return 1;
    }
    in.close();

    if (!is_pow2(tga_img.width) || !is_pow2(tga_img.height)) {
        std::cerr << "Error: TGA dimensions must be power-of-two (got "
                  << tga_img.width << "x" << tga_img.height << ")\n";
        return 1;
    }

    // Convert tga::Image to paa::Image
    armatools::paa::Image paa_img;
    paa_img.width = tga_img.width;
    paa_img.height = tga_img.height;
    paa_img.pixels = std::move(tga_img.pixels);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: creating output: " << out_path << '\n';
        return 1;
    }

    armatools::paa::Header hdr;
    try {
        hdr = armatools::paa::encode(out, paa_img, format);
    } catch (const std::exception& e) {
        out.close();
        fs::remove(out_path);
        std::cerr << "Error: encoding PAA: " << e.what() << '\n';
        return 1;
    }

    if (!out) {
        std::cerr << "Error: finalizing output: " << out_path << '\n';
        return 1;
    }

    std::cerr << "Output: " << out_path << " (" << hdr.format << " " << hdr.width << "x" << hdr.height << ")\n";
    return 0;
}
