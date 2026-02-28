#include "armatools/armapath.h"
#include "armatools/config.h"
#include "armatools/p3d.h"
#include "armatools/pbo.h"
#include "armatools/pboindex.h"
#include "armatools/rvmat.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../common/cli_logger.h"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace {

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string normalize_path(const std::string& p) {
    return armatools::armapath::to_slash_lower(p);
}

std::vector<uint8_t> extract_from_pbo(const std::string& pbo_path,
                                      const std::string& entry_name) {
    std::ifstream f(pbo_path, std::ios::binary);
    if (!f.is_open()) return {};

    auto pbo = armatools::pbo::read(f);
    auto target = normalize_path(entry_name);

    for (const auto& e : pbo.entries) {
        if (normalize_path(e.filename) != target) continue;
        std::ostringstream out;
        armatools::pbo::extract_file(f, e, out);
        auto s = out.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    return {};
}

armatools::rvmat::Material parse_rvmat_bytes(const std::vector<uint8_t>& data) {
    if (data.empty()) throw std::runtime_error("rvmat: empty data");
    std::string s(reinterpret_cast<const char*>(data.data()), data.size());
    std::istringstream iss(s, std::ios::binary);

    armatools::config::Config cfg;
    if (data.size() >= 4 && data[0] == 0x00 && data[1] == 'r' && data[2] == 'a' && data[3] == 'P')
        cfg = armatools::config::read(iss);
    else
        cfg = armatools::config::parse_text(iss);

    return armatools::rvmat::parse(cfg);
}

json rgba_to_json(const std::array<float, 4>& c) {
    return json::array({c[0], c[1], c[2], c[3]});
}

json vec3_to_json(const std::array<float, 3>& v) {
    return json::array({v[0], v[1], v[2]});
}

json material_to_json(const armatools::rvmat::Material& m) {
    json stages = json::array();
    for (const auto& st : m.stages) {
        json stage = {
            {"stageNumber", st.stage_number},
            {"className", st.class_name},
            {"texturePath", st.texture_path},
            {"uvSource", st.uv_source},
            {"filter", st.filter},
            {"texGen", st.tex_gen},
        };
        if (st.uv_transform.valid) {
            stage["uvTransform"] = {
                {"aside", vec3_to_json(st.uv_transform.aside)},
                {"up", vec3_to_json(st.uv_transform.up)},
                {"pos", vec3_to_json(st.uv_transform.pos)},
            };
        }
        stages.push_back(std::move(stage));
    }

    return {
        {"pixelShader", m.pixel_shader},
        {"vertexShader", m.vertex_shader},
        {"ambient", rgba_to_json(m.ambient)},
        {"diffuse", rgba_to_json(m.diffuse)},
        {"forcedDiffuse", rgba_to_json(m.forced_diffuse)},
        {"emissive", rgba_to_json(m.emissive)},
        {"specular", rgba_to_json(m.specular)},
        {"specularPower", m.specular_power},
        {"surface", m.surface},
        {"stages", std::move(stages)},
    };
}

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
        LOGW(result.warning);
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
    armatools::cli::print("Usage: p3d_info [flags] [input.p3d]");
    armatools::cli::print("       p3d_info --rvmat <input.rvmat> [--pretty]");
    armatools::cli::print("       p3d_info --materials <model.p3d> [--db <a3.db>] [--drive-root <path>] [--pretty]");
    armatools::cli::print("Extracts metadata from P3D model files and RVMAT material files.");
    armatools::cli::print("Output:");
    armatools::cli::print("  p3d.json   - Full structured metadata (LODs, textures, model info)");
    armatools::cli::print("  rvmat JSON - Material properties and texture stages");
    armatools::cli::print("");
    armatools::cli::print("Flags:");
    armatools::cli::print("  --pretty           Pretty-print JSON output");
    armatools::cli::print("  --json             Write P3D JSON to stdout instead of file");
    armatools::cli::print("  --rvmat <path>     Parse one RVMAT and print JSON");
    armatools::cli::print("  --materials <p3d>  Parse model and resolve all referenced RVMATs");
    armatools::cli::print("  --db <path>        A3DB path used to resolve files from PBOs");
    armatools::cli::print("  --drive-root <dir> Disk root used for virtual path fallback (e.g. P:)");
}

