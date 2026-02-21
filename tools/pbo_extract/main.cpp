#include "armatools/pbo.h"
#include "armatools/armapath.h"
#include "armatools/config.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static bool derap_flag = false;
static bool auto_derap_flag = false;

static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Simple glob match supporting * and ? wildcards.
static bool glob_match(const std::string& pattern, const std::string& str) {
    size_t pi = 0, si = 0;
    size_t star_p = std::string::npos, star_s = 0;

    while (si < str.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == str[si])) {
            pi++;
            si++;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_p = pi++;
            star_s = si;
        } else if (star_p != std::string::npos) {
            pi = star_p + 1;
            si = ++star_s;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') pi++;
    return pi == pattern.size();
}

static bool should_derap(const armatools::pbo::Entry& entry) {
    if (derap_flag) return true;
    if (auto_derap_flag) {
        std::string base = armatools::armapath::to_slash_lower(entry.filename);
        auto pos = base.rfind('/');
        if (pos != std::string::npos) base = base.substr(pos + 1);
        return base == "config.bin";
    }
    return false;
}

static const std::string rap_signature("\x00raP", 4);

static void extract_derap(std::istream& r, const armatools::pbo::Entry& entry, std::ostream& w) {
    std::ostringstream buf;
    armatools::pbo::extract_file(r, entry, buf);
    std::string data = buf.str();

    if (data.size() >= 4 && data.substr(0, 4) == rap_signature) {
        std::istringstream is(data);
        auto cfg = armatools::config::read(is);
        armatools::config::write_text(w, cfg);
    } else {
        w.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
}

static std::string extract_derap_to_file(std::istream& r, const armatools::pbo::Entry& entry, std::string out_path) {
    std::ostringstream buf;
    armatools::pbo::extract_file(r, entry, buf);
    std::string data = buf.str();

    bool is_rapified = data.size() >= 4 && data.substr(0, 4) == rap_signature;
    if (is_rapified) {
        std::string lower = to_lower(out_path);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bin") {
            out_path = out_path.substr(0, out_path.size() - 4) + ".cpp";
        }
    }

    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("creating " + out_path);

    if (is_rapified) {
        std::istringstream is(data);
        auto cfg = armatools::config::read(is);
        armatools::config::write_text(out, cfg);
    } else {
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    return out_path;
}

static void print_usage() {
    std::cerr << "Usage: pbo_extract [-derap|-auto-derap] <input.pbo> <output_dir> [patterns...]\n\n"
              << "Extracts files from a PBO archive.\n\n"
              << "  -derap         Debinarize all rapified files to text (.cpp)\n"
              << "  -auto-derap    Debinarize only config.bin files to text (.cpp)\n"
              << "  No patterns    Extract all files\n"
              << "  patterns       Extract only files matching any pattern (case-insensitive)\n"
              << "                 Supports * and ? wildcards\n\n"
              << "If one pattern matches exactly one file and output_dir doesn't exist,\n"
              << "the file is extracted directly as output_dir (like cp).\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-derap") == 0 || std::strcmp(argv[i], "--derap") == 0) {
            derap_flag = true;
        } else if (std::strcmp(argv[i], "-auto-derap") == 0 || std::strcmp(argv[i], "--auto-derap") == 0) {
            auto_derap_flag = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() < 2) {
        print_usage();
        return 1;
    }

    std::string input_path = positional[0];
    std::string output_dir = positional[1];
    std::vector<std::string> patterns;
    for (size_t i = 2; i < positional.size(); i++) {
        patterns.push_back(armatools::armapath::to_slash_lower(positional[i]));
    }

    std::ifstream f(input_path, std::ios::binary);
    if (!f) {
        std::cerr << "Error: cannot open " << input_path << '\n';
        return 1;
    }

    armatools::pbo::PBO p;
    try {
        p = armatools::pbo::read(f);
    } catch (const std::exception& e) {
        std::cerr << "Error: parsing " << input_path << ": " << e.what() << '\n';
        return 1;
    }

    // Select matching entries.
    std::vector<armatools::pbo::Entry> matched;
    for (const auto& entry : p.entries) {
        if (patterns.empty()) {
            matched.push_back(entry);
            continue;
        }
        std::string norm = armatools::armapath::to_slash_lower(entry.filename);
        for (const auto& pat : patterns) {
            if (glob_match(pat, norm)) {
                matched.push_back(entry);
                break;
            }
        }
    }

    if (matched.empty()) {
        std::cerr << "No files matched.\n";
        return 1;
    }

    // Stdout mode
    if (output_dir == "-") {
        if (matched.size() != 1) {
            std::cerr << "Error: stdout output (-) requires exactly one matching file, got " << matched.size() << '\n';
            return 1;
        }
        try {
            if (should_derap(matched[0])) {
                extract_derap(f, matched[0], std::cout);
            } else {
                armatools::pbo::extract_file(f, matched[0], std::cout);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n';
            return 1;
        }
        std::cerr << "  " << matched[0].data_size << "  " << matched[0].filename << '\n';
        return 0;
    }

    // Single-file shortcut
    bool single_file = false;
    if (matched.size() == 1 && !fs::exists(output_dir)) {
        single_file = true;
    }

    int64_t total_size = 0;
    int extract_count = 0;

    try {
        if (single_file) {
            auto& entry = matched[0];
            fs::path dir = fs::path(output_dir).parent_path();
            if (!dir.empty()) fs::create_directories(dir);

            std::string out_path = output_dir;
            if (should_derap(entry)) {
                out_path = extract_derap_to_file(f, entry, out_path);
            } else {
                std::ofstream out(out_path, std::ios::binary);
                if (!out) throw std::runtime_error("creating " + out_path);
                armatools::pbo::extract_file(f, entry, out);
            }
            std::cerr << "  " << entry.data_size << "  " << entry.filename << " -> " << out_path << '\n';
            total_size = entry.data_size;
            extract_count = 1;
        } else {
            auto canon_root = fs::weakly_canonical(fs::path(output_dir));
            for (auto& entry : matched) {
                std::string rel = armatools::armapath::to_slash(entry.filename);
                fs::path out_path = fs::path(output_dir) / armatools::armapath::to_os(rel);

                // Prevent directory traversal via .. or absolute paths in entry names
                auto canon_out = fs::weakly_canonical(out_path);
                auto root_str = canon_root.string() + std::string(1, fs::path::preferred_separator);
                if (canon_out.string() != canon_root.string() &&
                    !canon_out.string().starts_with(root_str)) {
                    std::cerr << "  SKIPPED (path escapes output dir): " << entry.filename << '\n';
                    continue;
                }

                fs::create_directories(out_path.parent_path());

                std::string out_str = out_path.string();
                if (should_derap(entry)) {
                    out_str = extract_derap_to_file(f, entry, out_str);
                } else {
                    std::ofstream out(out_str, std::ios::binary);
                    if (!out) throw std::runtime_error("creating " + out_str);
                    armatools::pbo::extract_file(f, entry, out);
                }
                std::cerr << "  " << entry.data_size << "  " << entry.filename << '\n';
                total_size += entry.data_size;
                extract_count++;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    std::cerr << "Extracted " << extract_count << " file(s), " << total_size << " bytes total\n";
    return 0;
}
