#pragma once

#include "domain/model_view_panel_types.h"

#include <armatools/p3d.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ModelViewPanelPresenter {
public:
    using HighlightGeometry = modelview::HighlightGeometry;
    using NamedSelectionItem = modelview::NamedSelectionItem;

    void clear();

    [[nodiscard]] int choose_default_lod_index(
        const std::vector<armatools::p3d::LOD>& lods) const;
    void set_single_active_lod(int lod_index);
    [[nodiscard]] bool set_lod_active(int lod_index, bool active);
    [[nodiscard]] bool is_lod_active(int lod_index) const;
    [[nodiscard]] std::vector<int> sorted_active_lod_indices(size_t lod_count) const;

    void set_named_selection_source(const armatools::p3d::LOD& lod);
    [[nodiscard]] const std::vector<NamedSelectionItem>& named_selection_items() const;
    [[nodiscard]] size_t named_selection_count() const;

    void set_named_selection_active(const std::string& name, bool active);
    [[nodiscard]] HighlightGeometry build_highlight_geometry() const;

private:
    std::unordered_set<int> active_lod_indices_;
    std::vector<NamedSelectionItem> named_selection_items_;
    std::unordered_set<std::string> active_named_selections_;
    std::unordered_map<std::string, std::vector<uint32_t>> selection_vertices_;
    std::unordered_map<std::string, std::vector<float>> selection_face_geometry_;
    std::vector<armatools::p3d::Vector3P> lod_vertices_;
};