struct ResolvedRvmat {
    std::string reference;
    std::string resolved_path;
    std::string source;
    bool loaded = false;
    std::string error;
    armatools::rvmat::Material material;
};

bool looks_like_rvmat_path(const std::string& path) {
    auto lower = to_lower_ascii(path);
    return lower.ends_with(".rvmat");
}

bool try_parse_rvmat_file(const fs::path& path, ResolvedRvmat& out) {
    try {
        out.material = armatools::rvmat::parse(path);
        out.resolved_path = path.string();
        out.source = "disk";
        out.loaded = true;
        return true;
    } catch (const std::exception& e) {
        out.error = e.what();
        return false;
    }
}

bool try_parse_rvmat_from_pbo(const std::string& pbo_path,
                              const std::string& entry_name,
                              ResolvedRvmat& out) {
    try {
        auto data = extract_from_pbo(pbo_path, entry_name);
        if (data.empty()) {
            out.error = "empty or missing PBO entry";
            return false;
        }
        out.material = parse_rvmat_bytes(data);
        out.resolved_path = pbo_path + ":" + entry_name;
        out.source = "pbo";
        out.loaded = true;
        return true;
    } catch (const std::exception& e) {
        out.error = e.what();
        return false;
    }
}

ResolvedRvmat resolve_rvmat(const std::string& rvmat_ref,
                            const std::string& model_path,
                            const std::shared_ptr<armatools::pboindex::Index>& index,
                            const std::shared_ptr<armatools::pboindex::DB>& db,
                            const std::string& drive_root) {
    ResolvedRvmat out;
    out.reference = rvmat_ref;

    if (!looks_like_rvmat_path(rvmat_ref)) {
        out.error = "not an .rvmat reference";
        return out;
    }

    std::error_code ec;
    fs::path ref_fs = armatools::armapath::to_os(rvmat_ref);
    std::vector<fs::path> candidates;

    candidates.push_back(ref_fs);

    fs::path model_dir;
    if (!model_path.empty()) model_dir = fs::path(model_path).parent_path();
    if (!model_dir.empty()) {
        candidates.push_back(model_dir / ref_fs);
        candidates.push_back(model_dir / ref_fs.filename());
    }

    if (!drive_root.empty()) {
        candidates.push_back(fs::path(drive_root) / ref_fs);
        auto ci = armatools::armapath::find_file_ci(fs::path(drive_root), rvmat_ref);
        if (ci) candidates.push_back(*ci);
    }

    for (const auto& c : candidates) {
        if (c.empty()) continue;
        if (!fs::exists(c, ec)) continue;
        if (try_parse_rvmat_file(c, out)) return out;
    }

    auto norm = normalize_path(rvmat_ref);

    if (index) {
        armatools::pboindex::ResolveResult rr;
        if (index->resolve(norm, rr) && try_parse_rvmat_from_pbo(rr.pbo_path, rr.entry_name, out))
            return out;
        if (index->resolve(rvmat_ref, rr) && try_parse_rvmat_from_pbo(rr.pbo_path, rr.entry_name, out))
            return out;
    }

    if (db) {
        auto file = fs::path(norm).filename().string();
        auto hits = db->find_files("*" + file);
        for (const auto& hit : hits) {
            auto full = normalize_path(hit.prefix + "/" + hit.file_path);
            if (full == norm || full.ends_with("/" + norm) ||
                normalize_path(hit.file_path).ends_with("/" + file) ||
                normalize_path(hit.file_path) == file) {
                if (try_parse_rvmat_from_pbo(hit.pbo_path, hit.file_path, out)) return out;
            }
        }
    }

    if (out.error.empty()) out.error = "unable to resolve material";
    return out;
}

