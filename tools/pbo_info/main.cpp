#include "armatools/pbo.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../common/cli_logger.h"

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
    armatools::cli::print("Usage: pbo_info [flags] [input.pbo] [output_dir]");
    armatools::cli::print("Parses PBO archives and outputs structured JSON metadata.");
    armatools::cli::print("Reads from file argument or stdin (use - or omit argument).");
    armatools::cli::print("Output files:");
    armatools::cli::print("  pbo.json   - Full structured metadata (extensions, file list, checksum)");
    armatools::cli::print("  files.txt  - One file per line: <size>\\t<path>");
    armatools::cli::print("");
    armatools::cli::print("Flags:");
    armatools::cli::print("  --pretty   Pretty-print JSON output");
    armatools::cli::print("  --json     Write single JSON to stdout instead of files");
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    bool json_stdout = false;
    int verbosity = 0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) {
            pretty = true;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json_stdout = true;
        } else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbosity = std::min(verbosity + 1, 2);
        } else if (std::strcmp(argv[i], "-vv") == 0 || std::strcmp(argv[i], "--debug") == 0) {
            verbosity = 2;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    armatools::cli::set_verbosity(verbosity);

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
        LOGI("Reading PBO from stdin");
    } else {
        file_stream.open(positional[0], std::ios::binary);
        if (!file_stream) {
            LOGE("cannot open", positional[0]);
            return 1;
        }
        input = &file_stream;
        filename = fs::path(positional[0]).filename().string();
        LOGI("Reading", positional[0]);
        if (armatools::cli::debug_enabled()) {
            try {
                LOGD("Input size (bytes):", fs::file_size(positional[0]));
            } catch (const std::exception&) {
                LOGD("Input size unavailable for", positional[0]);
            }
        }
    }

    armatools::pbo::PBO p;
    try {
        p = armatools::pbo::read(*input);
    } catch (const std::exception& e) {
        LOGE("parsing", filename, e.what());
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
            LOGI("Writing outputs to", output_dir.string());
            write_output_files(doc, p, output_dir, pretty);
            LOGI("Output:", output_dir.string());
        }
    } catch (const std::exception& e) {
        LOGE("writing output:", e.what());
        return 1;
    }

    if (armatools::cli::verbose_enabled()) {
        LOGI("Total entries:", doc["totalFiles"].get<int>());
    }
    if (armatools::cli::debug_enabled() && !p.entries.empty()) {
        const auto& first = p.entries.front();
        LOGD("First entry:", first.filename,
                                 "size", first.data_size, "method", first.packing_method);
    }

    // Summary to stderr
    auto ext_it = p.extensions.find("prefix");
    std::string prefix_suffix;
    if (ext_it != p.extensions.end() && !ext_it->second.empty())
        prefix_suffix = " (prefix: " + ext_it->second + ")";
    LOGI("PBO:", filename + prefix_suffix);
    LOGI("Files:", doc["totalFiles"].get<int>(),
                              "Data size:", doc["totalDataSize"].get<int64_t>(),
                              "Original size:", doc["totalOriginalSize"].get<int64_t>());
    LOGI("SHA1:", doc["checksum"].get<std::string>());

    return 0;
}
