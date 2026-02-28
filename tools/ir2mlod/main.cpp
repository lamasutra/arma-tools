#include "armatools/binutil.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../common/cli_logger.h"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace {

constexpr const char* kDefaultRvmat = "\\a3\\data_f\\default.rvmat";

struct Selection {
    std::string name;
    std::vector<uint32_t> vertices;
};

struct LODMesh {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::vector<uint32_t>> faces;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uv0;
};

struct IRLod {
    std::string id;
    std::string lod_id; // VISUAL_RESOLUTION, SHADOW_VOLUME
    float resolution = 0.0f;
    LODMesh mesh;
    std::vector<std::string> materials;
    std::vector<int> face_material_ids;
    std::vector<Selection> selections;
    std::vector<std::string> warnings;
};

struct IRModel {
    int schema_version = 0;
    std::string model_name;
    std::vector<IRLod> lods;
};

struct ExportReportLod {
    std::string id;
    std::string lod_id;
    float resolution = 0.0f;
    int vertex_count = 0;
    int face_count = 0;
    std::vector<std::string> missing_channels;
    std::vector<std::string> warnings;
};

struct ExportReport {
    std::vector<ExportReportLod> lods;
    std::vector<std::string> warnings;
    std::vector<std::string> manual_steps;
};

enum class Mode {
    Strict,
    VisualUpgrade,
};

enum class RecomputeNormals {
    Never,
    IfMissing,
    Always,
};

struct Config {
    Mode mode = Mode::VisualUpgrade;
    RecomputeNormals recompute_normals = RecomputeNormals::IfMissing;
    bool deterministic = false;
    bool autofix_selections = false;
    std::optional<fs::path> output_path;
    std::optional<fs::path> report_path;
    std::set<std::string> lod_only_tokens;
};

struct ErrorCollector {
    std::vector<std::string> errors;

    void add(std::string message) { errors.push_back(std::move(message)); }
    [[nodiscard]] bool ok() const { return errors.empty(); }
};

void print_usage() {
    armatools::cli::print("Usage: ir2mlod <ir_dir> -o out.p3d [flags]");
    armatools::cli::print("Converts visual-upgrade IR to MLOD preview output.");
    armatools::cli::print("");
    armatools::cli::print("Flags:");
    armatools::cli::print("  -o, --output <path>                 Output .p3d path");
    armatools::cli::print("  --mode <strict|visual-upgrade>      Export mode (default: visual-upgrade)");
    armatools::cli::print("  --recompute-normals <never|if_missing|always> (default: if_missing)");
    armatools::cli::print("  --deterministic                     Deterministic ordering for selections");
    armatools::cli::print("  --report <path>                     Write JSON report");
    armatools::cli::print("  --lod-only <csv>                    Export only listed LODs (e.g. 0.000,1.000,shadow)");
    armatools::cli::print("  --autofix-selections                Drop out-of-range selection indices in visual mode");
    armatools::cli::print("  -v, --verbose                       Verbose logging");
    armatools::cli::print("  -vv, --debug                        Debug logging");
    armatools::cli::print("  -h, --help                          Show help");
}

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void write_i32(std::ostream& w, int32_t v) {
    armatools::binutil::write_u32(w, static_cast<uint32_t>(v));
}

void write_signature(std::ostream& w, std::string_view sig) {
    if (sig.size() != 4 || !w.write(sig.data(), 4)) {
        throw std::runtime_error("failed to write signature");
    }
}

