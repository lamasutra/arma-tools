#pragma once

#include "domain/wrp_terrain_camera_types.h"

#include <gtkmm.h>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <armatools/wrp.h>

class TexturesLoaderService;
class P3dModelLoaderService;

namespace render_domain {

class WrpTerrainWidget : public Gtk::Box {
public:
    WrpTerrainWidget();
    ~WrpTerrainWidget() override;

    void clear_world();
    void set_world_data(const armatools::wrp::WorldData& world);
    void set_objects(std::vector<armatools::wrp::ObjectRecord> objects);
    void set_wireframe(bool on);
    void set_show_objects(bool on);
    void set_object_max_distance(float distance_m);
    void set_object_category_filters(bool buildings, bool vegetation, bool rocks, bool props);
    void set_show_object_bounds(bool on);
    void set_show_water(bool on);
    void set_water_level(float level);
    void set_color_mode(int mode);
    void set_satellite_palette(const std::vector<std::array<float, 3>>& palette);
    void set_on_object_picked(std::function<void(size_t)> cb);
    void set_on_texture_debug_info(std::function<void(const std::string&)> cb);
    void set_on_terrain_stats(std::function<void(const std::string&)> cb);
    void set_on_compass_info(std::function<void(const std::string&)> cb);
    void set_model_loader_service(const std::shared_ptr<P3dModelLoaderService>& service);
    void set_texture_loader_service(const std::shared_ptr<TexturesLoaderService>& service);
    void set_show_patch_boundaries(bool on);
    void set_show_patch_lod_colors(bool on);
    void set_show_tile_boundaries(bool on);
    void set_terrain_far_distance(float distance_m);
    void set_material_quality_distances(float mid_distance_m, float far_distance_m);
    void set_seam_debug_mode(int mode);
    void set_camera_mode(wrpterrain::CameraMode mode);
    [[nodiscard]] wrpterrain::CameraMode camera_mode() const;

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
    Gtk::Box fallback_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label fallback_label_;

    std::function<void(size_t)> on_object_picked_;
    std::function<void(const std::string&)> on_texture_debug_info_;
    std::function<void(const std::string&)> on_terrain_stats_;
    std::function<void(const std::string&)> on_compass_info_;

    bool has_gles() const;
    void emit_fallback_status();
};

}  // namespace render_domain
