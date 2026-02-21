// wrp_debug: low-level OPRW section dumper for diagnosing WRP parsing issues.
//
// Manually walks the OPRW binary structure and prints raw field values
// for key sections (models, objects, mapinfo) without relying on the
// main wrp library's interpretation.

#include <armatools/binutil.h>
#include <armatools/lzss.h>
#include <armatools/lzo.h>

#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace armatools::binutil;

static std::vector<uint8_t> read_compressed(std::istream& r, size_t expected, uint32_t version) {
    if (version >= 23) return armatools::lzo::decompress_or_raw(r, expected);
    return armatools::lzss::decompress_or_raw(r, expected);
}

static void skip_quad_tree_node(std::istream& r) {
    uint16_t flag_mask = read_u16(r);
    for (int i = 0; i < 16; i++) {
        if (flag_mask & (1 << i)) skip_quad_tree_node(r);
        else read_bytes(r, 4);
    }
}

static void skip_quad_tree(std::istream& r) {
    uint8_t flag = read_u8(r);
    if (flag == 0) { read_bytes(r, 4); return; }
    skip_quad_tree_node(r);
}

static void print_usage() {
    std::cerr << "Usage: wrp_debug <file.wrp> [--records N]\n\n"
              << "Low-level OPRW section dumper.\n"
              << "Prints raw field values for models, objects, and mapinfo sections.\n\n"
              << "Options:\n"
              << "  --records N    Number of object/mapinfo records to dump (default: 15)\n";
}