bool finite_vec3(const std::array<float, 3>& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

std::optional<std::array<float, 3>> parse_vec3(const json& j) {
    if (!j.is_array() || j.size() != 3) return std::nullopt;
    if (!j[0].is_number() || !j[1].is_number() || !j[2].is_number()) return std::nullopt;
    return std::array<float, 3>{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

std::optional<std::array<float, 2>> parse_vec2(const json& j) {
    if (!j.is_array() || j.size() != 2) return std::nullopt;
    if (!j[0].is_number() || !j[1].is_number()) return std::nullopt;
    return std::array<float, 2>{j[0].get<float>(), j[1].get<float>()};
}

std::vector<std::string> split_csv_tokens(const std::string& csv) {
    std::vector<std::string> out;
    std::string current;

    for (char c : csv) {
        if (c == ',') {
            if (!current.empty()) out.push_back(to_lower_ascii(current));
            current.clear();
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) current.push_back(c);
    }

    if (!current.empty()) out.push_back(to_lower_ascii(current));
    return out;
}

std::optional<std::vector<std::vector<uint32_t>>> parse_indices(const json& j) {
    if (!j.is_array()) return std::nullopt;

    std::vector<std::vector<uint32_t>> faces;

    if (!j.empty() && j[0].is_array()) {
        faces.reserve(j.size());
        for (const auto& face_j : j) {
            if (!face_j.is_array() || face_j.size() < 3 || face_j.size() > 4) return std::nullopt;

            std::vector<uint32_t> face;
            face.reserve(face_j.size());
            for (const auto& idx_j : face_j) {
                if (!idx_j.is_number_integer() || idx_j.get<int64_t>() < 0) return std::nullopt;
                face.push_back(static_cast<uint32_t>(idx_j.get<uint64_t>()));
            }
            faces.push_back(std::move(face));
        }
        return faces;
    }

    // Flat triangle-list form.
    if (j.size() % 3 != 0) return std::nullopt;
    faces.reserve(j.size() / 3);
    for (size_t i = 0; i < j.size(); i += 3) {
        std::vector<uint32_t> tri;
        tri.reserve(3);
        for (size_t k = 0; k < 3; ++k) {
            const auto& idx_j = j[i + k];
            if (!idx_j.is_number_integer() || idx_j.get<int64_t>() < 0) return std::nullopt;
            tri.push_back(static_cast<uint32_t>(idx_j.get<uint64_t>()));
        }
        faces.push_back(std::move(tri));
    }
    return faces;
}

std::string infer_lod_id(const json& lod_j, float resolution) {
    if (lod_j.contains("lodId") && lod_j["lodId"].is_string()) {
        auto lod_id = to_lower_ascii(lod_j["lodId"].get<std::string>());
        if (lod_id == "shadow_volume") return "SHADOW_VOLUME";
    }
    if (resolution >= 10000.0f && resolution < 20000.0f) return "SHADOW_VOLUME";
    return "VISUAL_RESOLUTION";
}

bool should_export_lod(float resolution, const std::string& lod_id, const Config& cfg) {
    if (cfg.lod_only_tokens.empty()) return true;

    if (lod_id == "SHADOW_VOLUME") {
        return cfg.lod_only_tokens.contains("shadow") ||
               cfg.lod_only_tokens.contains("shadow_volume");
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << resolution;
    if (cfg.lod_only_tokens.contains(to_lower_ascii(ss.str()))) return true;

    const auto int_res = static_cast<int>(std::lround(resolution));
    if (std::fabs(resolution - static_cast<float>(int_res)) < 1e-4f) {
        return cfg.lod_only_tokens.contains(std::to_string(int_res));
    }

    return false;
}

std::array<float, 3> sub3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

std::array<float, 3> cross3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

void normalize3(std::array<float, 3>& v) {
    const auto len_sq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len_sq <= 1e-20f) {
        v = {0.0f, 0.0f, 1.0f};
        return;
    }
    const auto inv_len = 1.0f / std::sqrt(len_sq);
    v[0] *= inv_len;
    v[1] *= inv_len;
    v[2] *= inv_len;
}

std::vector<std::array<float, 3>> recompute_vertex_normals(
    const std::vector<std::array<float, 3>>& positions,
    const std::vector<std::vector<uint32_t>>& faces) {

    std::vector<std::array<float, 3>> out(positions.size(), {0.0f, 0.0f, 0.0f});

    for (const auto& face : faces) {
        if (face.size() < 3) continue;
        auto n = cross3(sub3(positions[face[1]], positions[face[0]]),
                        sub3(positions[face[2]], positions[face[0]]));

        const auto len_sq = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
        if (len_sq <= 1e-20f && face.size() == 4) {
            n = cross3(sub3(positions[face[2]], positions[face[0]]),
                       sub3(positions[face[3]], positions[face[0]]));
        }

        for (uint32_t idx : face) {
            out[idx][0] += n[0];
            out[idx][1] += n[1];
            out[idx][2] += n[2];
        }
    }

    for (auto& n : out) normalize3(n);
    return out;
}

bool parse_selections(const json& lod_j,
                      std::vector<Selection>& selections,
                      ErrorCollector& ec,
                      const std::string& lod_name) {
    if (!lod_j.contains("named_selections")) return true;
    const auto& ns = lod_j["named_selections"];

    if (ns.is_object()) {
        for (auto it = ns.begin(); it != ns.end(); ++it) {
            if (!it.value().is_array()) {
                ec.add(lod_name + ": selection " + it.key() + " must be array");
                return false;
            }
            Selection sel;
            sel.name = it.key();
            for (const auto& idx_j : it.value()) {
                if (!idx_j.is_number_integer() || idx_j.get<int64_t>() < 0) {
                    ec.add(lod_name + ": selection " + it.key() + " has invalid index");
                    return false;
                }
                sel.vertices.push_back(static_cast<uint32_t>(idx_j.get<uint64_t>()));
            }
            selections.push_back(std::move(sel));
        }
        return true;
    }

    if (ns.is_array()) {
        for (const auto& entry : ns) {
            if (!entry.is_object() ||
                !entry.contains("name") || !entry["name"].is_string() ||
                !entry.contains("vertices") || !entry["vertices"].is_array()) {
                ec.add(lod_name + ": invalid selection entry shape");
                return false;
            }
            Selection sel;
            sel.name = entry["name"].get<std::string>();
            for (const auto& idx_j : entry["vertices"]) {
                if (!idx_j.is_number_integer() || idx_j.get<int64_t>() < 0) {
                    ec.add(lod_name + ": selection " + sel.name + " has invalid index");
                    return false;
                }
                sel.vertices.push_back(static_cast<uint32_t>(idx_j.get<uint64_t>()));
            }
            selections.push_back(std::move(sel));
        }
        return true;
    }

    ec.add(lod_name + ": named_selections must be object or array");
    return false;
}

void validate_and_fix_lod(IRLod& lod,
                          const Config& cfg,
                          ExportReport& report,
                          ExportReportLod& rep,
                          ErrorCollector& ec) {
    if (cfg.recompute_normals == RecomputeNormals::Always ||
        (cfg.recompute_normals == RecomputeNormals::IfMissing && lod.mesh.normals.empty())) {
        lod.mesh.normals = recompute_vertex_normals(lod.mesh.positions, lod.mesh.faces);
        lod.warnings.push_back("Normals recomputed");
    }

    if (!lod.mesh.normals.empty() && lod.mesh.normals.size() != lod.mesh.positions.size()) {
        if (cfg.mode == Mode::Strict) {
            ec.add(lod.id + ": normals count must match positions in strict mode");
            return;
        }
        lod.warnings.push_back("Normals count mismatch; recomputed");
        lod.mesh.normals = recompute_vertex_normals(lod.mesh.positions, lod.mesh.faces);
    }

    if (lod.mesh.normals.empty()) {
        lod.mesh.normals.resize(lod.mesh.positions.size(), {0.0f, 0.0f, 1.0f});
        rep.missing_channels.push_back("normals");
        lod.warnings.push_back("Normals missing; default normals assigned");
    }

    if (lod.mesh.uv0.empty()) {
        if (cfg.mode == Mode::Strict) {
            ec.add(lod.id + ": uv0 is required in strict mode");
            return;
        }
        lod.mesh.uv0.resize(lod.mesh.positions.size(), {0.0f, 0.0f});
        rep.missing_channels.push_back("uv0");
        lod.warnings.push_back("UV missing in LOD " + lod.id);
        report.manual_steps.push_back("UVs missing: unwrap in Object Builder");
    } else if (lod.mesh.uv0.size() != lod.mesh.positions.size()) {
        if (cfg.mode == Mode::Strict) {
            ec.add(lod.id + ": uv0 count must match positions in strict mode");
            return;
        }
        std::vector<std::array<float, 2>> fixed(lod.mesh.positions.size(), {0.0f, 0.0f});
        const auto copy_n = std::min(fixed.size(), lod.mesh.uv0.size());
        for (size_t i = 0; i < copy_n; ++i) fixed[i] = lod.mesh.uv0[i];
        lod.mesh.uv0 = std::move(fixed);
        lod.warnings.push_back("uv0 count mismatch; padded/clamped");
    }

    if (lod.materials.empty()) {
        if (cfg.mode == Mode::Strict) {
            ec.add(lod.id + ": materials are required in strict mode");
            return;
        }
        lod.materials = {kDefaultRvmat};
        rep.missing_channels.push_back("materials");
        lod.warnings.push_back("Materials missing; placeholder assigned");
        report.manual_steps.push_back("materials placeholder: assign rvmat/texture");
    }

    if (lod.face_material_ids.empty()) {
        if (lod.materials.size() > 1) {
            if (cfg.mode == Mode::Strict) {
                ec.add(lod.id + ": multiple materials but no face_material_ids in strict mode");
                return;
            }
            lod.warnings.push_back("Multiple materials but no per-face mapping; slot 0 used");
        }
        lod.face_material_ids.assign(lod.mesh.faces.size(), 0);
        rep.missing_channels.push_back("face_material_ids");
    } else {
        if (lod.face_material_ids.size() != lod.mesh.faces.size()) {
            if (cfg.mode == Mode::Strict) {
                ec.add(lod.id + ": face_material_ids count must match face count in strict mode");
                return;
            }
            std::vector<int> fixed(lod.mesh.faces.size(), 0);
            const auto copy_n = std::min(fixed.size(), lod.face_material_ids.size());
            for (size_t i = 0; i < copy_n; ++i) fixed[i] = lod.face_material_ids[i];
            lod.face_material_ids = std::move(fixed);
            lod.warnings.push_back("face_material_ids count mismatch; padded/clamped");
        }

        for (size_t i = 0; i < lod.face_material_ids.size(); ++i) {
            const auto mat_id = lod.face_material_ids[i];
            if (mat_id < 0 || static_cast<size_t>(mat_id) >= lod.materials.size()) {
                if (cfg.mode == Mode::Strict) {
                    ec.add(lod.id + ": face material id out of range at face " + std::to_string(i));
                    return;
                }
                lod.face_material_ids[i] = 0;
                lod.warnings.push_back("face material id out of range; slot 0 used");
            }
        }
    }

    if (!rep.missing_channels.empty() &&
        std::find(rep.missing_channels.begin(), rep.missing_channels.end(), "uv0") != rep.missing_channels.end() &&
        !lod.materials.empty()) {
        lod.warnings.push_back("Materials exist while UV missing; manual unwrap required");
    }

    for (auto& sel : lod.selections) {
        std::vector<uint32_t> filtered;
        filtered.reserve(sel.vertices.size());
        bool had_oob = false;

        for (uint32_t idx : sel.vertices) {
            if (idx >= lod.mesh.positions.size()) {
                had_oob = true;
                continue;
            }
            filtered.push_back(idx);
        }

        if (had_oob) {
            if (cfg.mode == Mode::Strict) {
                ec.add(lod.id + ": selection " + sel.name + " has out-of-range indices");
                return;
            }
            if (cfg.autofix_selections) {
                lod.warnings.push_back("Selection " + sel.name + " had out-of-range indices and was autofixed");
            } else {
                lod.warnings.push_back("Selection " + sel.name + " has out-of-range indices");
            }
            sel.vertices = std::move(filtered);
        }

        std::sort(sel.vertices.begin(), sel.vertices.end());
        sel.vertices.erase(std::unique(sel.vertices.begin(), sel.vertices.end()), sel.vertices.end());
    }
}

std::optional<IRModel> load_ir_model(const fs::path& input_path,
                                     const Config& cfg,
                                     ExportReport& report,
                                     ErrorCollector& ec) {
    fs::path ir_json_path = input_path;
    if (fs::is_directory(input_path)) {
        ir_json_path = input_path / "ir.json";
    }

    std::ifstream in(ir_json_path);
    if (!in.is_open()) {
        ec.add("cannot open IR file: " + ir_json_path.string());
        return std::nullopt;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        ec.add(std::string("failed to parse IR JSON: ") + e.what());
        return std::nullopt;
    }

    if (!root.contains("lods") || !root["lods"].is_array()) {
        ec.add("IR must contain array field 'lods'");
        return std::nullopt;
    }

    IRModel model;
    model.schema_version = root.value("schemaVersion", 0);
    model.model_name = root.value("modelName", std::string{});

    for (size_t i = 0; i < root["lods"].size(); ++i) {
        const auto& lod_j = root["lods"][i];
        if (!lod_j.is_object()) {
            ec.add("lods[" + std::to_string(i) + "] must be an object");
            return std::nullopt;
        }

        IRLod lod;
        lod.id = lod_j.value("id", "lod_" + std::to_string(i));
        lod.resolution = lod_j.value("resolution", 0.0f);
        lod.lod_id = infer_lod_id(lod_j, lod.resolution);

        if (!should_export_lod(lod.resolution, lod.lod_id, cfg)) {
            continue;
        }

        const auto& mesh_j = lod_j.contains("mesh") ? lod_j["mesh"] : lod_j;
        if (!mesh_j.is_object()) {
            ec.add(lod.id + ": mesh must be an object");
            return std::nullopt;
        }

        if (!mesh_j.contains("positions") || !mesh_j["positions"].is_array()) {
            ec.add(lod.id + ": missing or invalid mesh.positions");
            return std::nullopt;
        }
        if (!mesh_j.contains("indices")) {
            ec.add(lod.id + ": missing mesh.indices");
            return std::nullopt;
        }

        lod.mesh.positions.reserve(mesh_j["positions"].size());
        for (const auto& p_j : mesh_j["positions"]) {
            auto p = parse_vec3(p_j);
            if (!p || !finite_vec3(*p)) {
                ec.add(lod.id + ": invalid position encountered");
                return std::nullopt;
            }
            lod.mesh.positions.push_back(*p);
        }

        auto faces = parse_indices(mesh_j["indices"]);
        if (!faces) {
            ec.add(lod.id + ": invalid mesh.indices format");
            return std::nullopt;
        }
        lod.mesh.faces = std::move(*faces);

        for (size_t fi = 0; fi < lod.mesh.faces.size(); ++fi) {
            for (uint32_t idx : lod.mesh.faces[fi]) {
                if (idx >= lod.mesh.positions.size()) {
                    ec.add(lod.id + ": face index out of range at face " + std::to_string(fi));
                    return std::nullopt;
                }
            }
        }

        if (mesh_j.contains("normals")) {
            if (!mesh_j["normals"].is_array()) {
                ec.add(lod.id + ": mesh.normals must be an array");
                return std::nullopt;
            }
            lod.mesh.normals.reserve(mesh_j["normals"].size());
            for (const auto& n_j : mesh_j["normals"]) {
                auto n = parse_vec3(n_j);
                if (!n) {
                    ec.add(lod.id + ": invalid normal encountered");
                    return std::nullopt;
                }
                lod.mesh.normals.push_back(*n);
            }
        }

        if (mesh_j.contains("uv0")) {
            if (!mesh_j["uv0"].is_array()) {
                ec.add(lod.id + ": mesh.uv0 must be an array");
                return std::nullopt;
            }
            lod.mesh.uv0.reserve(mesh_j["uv0"].size());
            for (const auto& uv_j : mesh_j["uv0"]) {
                auto uv = parse_vec2(uv_j);
                if (!uv) {
                    ec.add(lod.id + ": invalid uv0 encountered");
                    return std::nullopt;
                }
                lod.mesh.uv0.push_back(*uv);
            }
        }

        if (lod_j.contains("materials")) {
            if (!lod_j["materials"].is_array()) {
                ec.add(lod.id + ": materials must be an array");
                return std::nullopt;
            }
            for (const auto& mat_j : lod_j["materials"]) {
                if (!mat_j.is_string()) {
                    ec.add(lod.id + ": material entries must be strings");
                    return std::nullopt;
                }
                lod.materials.push_back(mat_j.get<std::string>());
            }
        }

        if (lod_j.contains("face_material_ids")) {
            if (!lod_j["face_material_ids"].is_array()) {
                ec.add(lod.id + ": face_material_ids must be an array");
                return std::nullopt;
            }
            for (const auto& id_j : lod_j["face_material_ids"]) {
                if (!id_j.is_number_integer()) {
                    ec.add(lod.id + ": face_material_ids entries must be integers");
                    return std::nullopt;
                }
                lod.face_material_ids.push_back(id_j.get<int>());
            }
        }

        if (!parse_selections(lod_j, lod.selections, ec, lod.id)) {
            return std::nullopt;
        }

        ExportReportLod rep;
        rep.id = lod.id;
        rep.lod_id = lod.lod_id;
        rep.resolution = lod.resolution;
        rep.vertex_count = static_cast<int>(lod.mesh.positions.size());
        rep.face_count = static_cast<int>(lod.mesh.faces.size());

        validate_and_fix_lod(lod, cfg, report, rep, ec);
        if (!ec.ok()) return std::nullopt;

        rep.warnings = lod.warnings;
        report.lods.push_back(std::move(rep));
        model.lods.push_back(std::move(lod));
    }

    if (model.lods.empty()) {
        ec.add("no LODs selected for export");
        return std::nullopt;
    }

    std::stable_sort(model.lods.begin(), model.lods.end(), [](const IRLod& a, const IRLod& b) {
        const bool a_shadow = a.lod_id == "SHADOW_VOLUME";
        const bool b_shadow = b.lod_id == "SHADOW_VOLUME";
        if (a_shadow != b_shadow) return !a_shadow && b_shadow;
        if (a.resolution != b.resolution) return a.resolution < b.resolution;
        return a.id < b.id;
    });

    return model;
}

void write_named_selection_tagg(std::ostream& out,
                                const Selection& selection,
                                size_t point_count) {
    armatools::binutil::write_u8(out, 1); // active
    armatools::binutil::write_asciiz(out, selection.name);

    const auto payload_size = static_cast<uint32_t>(point_count);
    armatools::binutil::write_u32(out, payload_size);

    std::vector<uint8_t> payload(point_count, 0);
    for (uint32_t idx : selection.vertices) {
        if (idx < payload.size()) payload[idx] = 1;
    }

    if (!payload.empty() &&
        !out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()))) {
        throw std::runtime_error("failed to write selection payload");
    }
}

void write_lod(std::ostream& out, const IRLod& lod, const Config& cfg) {
    write_signature(out, "P3DM");
    write_i32(out, 28);
    write_i32(out, 256);

    const auto point_count = static_cast<uint32_t>(lod.mesh.positions.size());
    const auto normal_count = static_cast<uint32_t>(lod.mesh.normals.size());
    const auto face_count = static_cast<uint32_t>(lod.mesh.faces.size());

    armatools::binutil::write_u32(out, point_count);
    armatools::binutil::write_u32(out, normal_count);
    armatools::binutil::write_u32(out, face_count);
    armatools::binutil::write_u32(out, 0); // flags

    for (const auto& p : lod.mesh.positions) {
        armatools::binutil::write_f32(out, p[0]);
        armatools::binutil::write_f32(out, p[1]);
        armatools::binutil::write_f32(out, p[2]);
        armatools::binutil::write_u32(out, 0); // point flags
    }

    for (const auto& n : lod.mesh.normals) {
        armatools::binutil::write_f32(out, n[0]);
        armatools::binutil::write_f32(out, n[1]);
        armatools::binutil::write_f32(out, n[2]);
    }

    for (size_t fi = 0; fi < lod.mesh.faces.size(); ++fi) {
        const auto& face = lod.mesh.faces[fi];
        const auto n = static_cast<int32_t>(face.size());

        write_i32(out, n);

        // Reader reverses vertex order, so write reversed to preserve topology.
        for (int slot = 0; slot < 4; ++slot) {
            if (slot < n) {
                const auto idx = face[face.size() - 1 - static_cast<size_t>(slot)];
                write_i32(out, static_cast<int32_t>(idx));
                write_i32(out, static_cast<int32_t>(idx));

                const auto uv = idx < lod.mesh.uv0.size() ? lod.mesh.uv0[idx] : std::array<float, 2>{0.0f, 0.0f};
                armatools::binutil::write_f32(out, uv[0]);
                armatools::binutil::write_f32(out, uv[1]);
            } else {
                write_i32(out, 0);
                write_i32(out, 0);
                armatools::binutil::write_f32(out, 0.0f);
                armatools::binutil::write_f32(out, 0.0f);
            }
        }

        write_i32(out, 0); // face flags
        armatools::binutil::write_asciiz(out, ""); // texture placeholder

        const auto mat_id = (fi < lod.face_material_ids.size() && lod.face_material_ids[fi] >= 0)
            ? static_cast<size_t>(lod.face_material_ids[fi])
            : 0U;
        const auto material = mat_id < lod.materials.size() ? lod.materials[mat_id] : std::string(kDefaultRvmat);
        armatools::binutil::write_asciiz(out, material);
    }

    write_signature(out, "TAGG");

    auto selections = lod.selections;
    if (cfg.deterministic) {
        std::sort(selections.begin(), selections.end(), [](const Selection& a, const Selection& b) {
            return a.name < b.name;
        });
    }

    for (const auto& selection : selections) {
        write_named_selection_tagg(out, selection, lod.mesh.positions.size());
    }

    armatools::binutil::write_u8(out, 1);
    armatools::binutil::write_asciiz(out, "#EndOfFile#");
    armatools::binutil::write_u32(out, 0);
    armatools::binutil::write_f32(out, lod.resolution);
}

void write_mlod(const IRModel& model, const fs::path& output_path, const Config& cfg) {
    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("cannot write output: " + output_path.string());
    }

    write_signature(out, "MLOD");
    armatools::binutil::write_u32(out, 257);
    armatools::binutil::write_u32(out, static_cast<uint32_t>(model.lods.size()));

    for (const auto& lod : model.lods) {
        write_lod(out, lod, cfg);
    }
}

