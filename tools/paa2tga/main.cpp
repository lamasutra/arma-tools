#include "armatools/paa.h"
#include "armatools/tga.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void print_usage() {
    std::cerr << "Usage: paa2tga [flags] <input.paa|input.pac>\n\n"
              << "Converts PAA/PAC to TGA.\n\n"
              << "Flags:\n"
              << "  -o <path>  Output TGA path\n";
}

static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int main(int argc, char* argv[]) {
    std::string output;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
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
    std::string ext = to_lower(fs::path(in_path).extension().string());
    if (ext != ".paa" && ext != ".pac") {
        std::cerr << "Error: input must be .paa or .pac: " << in_path << '\n';
        return 1;
    }

    std::string out_path = output;
    if (out_path.empty()) {
        fs::path p(in_path);
        out_path = (p.parent_path() / p.stem()).string() + ".tga";
    }
    if (to_lower(fs::path(out_path).extension().string()) != ".tga") {
        std::cerr << "Error: output must use .tga extension: " << out_path << '\n';
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

    armatools::paa::Image paa_img;
    armatools::paa::Header hdr;
    try {
        auto [i, h] = armatools::paa::decode(in);
        paa_img = std::move(i);
        hdr = std::move(h);
    } catch (const std::exception& e) {
        std::cerr << "Error: decoding PAA: " << e.what() << '\n';
        return 1;
    }

    // Convert paa::Image to tga::Image (same pixel format)
    armatools::tga::Image tga_img;
    tga_img.width = paa_img.width;
    tga_img.height = paa_img.height;
    tga_img.pixels = std::move(paa_img.pixels);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: creating output: " << out_path << '\n';
        return 1;
    }

    try {
        armatools::tga::encode(out, tga_img);
    } catch (const std::exception& e) {
        std::cerr << "Error: encoding TGA: " << e.what() << '\n';
        return 1;
    }

    std::cerr << "Output: " << out_path << " (" << hdr.format << " " << hdr.width << "x" << hdr.height << ")\n";
    return 0;
}
