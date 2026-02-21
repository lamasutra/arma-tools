#include "armatools/p3d.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "../common/cli_logger.h"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static json build_json(const armatools::p3d::P3DFile& model, const std::string& filename) {
    json lods = json::array();
    std::set<std::string> tex_set;

    for (const auto& l : model.lods) {
        for (const auto& t : l.textures) {
            if (!t.empty()) tex_set.insert(t);
        }

        json lod_json = {
            {"index", l.index},
            {"resolution", l.resolution},
            {"resolutionName", l.resolution_name},
            {"vertices", l.vertex_count},
            {"faces", l.face_count},
            {"textures", l.textures},
        };

        if (!l.materials.empty()) lod_json["materials"] = l.materials;

        if (!l.named_properties.empty()) {
            json props = json::array();
            for (const auto& np : l.named_properties) {
                props.push_back({{"name", np.name}, {"value", np.value}});
            }
            lod_json["namedProperties"] = std::move(props);
        }

        if (!l.named_selections.empty()) lod_json["namedSelections"] = l.named_selections;

        lods.push_back(std::move(lod_json));
    }

    std::vector<std::string> all_textures(tex_set.begin(), tex_set.end());

    json doc = {
        {"schemaVersion", 1},
        {"filename", filename},
        {"format", model.format},
        {"version", model.version},
        {"lods", lods},
        {"textures", all_textures},
    };

    if (model.model_info) {
        const auto& mi = *model.model_info;
        doc["modelInfo"] = {
            {"boundingBoxMin", {mi.bounding_box_min[0], mi.bounding_box_min[1], mi.bounding_box_min[2]}},
            {"boundingBoxMax", {mi.bounding_box_max[0], mi.bounding_box_max[1], mi.bounding_box_max[2]}},
            {"boundingSphere", mi.bounding_sphere},
            {"centerOfMass", {mi.center_of_mass[0], mi.center_of_mass[1], mi.center_of_mass[2]}},
            {"mass", mi.mass},
        };
    }

    auto result = armatools::p3d::calculate_size(model);
    if (!result.warning.empty()) {
        std::cerr << "Warning: " << result.warning << '\n';
    }
    if (result.info) {
        const auto& si = *result.info;
        doc["size"] = {
            {"source", si.source},
            {"boundingBoxMin", {si.bbox_min[0], si.bbox_min[1], si.bbox_min[2]}},
            {"boundingBoxMax", {si.bbox_max[0], si.bbox_max[1], si.bbox_max[2]}},
            {"dimensions", {si.dimensions[0], si.dimensions[1], si.dimensions[2]}},
        };
    }

    return doc;
}

static void write_json(std::ostream& w, const json& doc, bool pretty) {
    if (pretty)
        w << std::setw(2) << doc << '\n';
    else
        w << doc << '\n';
}

static std::string version_string(const std::string& format, int version) {
    if (format == "ODOL" && version <= 7) return "ODOL v" + std::to_string(version) + ", OFP/CWA";
    if (format == "ODOL") return "ODOL v" + std::to_string(version) + ", Arma";
    if (format == "MLOD") return "MLOD v" + std::to_string(version);
    return format + " v" + std::to_string(version);
}

static void print_usage() {
    std::cerr << "Usage: p3d_info [flags] [input.p3d]\n\n"
              << "Extracts metadata from P3D model files (ODOL and MLOD formats).\n"
              << "Reads from file argument or stdin (use - or omit argument).\n\n"
              << "Output:\n"
              << "  p3d.json   - Full structured metadata (LODs, textures, model info)\n\n"
              << "Flags:\n"
              << "  --pretty   Pretty-print JSON output\n"
              << "  --json     Write JSON to stdout instead of file\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    bool json_stdout = false;
    int verbosity = 0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "--json") == 0) json_stdout = true;
        else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0)
            verbosity = std::min(verbosity + 1, 2);
        else if (std::strcmp(argv[i], "-vv") == 0 || std::strcmp(argv[i], "--debug") == 0)
            verbosity = 2;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
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
        std::ostringstream buf;
        buf << std::cin.rdbuf();
        stdin_stream = std::istringstream(buf.str());
        input = &stdin_stream;
        filename = "stdin";
        armatools::cli::log_verbose("Reading from stdin");
    } else {
        file_stream.open(positional[0], std::ios::binary);
        if (!file_stream) {
            std::cerr << "Error: cannot open " << positional[0] << '\n';
            return 1;
        }
        input = &file_stream;
        filename = fs::path(positional[0]).filename().string();
        armatools::cli::log_verbose("Reading", positional[0]);
        if (armatools::cli::debug_enabled()) {
            try {
                armatools::cli::log_debug("Size (bytes):", fs::file_size(positional[0]));
            } catch (const std::exception&) {
                // ignore missing file size
            }
        }
    }

    armatools::p3d::P3DFile model;
    try {
        model = armatools::p3d::read(*input);
    } catch (const std::exception& e) {
        std::cerr << "Error: parsing " << filename << ": " << e.what() << '\n';
        return 1;
    }

    auto doc = build_json(model, filename);

    try {
        if (json_stdout || from_stdin) {
            write_json(std::cout, doc, pretty);
        } else {
            std::string base = fs::path(positional[0]).stem().string();
            fs::path output_dir = fs::path(positional[0]).parent_path() / (base + "_p3d_info");
            armatools::cli::log_verbose("Writing to", output_dir.string());
            fs::create_directories(output_dir);
            std::ofstream jf(output_dir / "p3d.json");
            if (!jf) throw std::runtime_error("failed to create p3d.json");
            write_json(jf, doc, pretty);
            std::cerr << "Output: " << output_dir.string() << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: writing output: " << e.what() << '\n';
        return 1;
    }

    if (armatools::cli::verbose_enabled()) {
        armatools::cli::log_verbose("LOD count:", model.lods.size(), "Textures:", doc["textures"].size());
    }
    if (armatools::cli::debug_enabled()) {
        size_t limit = std::min<size_t>(model.lods.size(), 3);
        for (size_t i = 0; i < limit; ++i) {
            const auto& lod = model.lods[i];
            armatools::cli::log_debug("LOD", lod.index, "resolution", lod.resolution,
                                     "verts", lod.vertex_count, "faces", lod.face_count);
        }
        armatools::cli::log_debug("Total textures tracked", doc["textures"].size());
    }

    // Summary to stderr
    std::cerr << "P3D: " << filename << " (" << version_string(model.format, model.version) << ")\n";

    std::string lod_names;
    for (size_t i = 0; i < model.lods.size(); i++) {
        if (i > 0) lod_names += ", ";
        lod_names += model.lods[i].resolution_name;
    }
    std::cerr << "LODs: " << model.lods.size() << " (" << lod_names << ")\n";
    std::cerr << "Textures: " << doc["textures"].size() << " unique\n";
    if (!model.lods.empty()) {
        std::cerr << "Vertices: " << model.lods[0].vertex_count << " (LOD 0)\n";
    }
    if (doc.contains("size")) {
        auto& d = doc["size"]["dimensions"];
        std::cerr << "Size: " << d[0].get<float>() << " x " << d[1].get<float>() << " x " << d[2].get<float>()
                  << " m (from " << doc["size"]["source"].get<std::string>() << ")\n";
    }

    return 0;
}