json build_report_json(const ExportReport& report,
                       const Config& cfg,
                       const fs::path& input_path,
                       const fs::path& output_path) {
    json lods_j = json::array();
    for (const auto& lod : report.lods) {
        lods_j.push_back({
            {"id", lod.id},
            {"lodId", lod.lod_id},
            {"resolution", lod.resolution},
            {"vertexCount", lod.vertex_count},
            {"faceCount", lod.face_count},
            {"missingChannels", lod.missing_channels},
            {"warnings", lod.warnings},
        });
    }

    std::set<std::string> unique_manual(report.manual_steps.begin(), report.manual_steps.end());

    return {
        {"schemaVersion", 1},
        {"tool", "ir2mlod"},
        {"mode", cfg.mode == Mode::Strict ? "strict" : "visual-upgrade"},
        {"input", input_path.string()},
        {"output", output_path.string()},
        {"lods", lods_j},
        {"warnings", report.warnings},
        {"manualStepsSuggested", std::vector<std::string>(unique_manual.begin(), unique_manual.end())},
    };
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        print_usage();
        return 1;
    }

    Config cfg;
    int verbosity = 0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            cfg.output_path = fs::path(argv[++i]);
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            auto mode = to_lower_ascii(argv[++i]);
            if (mode == "strict") cfg.mode = Mode::Strict;
            else if (mode == "visual-upgrade") cfg.mode = Mode::VisualUpgrade;
            else {
                LOGE("invalid --mode", mode);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--recompute-normals") == 0 && i + 1 < argc) {
            auto mode = to_lower_ascii(argv[++i]);
            if (mode == "never") cfg.recompute_normals = RecomputeNormals::Never;
            else if (mode == "if_missing") cfg.recompute_normals = RecomputeNormals::IfMissing;
            else if (mode == "always") cfg.recompute_normals = RecomputeNormals::Always;
            else {
                LOGE("invalid --recompute-normals", mode);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--deterministic") == 0) {
            cfg.deterministic = true;
        } else if (std::strcmp(argv[i], "--autofix-selections") == 0) {
            cfg.autofix_selections = true;
        } else if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            cfg.report_path = fs::path(argv[++i]);
        } else if (std::strcmp(argv[i], "--lod-only") == 0 && i + 1 < argc) {
            for (const auto& token : split_csv_tokens(argv[++i])) {
                cfg.lod_only_tokens.insert(token);
            }
        } else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbosity = std::min(verbosity + 1, 2);
        } else if (std::strcmp(argv[i], "-vv") == 0 || std::strcmp(argv[i], "--debug") == 0) {
            verbosity = 2;
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    armatools::cli::set_verbosity(verbosity);

    if (positional.size() != 1) {
        LOGE("expected one input IR path");
        print_usage();
        return 1;
    }
    if (!cfg.output_path.has_value()) {
        LOGE("missing required -o/--output");
        return 1;
    }

    const auto input_path = fs::path(positional.front());
    const auto output_path = *cfg.output_path;

    ExportReport report;
    ErrorCollector ec;

    auto model = load_ir_model(input_path, cfg, report, ec);
    if (!model || !ec.ok()) {
        for (const auto& err : ec.errors) {
            LOGE(err);
        }
        return 1;
    }

    try {
        if (!output_path.parent_path().empty()) {
            std::error_code out_ec;
            fs::create_directories(output_path.parent_path(), out_ec);
            if (out_ec) {
                LOGE("cannot create output directory", out_ec.message());
                return 1;
            }
        }

        write_mlod(*model, output_path, cfg);
        armatools::cli::log_stdout("wrote", output_path.string());

        for (const auto& lod : report.lods) {
            for (const auto& warn : lod.warnings) {
                report.warnings.push_back(lod.id + ": " + warn);
            }
        }

        for (const auto& warn : report.warnings) {
            LOGW(warn);
        }

        if (cfg.report_path.has_value()) {
            if (!cfg.report_path->parent_path().empty()) {
                std::error_code rep_ec;
                fs::create_directories(cfg.report_path->parent_path(), rep_ec);
                if (rep_ec) {
                    LOGE("cannot create report directory", rep_ec.message());
                    return 1;
                }
            }

            auto rep_json = build_report_json(report, cfg, input_path, output_path);
            std::ofstream rep_out(*cfg.report_path);
            if (!rep_out.is_open()) {
                LOGE("cannot write report", cfg.report_path->string());
                return 1;
            }
            rep_out << std::setw(2) << rep_json << '\n';
            armatools::cli::log_stdout("report", cfg.report_path->string());
        }
    } catch (const std::exception& e) {
        LOGE("export failed:", e.what());
        return 1;
    }

    return 0;
}