armatools::p3d::P3DFile load_model_for_materials(
    const std::string& model_path,
    const std::shared_ptr<armatools::pboindex::Index>& index,
    const std::shared_ptr<armatools::pboindex::DB>& db,
    const std::string& drive_root,
    std::string& loaded_from) {

    auto try_parse_data = [&](const std::vector<uint8_t>& data, const std::string& src)
        -> std::optional<armatools::p3d::P3DFile> {
        if (data.empty()) return std::nullopt;
        std::string buf(reinterpret_cast<const char*>(data.data()), data.size());
        std::istringstream iss(buf, std::ios::binary);
        loaded_from = src;
        return armatools::p3d::read(iss);
    };

    {
        std::ifstream f(model_path, std::ios::binary);
        if (f.good()) {
            loaded_from = model_path;
            return armatools::p3d::read(f);
        }
    }

    auto norm = normalize_path(model_path);

    if (index) {
        armatools::pboindex::ResolveResult rr;
        if (index->resolve(norm, rr)) {
            if (auto p3d = try_parse_data(extract_from_pbo(rr.pbo_path, rr.entry_name),
                                          rr.pbo_path + ":" + rr.entry_name)) {
                return std::move(*p3d);
            }
        }
        if (index->resolve(model_path, rr)) {
            if (auto p3d = try_parse_data(extract_from_pbo(rr.pbo_path, rr.entry_name),
                                          rr.pbo_path + ":" + rr.entry_name)) {
                return std::move(*p3d);
            }
        }
    }

    if (db) {
        auto filename = fs::path(norm).filename().string();
        auto hits = db->find_files("*" + filename);
        for (const auto& hit : hits) {
            auto full = normalize_path(hit.prefix + "/" + hit.file_path);
            if (full == norm || full.ends_with("/" + norm)) {
                if (auto p3d = try_parse_data(extract_from_pbo(hit.pbo_path, hit.file_path),
                                              hit.pbo_path + ":" + hit.file_path)) {
                    return std::move(*p3d);
                }
            }
        }
    }

    if (!drive_root.empty()) {
        auto ci = armatools::armapath::find_file_ci(fs::path(drive_root), model_path);
        if (ci) {
            std::ifstream f(ci->string(), std::ios::binary);
            if (f.good()) {
                loaded_from = ci->string();
                return armatools::p3d::read(f);
            }
        }
    }

    throw std::runtime_error("cannot resolve model path");
}

} // namespace

