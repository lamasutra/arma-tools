#include "armatools/ogg.h"
#include "armatools/pbo.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

struct Issue {
    std::string level;
    std::string message;
};

struct Result {
    std::string path;
    std::string encoder;
    int sample_rate = 0;
    int channels = 0;
    std::string status; // "ok", "info", "warn", "error"
    std::vector<Issue> issues;
};

static Result validate_ogg_data(const std::vector<uint8_t>& data, const std::string& path) {
    Result res;
    res.path = path;
    res.status = "ok";

    std::string str(data.begin(), data.end());
    std::istringstream stream(str);

    armatools::ogg::Header hdr;
    try {
        hdr = armatools::ogg::read_header(stream);
    } catch (const std::exception& e) {
        res.status = "error";
        res.issues.push_back({"error", std::format("parse: {}", e.what())});
        return res;
    }

    res.encoder = hdr.encoder;
    res.sample_rate = hdr.sample_rate;
    res.channels = hdr.channels;

    // Check 1: Pre-1.0 encoder
    if (armatools::ogg::is_pre_one_encoder(hdr.encoder)) {
        res.issues.push_back({"warn", std::format("pre-1.0 encoder ({})", hdr.encoder)});
    }

    // Check 2: Floor type 0
    if (hdr.floor_type == 0 && !hdr.codebooks.empty()) {
        res.issues.push_back({"warn", "uses floor type 0"});
    }

    // Check 3: lookup1Values precision risk
    for (size_t i = 0; i < hdr.codebooks.size(); i++) {
        const auto& cb = hdr.codebooks[i];
        if (cb.lookup_type == 1 && armatools::ogg::lookup1_values_precision_risk(cb.entries, cb.dimensions)) {
            res.issues.push_back({"warn",
                std::format("codebook {}: lookup1Values precision risk (entries={}, dims={})",
                            i, cb.entries, cb.dimensions)});
        }
    }

    // Check 4: Low sample rate
    if (hdr.sample_rate > 0 && hdr.sample_rate < 44100) {
        res.issues.push_back({"info", std::format("low sample rate ({} Hz)", hdr.sample_rate)});
    }

    // Determine overall status
    for (const auto& iss : res.issues) {
        if (iss.level == "error") {
            res.status = "error";
        } else if (iss.level == "warn" && res.status != "error") {
            res.status = "warn";
        } else if (iss.level == "info" && res.status == "ok") {
            res.status = "info";
        }
    }

    return res;
}

static Result validate_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {path, "", 0, 0, "error", {{"error", std::format("open: {}", path)}}};
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return validate_ogg_data(data, path);
}

static std::vector<Result> scan_pbo(const std::string& pbo_path) {
    std::ifstream f(pbo_path, std::ios::binary);
    if (!f) {
        std::cerr << "Error opening PBO " << pbo_path << '\n';
        return {};
    }

    armatools::pbo::PBO pbo;
    try {
        pbo = armatools::pbo::read(f);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing PBO " << pbo_path << ": " << e.what() << '\n';
        return {};
    }

    std::vector<Result> results;
    for (const auto& entry : pbo.entries) {
        std::string lower = entry.filename;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".ogg") continue;

        std::ostringstream buf;
        try {
            armatools::pbo::extract_file(f, entry, buf);
        } catch (const std::exception& e) {
            results.push_back({pbo_path + "::" + entry.filename, "", 0, 0, "error",
                              {{"error", std::format("extract: {}", e.what())}}});
            continue;
        }

        std::string s = buf.str();
        std::vector<uint8_t> data(s.begin(), s.end());
        results.push_back(validate_ogg_data(data, pbo_path + "::" + entry.filename));
    }

    return results;
}

static std::vector<Result> scan_dir(const std::string& dir) {
    std::vector<Result> results;
    for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        std::string path = entry.path().string();
        std::string lower = entry.path().filename().string();
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower.ends_with(".ogg")) {
            results.push_back(validate_file(path));
        } else if (lower.ends_with(".pbo")) {
            auto pbo_results = scan_pbo(path);
            results.insert(results.end(), pbo_results.begin(), pbo_results.end());
        }
    }
    return results;
}

