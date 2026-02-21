#include "armatools/pbo.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static std::string hex_encode(const std::vector<uint8_t>& data) {
    std::ostringstream ss;
    for (uint8_t b : data)
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return ss.str();
}

static json build_json(const armatools::pbo::PBO& p, const std::string& filename) {
    json files = json::array();
    int64_t total_data = 0;
    int64_t total_original = 0;

    for (const auto& e : p.entries) {
        files.push_back({
            {"path", e.filename},
            {"packingMethod", e.packing_method},
            {"originalSize", e.original_size},
            {"dataSize", e.data_size},
            {"timestamp", e.timestamp},
            {"offset", e.data_offset},
        });
        total_data += e.data_size;
        total_original += e.original_size;
    }

    json extensions = json::object();
    for (const auto& [k, v] : p.extensions)
        extensions[k] = v;

    return {
        {"schemaVersion", 1},
        {"filename", filename},
        {"extensions", extensions},
        {"totalFiles", static_cast<int>(p.entries.size())},
        {"totalDataSize", total_data},
        {"totalOriginalSize", total_original},
        {"checksum", hex_encode(p.checksum)},
        {"files", files},
    };
}

static void write_json(std::ostream& out, const json& doc, bool pretty) {
    if (pretty)
        out << std::setw(2) << doc << '\n';
    else
        out << doc << '\n';
}

static void write_output_files(const json& doc, const armatools::pbo::PBO& p,
                                const fs::path& output_dir, bool pretty) {
    fs::create_directories(output_dir);

    // pbo.json
    std::ofstream jf(output_dir / "pbo.json");
    if (!jf) throw std::runtime_error("failed to create pbo.json");
    write_json(jf, doc, pretty);

    // files.txt
    std::ofstream ft(output_dir / "files.txt");
    if (!ft) throw std::runtime_error("failed to create files.txt");
    for (const auto& e : p.entries)
        ft << e.data_size << '\t' << e.filename << '\n';
}

static void print_usage() {
    std::cerr << "Usage: pbo_info [flags] [input.pbo] [output_dir]\n\n"
              << "Parses PBO archives and outputs structured JSON metadata.\n"
              << "Reads from file argument or stdin (use - or omit argument).\n\n"
              << "Output files:\n"
              << "  pbo.json   - Full structured metadata (extensions, file list, checksum)\n"
              << "  files.txt  - One file per line: <size>\\t<path>\n\n"
              << "Flags:\n"
              << "  --pretty   Pretty-print JSON output\n"
              << "  --json     Write single JSON to stdout instead of files\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    bool json_stdout = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) {
            pretty = true;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json_stdout = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    bool from_stdin = positional.empty() || positional[0] == "-";
    std::string filename;
    std::ifstream file_stream;
    std::istringstream stdin_stream;

    std::istream* input = nullptr;

    if (from_stdin) {
        // Read all of stdin into memory (need seekable stream)
        std::ostringstream buf;
        buf << std::cin.rdbuf();
        stdin_stream = std::istringstream(buf.str());
        input = &stdin_stream;
        filename = "stdin";
    } else {
        file_stream.open(positional[0], std::ios::binary);
        if (!file_stream) {
            std::cerr << "Error: cannot open " << positional[0] << '\n';
            return 1;
        }
        input = &file_stream;
        filename = fs::path(positional[0]).filename().string();
    }

    armatools::pbo::PBO p;
    try {
        p = armatools::pbo::read(*input);
    } catch (const std::exception& e) {
        std::cerr << "Error: parsing " << filename << ": " << e.what() << '\n';
        return 1;
    }

    auto doc = build_json(p, filename);

    try {
        if (json_stdout || from_stdin) {
            write_json(std::cout, doc, pretty);
        } else {
            fs::path output_dir;
            if (positional.size() >= 2) {
                output_dir = positional[1];
            } else {
                fs::path input_path(positional[0]);
                std::string stem = input_path.stem().string();
                output_dir = input_path.parent_path() / (stem + "_pbo_info");
            }
            write_output_files(doc, p, output_dir, pretty);
            std::cerr << "Output: " << output_dir.string() << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: writing output: " << e.what() << '\n';
        return 1;
    }

    // Summary to stderr
    auto ext_it = p.extensions.find("prefix");
    std::cerr << "PBO: " << filename;
    if (ext_it != p.extensions.end() && !ext_it->second.empty())
        std::cerr << " (prefix: " << ext_it->second << ")";
    std::cerr << '\n';
    std::cerr << "Files: " << doc["totalFiles"].get<int>()
              << ", Data size: " << doc["totalDataSize"].get<int64_t>()
              << ", Original size: " << doc["totalOriginalSize"].get<int64_t>() << '\n';
    std::cerr << "SHA1: " << doc["checksum"].get<std::string>() << '\n';

    return 0;
}