int main(int argc, char* argv[]) {
    bool pretty = false;
    bool json_stdout = false;
    int verbosity = 0;
    std::vector<std::string> positional;

    std::string rvmat_input;
    std::string materials_model;
    std::string db_path;
    std::string drive_root;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "--json") == 0) json_stdout = true;
        else if ((std::strcmp(argv[i], "--rvmat") == 0 || std::strcmp(argv[i], "--materials") == 0 ||
                  std::strcmp(argv[i], "--db") == 0 || std::strcmp(argv[i], "--drive-root") == 0) &&
                 i + 1 >= argc) {
            LOGE("missing value for", argv[i]);
            return 1;
        } else if (std::strcmp(argv[i], "--rvmat") == 0) {
            rvmat_input = argv[++i];
        } else if (std::strcmp(argv[i], "--materials") == 0) {
            materials_model = argv[++i];
        } else if (std::strcmp(argv[i], "--db") == 0) {
            db_path = argv[++i];
        } else if (std::strcmp(argv[i], "--drive-root") == 0) {
            drive_root = argv[++i];
        } else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0)
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

    if (!rvmat_input.empty() && !materials_model.empty()) {
        LOGE("--rvmat and --materials cannot be used together");
        return 1;
    }

    std::shared_ptr<armatools::pboindex::DB> db;
    std::shared_ptr<armatools::pboindex::Index> index;
    if (!db_path.empty()) {
        try {
            db = std::make_shared<armatools::pboindex::DB>(armatools::pboindex::DB::open(db_path));
            index = std::make_shared<armatools::pboindex::Index>(db->index());
        } catch (const std::exception& e) {
            LOGE("opening --db", db_path, e.what());
            return 1;
        }
    }

    if (!rvmat_input.empty()) {
        try {
            ResolvedRvmat mat;
            mat.reference = rvmat_input;
            auto path = fs::path(rvmat_input);
            if (fs::exists(path)) {
                if (!try_parse_rvmat_file(path, mat)) {
                    LOGE("parsing", rvmat_input, mat.error);
                    return 1;
                }
            } else {
                mat = resolve_rvmat(rvmat_input, "", index, db, drive_root);
                if (!mat.loaded) {
                    LOGE("resolving", rvmat_input, mat.error);
                    return 1;
                }
            }

            json doc = {
                {"schemaVersion", 1},
                {"mode", "rvmat"},
                {"input", rvmat_input},
                {"resolvedPath", mat.resolved_path},
                {"source", mat.source},
                {"material", material_to_json(mat.material)},
            };
            write_json(std::cout, doc, pretty);
            return 0;
        } catch (const std::exception& e) {
            LOGE("rvmat parse failed:", e.what());
            return 1;
        }
    }

    if (!materials_model.empty()) {
        try {
            std::string model_loaded_from;
            auto model = load_model_for_materials(materials_model, index, db, drive_root, model_loaded_from);

            std::set<std::string> refs;
            for (const auto& lod : model.lods) {
                for (const auto& m : lod.materials) {
                    if (!m.empty()) refs.insert(m);
                }
                for (const auto& face : lod.face_data) {
                    if (!face.material.empty()) refs.insert(face.material);
                }
            }

            json materials = json::array();
            int loaded_count = 0;
            for (const auto& ref : refs) {
                auto parsed = resolve_rvmat(ref, materials_model, index, db, drive_root);
                json m = {
                    {"reference", parsed.reference},
                    {"loaded", parsed.loaded},
                    {"resolvedPath", parsed.resolved_path},
                    {"source", parsed.source},
                };
                if (parsed.loaded) {
                    m["material"] = material_to_json(parsed.material);
                    ++loaded_count;
                } else {
                    m["error"] = parsed.error;
                }
                materials.push_back(std::move(m));
            }

            json doc = {
                {"schemaVersion", 1},
                {"mode", "materials"},
                {"model", materials_model},
                {"modelLoadedFrom", model_loaded_from},
                {"materialRefCount", refs.size()},
                {"materialsLoaded", loaded_count},
                {"materials", std::move(materials)},
            };
            write_json(std::cout, doc, pretty);
            return 0;
        } catch (const std::exception& e) {
            LOGE("materials failed:", e.what());
            return 1;
        }
    }

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
        LOGI("Reading from stdin");
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
                LOGD("Size (bytes):", fs::file_size(positional[0]));
            } catch (const std::exception&) {
                // ignore missing file size
            }
        }
    }

    armatools::p3d::P3DFile model;
    try {
        model = armatools::p3d::read(*input);
    } catch (const std::exception& e) {
        LOGE("parsing", filename, e.what());
        return 1;
    }

    auto doc = build_json(model, filename);

    try {
        if (json_stdout || from_stdin) {
            write_json(std::cout, doc, pretty);
        } else {
            std::string base = fs::path(positional[0]).stem().string();
            fs::path output_dir = fs::path(positional[0]).parent_path() / (base + "_p3d_info");
            LOGI("Writing to", output_dir.string());
            fs::create_directories(output_dir);
            std::ofstream jf(output_dir / "p3d.json");
            if (!jf) throw std::runtime_error("failed to create p3d.json");
            write_json(jf, doc, pretty);
            LOGI("Output:", output_dir.string());
        }
    } catch (const std::exception& e) {
        LOGE("writing output:", e.what());
        return 1;
    }

    if (armatools::cli::verbose_enabled()) {
        LOGI("LOD count:", model.lods.size(), "Textures:", doc["textures"].size());
    }
    if (armatools::cli::debug_enabled()) {
        size_t limit = std::min<size_t>(model.lods.size(), 3);
        for (size_t i = 0; i < limit; ++i) {
            LOGD("LOD", model.lods[i].index, "resolution", model.lods[i].resolution,
                 "verts", model.lods[i].vertex_count, "faces", model.lods[i].face_count);
        }
        LOGD("Total textures tracked", doc["textures"].size());
    }

    std::string version = version_string(model.format, model.version);
    LOGI("P3D:", filename, "(", version, ")");

    std::string lod_names;
    for (size_t i = 0; i < model.lods.size(); i++) {
        if (i > 0) lod_names += ", ";
        lod_names += model.lods[i].resolution_name;
    }
    LOGI("LODs:", model.lods.size(), "(", lod_names, ")");
    LOGI("Textures:", doc["textures"].size(), "unique");
    if (!model.lods.empty()) {
        LOGI("Vertices:", model.lods[0].vertex_count, "(LOD 0)");
    }
    if (doc.contains("size")) {
        auto& d = doc["size"]["dimensions"];
        LOGI("Size:", d[0].get<float>(), "x", d[1].get<float>(), "x", d[2].get<float>(),
                                  "m (from", doc["size"]["source"].get<std::string>() + ")");
    }

    return 0;
}