static void print_result(const Result& r) {
    std::string label = r.status;
    for (auto& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::cout << r.path << ": " << label << '\n';
    if (!r.encoder.empty()) {
        std::cout << "  encoder: " << r.encoder << '\n';
    }
    if (r.sample_rate > 0) {
        std::cout << "  sample_rate: " << r.sample_rate << ", channels: " << r.channels << '\n';
    }
    for (const auto& iss : r.issues) {
        std::cout << "  [" << iss.level << "] " << iss.message << '\n';
    }
}

static void print_usage() {
    std::cerr << "Usage: ogg_validate [flags] [file.ogg|file.pbo|dir ...]\n\n"
              << "Validate OGG/Vorbis files for compatibility issues.\n\n"
              << "Modes:\n"
              << "  File mode (default)   Validate OGG files from arguments\n"
              << "  PBO mode              Auto-detected from .pbo extension\n"
              << "  Directory mode (-r)   Recursively scan for .ogg and .pbo files\n\n"
              << "Checks:\n"
              << "  old-encoder           Pre-1.0 Vorbis encoder (WARN)\n"
              << "  floor-type-0          Uses deprecated floor type 0 (WARN)\n"
              << "  lookup1values         Codebook triggers float precision bug (WARN)\n"
              << "  low-sample-rate       Sample rate below 44100 Hz (INFO)\n\n"
              << "Flags:\n"
              << "  -r         Recursively scan directories\n"
              << "  --json     JSON output\n"
              << "  --warn     Show only files with warnings/errors\n";
}

int main(int argc, char* argv[]) {
    bool recursive = false;
    bool json_out = false;
    bool warn_only = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-r") == 0) recursive = true;
        else if (std::strcmp(argv[i], "--json") == 0) json_out = true;
        else if (std::strcmp(argv[i], "--warn") == 0) warn_only = true;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.empty()) {
        print_usage();
        return 1;
    }

    std::vector<Result> results;

    for (const auto& arg : positional) {
        std::error_code ec;
        auto status = fs::status(arg, ec);
        if (ec) {
            std::cerr << "Error: " << ec.message() << ": " << arg << '\n';
            continue;
        }

        if (fs::is_directory(status)) {
            if (recursive) {
                auto dir_results = scan_dir(arg);
                results.insert(results.end(), dir_results.begin(), dir_results.end());
            } else {
                std::cerr << "Skipping directory " << arg << " (use -r for recursive scan)\n";
            }
            continue;
        }

        std::string lower = arg;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower.ends_with(".pbo")) {
            auto pbo_results = scan_pbo(arg);
            results.insert(results.end(), pbo_results.begin(), pbo_results.end());
        } else {
            results.push_back(validate_file(arg));
        }
    }

    if (warn_only) {
        std::erase_if(results, [](const Result& r) {
            return r.status != "warn" && r.status != "error";
        });
    }

    if (json_out) {
        json arr = json::array();
        for (const auto& r : results) {
            json issues = json::array();
            for (const auto& iss : r.issues) {
                issues.push_back({{"level", iss.level}, {"message", iss.message}});
            }
            json obj = {
                {"path", r.path},
                {"encoder", r.encoder},
                {"sampleRate", r.sample_rate},
                {"channels", r.channels},
                {"status", r.status},
            };
            if (!issues.empty()) obj["issues"] = issues;
            arr.push_back(obj);
        }
        std::cout << std::setw(2) << arr << '\n';
    } else {
        for (const auto& r : results) {
            print_result(r);
        }
        int ok_count = 0, warn_count = 0, err_count = 0;
        for (const auto& r : results) {
            if (r.status == "ok" || r.status == "info") ok_count++;
            else if (r.status == "warn") warn_count++;
            else if (r.status == "error") err_count++;
        }
        int total = ok_count + warn_count + err_count;
        if (total > 0) {
            std::cerr << std::format("\nScanned {} files: {} ok, {} warnings, {} errors\n",
                                      total, ok_count, warn_count, err_count);
        }
    }

    return 0;
}
