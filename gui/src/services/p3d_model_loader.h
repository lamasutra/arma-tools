#pragma once

#include <armatools/pboindex.h>
#include <armatools/p3d.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Config;

// P3dModelLoaderService loads P3D model files on demand.
//
// P3D is the Arma 3 3D model format.  Models can live either as plain files on
// disk or embedded inside PBO archives (the game's archive format).
//
// This service uses the PBO index to resolve virtual paths (e.g.
// "a3\characters_f\head.p3d") to the real data, then parses the binary P3D
// format into an in-memory representation.
//
// Shared by the P3D Info tab, Asset Browser, WRP Info, and OBJ Replace tabs.
class P3dModelLoaderService : public std::enable_shared_from_this<P3dModelLoaderService> {
public:
    // Create the service with configuration and the PBO index.
    // cfg_in   - application config, used to resolve tool paths.
    // db_in    - PBO database mapping logical names to physical PBO files.
    // index_in - PBO index allowing virtual path lookup inside PBO archives.
    P3dModelLoaderService(Config* cfg_in,
                          const std::shared_ptr<armatools::pboindex::DB>& db_in,
                          const std::shared_ptr<armatools::pboindex::Index>& index_in);

    // Load and parse the P3D file from model_path.
    // model_path can be a physical disk path or a virtual path like "a3\...\model.p3d".
    // Returns a parsed P3DFile struct; check its validity before use.
    armatools::p3d::P3DFile load_p3d(const std::string& model_path);

private:
    std::string db_path;             // Path to the A3 PBO database file.
    Config* cfg = nullptr;           // Pointer to app config (not owned; must outlive this).
    std::shared_ptr<armatools::pboindex::DB> db;      // PBO database for path lookup.
    std::shared_ptr<armatools::pboindex::Index> index; // PBO index for virtual path resolution.

    // Internal helper: given raw binary data, parse it as a P3D file.
    armatools::p3d::P3DFile try_load_p3d_from_data(const std::vector<uint8_t>& data);
};

