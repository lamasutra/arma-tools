#include "armatools/paa.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace fs = std::filesystem;

static void print_usage() {
    std::cerr << "Usage: paa2img [flags] <input.paa>\n\n"
              << "Converts a PAA texture to PNG.\n"
              << "Reads from file argument or stdin (use - or omit argument).\n\n"
              << "Flags:\n"
              << "  -o <path>  Output PNG path (use - for stdout)\n";
}

static void write_png_to_stream(std::ostream& out, const armatools::paa::Image& img) {
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            static_cast<std::ostream*>(ctx)->write(static_cast<const char*>(data), size);
        },
        &out, img.width, img.height, 4, img.pixels.data(), img.width * 4);
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

    bool from_stdin = positional.empty() || positional[0] == "-";
    std::string input_name;
    std::ifstream file_stream;
    std::istringstream stdin_stream;
    std::istream* input = nullptr;

    if (from_stdin) {
        std::ostringstream buf;
        buf << std::cin.rdbuf();
        stdin_stream = std::istringstream(buf.str());
        input = &stdin_stream;
        input_name = "stdin";
    } else {
        file_stream.open(positional[0], std::ios::binary);
        if (!file_stream) {
            std::cerr << "Error: cannot open " << positional[0] << '\n';
            return 1;
        }
        input = &file_stream;
        input_name = positional[0];
    }

    armatools::paa::Image img;
    armatools::paa::Header hdr;
    try {
        auto [i, h] = armatools::paa::decode(*input);
        img = std::move(i);
        hdr = std::move(h);
    } catch (const std::exception& e) {
        std::cerr << "Error: decoding " << input_name << ": " << e.what() << '\n';
        return 1;
    }

    std::cerr << "PAA: " << input_name << " (" << hdr.format << ", " << hdr.width << "x" << hdr.height << ")\n";

    if (output == "-" || (from_stdin && output.empty())) {
        write_png_to_stream(std::cout, img);
    } else {
        std::string out_path = output;
        if (out_path.empty()) {
            fs::path p(input_name);
            out_path = (p.parent_path() / p.stem()).string() + ".png";
        }
        if (!stbi_write_png(out_path.c_str(), img.width, img.height, 4, img.pixels.data(), img.width * 4)) {
            std::cerr << "Error: writing " << out_path << '\n';
            return 1;
        }
        std::cerr << "Output: " << out_path << '\n';
    }

    return 0;
}