int main(int argc, char* argv[]) {
    int dump_count = 15;
    std::string input_path;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--records" && i + 1 < argc) {
            dump_count = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            print_usage();
            return 0;
        } else {
            input_path = argv[i];
        }
    }

    if (input_path.empty()) { print_usage(); return 1; }

    std::ifstream f(input_path, std::ios::binary);
    if (!f) { std::cerr << "Error: cannot open " << input_path << '\n'; return 1; }

    auto sig = read_signature(f);
    uint32_t ver = read_u32(f);
    std::cerr << std::format("Format: {} v{}\n", sig, ver);

    if (sig != "OPRW" || ver < 12) {
        std::cerr << "This tool only supports OPRW v12-25.\n";
        return 1;
    }

    // AppID (v>=25)
    if (ver >= 25) {
        int32_t app_id = read_i32(f);
        std::cerr << std::format("AppID: {}\n", app_id);
    }

    int lrx = read_i32(f), lry = read_i32(f);
    int trx = read_i32(f), try_ = read_i32(f);
    float cs = read_f32(f);
    std::cerr << std::format("Grid: {}x{} land, {}x{} terrain, cell={:.1f}m\n", lrx, lry, trx, try_, cs);

    int land_cells = lrx * lry;
    int terrain_cells = trx * try_;

    skip_quad_tree(f); // Geography
    skip_quad_tree(f); // SoundMap

    // Mountains
    {
        int32_t n = read_i32(f);
        std::cerr << std::format("Peaks: {}\n", n);
        if (n > 0) read_bytes(f, static_cast<size_t>(n) * 12);
    }

    skip_quad_tree(f); // Materials

    if (ver < 21) read_compressed(f, static_cast<size_t>(land_cells) * 2, ver);
    if (ver >= 18) read_compressed(f, static_cast<size_t>(terrain_cells), ver);
    if (ver >= 22) read_compressed(f, static_cast<size_t>(terrain_cells), ver);
    read_compressed(f, static_cast<size_t>(terrain_cells) * 4, ver); // Elevation

    // MatNames
    {
        int32_t n = read_i32(f);
        std::cerr << std::format("MatNames: {}\n", n);
        for (int i = 0; i < n; i++) { read_asciiz(f); read_u8(f); }
    }

    // Models
    int32_t n_models = read_i32(f);
    std::vector<std::string> models(static_cast<size_t>(n_models));
    for (int i = 0; i < n_models; i++) models[static_cast<size_t>(i)] = read_asciiz(f);
    std::cerr << std::format("Models: {}\n", n_models);
    for (int i = 0; i < n_models; i++) {
        std::cerr << std::format("  [{:3d}] {}\n", i, models[static_cast<size_t>(i)]);
    }

    // EntityInfos (v>=15)
    if (ver >= 15) {
        int32_t n = read_i32(f);
        std::cerr << std::format("EntityInfos: {}\n", n);
        for (int i = 0; i < n; i++) {
            auto cn = read_asciiz(f);
            auto sn = read_asciiz(f);
            read_bytes(f, 12);
            int32_t oid = read_i32(f);
            if (i < 5) {
                std::cerr << std::format("  [{}] class={} shape={} objID={}\n", i, cn, sn, oid);
            }
        }
    }

    skip_quad_tree(f); // ObjectOffsets
    int32_t sizeOfObjects = read_i32(f);
    skip_quad_tree(f); // MapObjectOffsets
    int32_t sizeOfMapInfo = read_i32(f);

    std::cerr << std::format("\nSizeOfObjects: {} bytes ({} records of 60)\n", sizeOfObjects, sizeOfObjects / 60);
    std::cerr << std::format("SizeOfMapInfo: {} bytes (mod60={})\n", sizeOfMapInfo, sizeOfMapInfo % 60);

    read_compressed(f, static_cast<size_t>(land_cells), ver); // Persistent
    read_compressed(f, static_cast<size_t>(terrain_cells), ver); // SubDivHints

    int32_t maxObjId = read_i32(f);
    int32_t roadNetSize = read_i32(f);
    std::cerr << std::format("MaxObjectID: {}\n", maxObjId);
    std::cerr << std::format("RoadNetSize: {}\n", roadNetSize);

    // Skip RoadNets
    for (int i = 0; i < land_cells; i++) {
        int32_t n = read_i32(f);
        for (int j = 0; j < n; j++) {
            uint16_t cc = read_u16(f);
            if (cc > 0) read_bytes(f, static_cast<size_t>(cc) * 12);
            if (ver >= 24 && cc > 0) read_bytes(f, static_cast<size_t>(cc));
            read_i32(f); // objectId
            if (ver >= 16) {
                read_asciiz(f); // p3d
                read_bytes(f, 48); // transform
            }
        }
    }

    // Section 23: Objects
    int n_objects = sizeOfObjects / 60;
    auto objects_offset = f.tellg();
    std::cerr << std::format("\n--- Section 23: Objects ({} records, file offset {}) ---\n",
                             n_objects, (long long)objects_offset);

    int show_obj = std::min(dump_count, n_objects);
    for (int i = 0; i < show_obj; i++) {
        int32_t obj_id = read_i32(f);
        int32_t model_idx = read_i32(f);
        auto transform = read_f32_slice(f, 12);
        int32_t shape_param = read_i32(f);

        std::string m0 = (model_idx >= 0 && model_idx < n_models)
            ? models[static_cast<size_t>(model_idx)] : "OOB";

        std::cerr << std::format("  [{}] objID={} rawIdx={} shape={:#010x}"
                                  " pos=[{:.1f},{:.1f},{:.1f}] → {}\n",
            i, obj_id, model_idx, static_cast<unsigned>(shape_param),
            transform[9], transform[10], transform[11], m0);
    }

    // Index distribution
    f.seekg(objects_offset);
    std::vector<int> idx_hist(static_cast<size_t>(n_models) + 2, 0);
    int oob_count = 0;
    for (int i = 0; i < n_objects; i++) {
        read_i32(f); // objID
        int32_t model_idx = read_i32(f);
        read_bytes(f, 52); // transform + shape
        if (model_idx >= 0 && model_idx < n_models) {
            idx_hist[static_cast<size_t>(model_idx)]++;
        } else {
            oob_count++;
        }
    }

    std::cerr << std::format("\nObject index range: OOB={}, idx[0]={}, idx[{}]={}, idx[{}]={}\n",
        oob_count, idx_hist[0],
        n_models - 1, idx_hist[static_cast<size_t>(n_models) - 1],
        n_models, idx_hist[static_cast<size_t>(n_models)]);

    // Section 24: MapInfo
    auto mapinfo_offset = f.tellg();
    std::cerr << std::format("\n--- Section 24: MapInfo ({} bytes, file offset {}) ---\n",
                             sizeOfMapInfo, (long long)mapinfo_offset);

    if (sizeOfMapInfo > 0) {
        int show_map = std::min(dump_count, static_cast<int>(sizeOfMapInfo / 60));
        std::cerr << std::format("First {} raw 60-byte interpretations:\n", show_map);
        for (int i = 0; i < show_map; i++) {
            int32_t val0 = read_i32(f);
            int32_t val1 = read_i32(f);
            auto floats = read_f32_slice(f, 12);
            int32_t val2 = read_i32(f);
            std::cerr << std::format("  [{}] field0={} field1={} field14={:#010x}\n",
                i, val0, val1, static_cast<unsigned>(val2));
        }
    }

    return 0;
}
