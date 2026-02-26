#include "model_view_panel_presenter.h"

#include <algorithm>
#include <sstream>

void ModelViewPanelPresenter::clear() {
    active_lod_indices_.clear();
    named_selection_items_.clear();
    active_named_selections_.clear();
    selection_vertices_.clear();
    selection_face_geometry_.clear();
    lod_vertices_.clear();
}

int ModelViewPanelPresenter::choose_default_lod_index(
    const std::vector<armatools::p3d::LOD>& lods) const {
    for (size_t i = 0; i < lods.size(); ++i) {
        const auto& lod = lods[i];
        if (lod.face_count > 0 && !lod.face_data.empty() && !lod.vertices.empty()) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void ModelViewPanelPresenter::set_single_active_lod(int lod_index) {
    active_lod_indices_.clear();
    active_lod_indices_.insert(lod_index);
}

bool ModelViewPanelPresenter::set_lod_active(int lod_index, bool active) {
    if (active) {
        active_lod_indices_.insert(lod_index);
        return true;
    }

    active_lod_indices_.erase(lod_index);
    if (active_lod_indices_.empty()) {
        active_lod_indices_.insert(lod_index);
        return false;
    }
    return true;
}

bool ModelViewPanelPresenter::is_lod_active(int lod_index) const {
    return active_lod_indices_.count(lod_index) > 0;
}

std::vector<int> ModelViewPanelPresenter::sorted_active_lod_indices(size_t lod_count) const {
    std::vector<int> indices(active_lod_indices_.begin(), active_lod_indices_.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(
        std::remove_if(indices.begin(), indices.end(), [lod_count](int idx) {
            return idx < 0 || static_cast<size_t>(idx) >= lod_count;
        }),
        indices.end());
    return indices;
}

void ModelViewPanelPresenter::set_named_selection_source(const armatools::p3d::LOD& lod) {
    active_named_selections_.clear();
    named_selection_items_.clear();
    selection_vertices_ = lod.named_selection_vertices;
    selection_face_geometry_.clear();
    lod_vertices_ = lod.vertices;

    for (const auto& name : lod.named_selections) {
        size_t vertex_count = 0;
        const auto vit = lod.named_selection_vertices.find(name);
        if (vit != lod.named_selection_vertices.end()) {
            vertex_count = vit->second.size();
        }

        size_t face_count = 0;
        const auto fit = lod.named_selection_faces.find(name);
        if (fit != lod.named_selection_faces.end()) {
            face_count = fit->second.size();
        }

        NamedSelectionItem item;
        item.name = name;
        item.label = name + " (F:" + std::to_string(face_count)
            + ", V:" + std::to_string(vertex_count) + ")";
        named_selection_items_.push_back(std::move(item));
    }

    if (lod_vertices_.empty() || lod.named_selection_faces.empty()) {
        return;
    }

    auto append_edge = [this](uint32_t a, uint32_t b, std::vector<float>& dest) {
        if (a >= lod_vertices_.size() || b >= lod_vertices_.size()) {
            return;
        }
        const auto& pa = lod_vertices_[static_cast<size_t>(a)];
        const auto& pb = lod_vertices_[static_cast<size_t>(b)];
        dest.push_back(-pa[0]);
        dest.push_back(pa[1]);
        dest.push_back(pa[2]);
        dest.push_back(-pb[0]);
        dest.push_back(pb[1]);
        dest.push_back(pb[2]);
    };

    for (const auto& [name, face_indices] : lod.named_selection_faces) {
        std::vector<float> geom;
        geom.reserve(face_indices.size() * 6);
        for (auto face_index : face_indices) {
            if (face_index >= lod.faces.size()) {
                continue;
            }
            const auto& face = lod.faces[face_index];
            if (face.size() < 2) {
                continue;
            }
            for (size_t i = 0; i < face.size(); ++i) {
                append_edge(face[i], face[(i + 1) % face.size()], geom);
            }
        }
        if (!geom.empty()) {
            selection_face_geometry_.emplace(name, std::move(geom));
        }
    }
}

const std::vector<ModelViewPanelPresenter::NamedSelectionItem>&
ModelViewPanelPresenter::named_selection_items() const {
    return named_selection_items_;
}

size_t ModelViewPanelPresenter::named_selection_count() const {
    return named_selection_items_.size();
}

void ModelViewPanelPresenter::set_named_selection_active(const std::string& name, bool active) {
    if (active) {
        active_named_selections_.insert(name);
    } else {
        active_named_selections_.erase(name);
    }
}

ModelViewPanelPresenter::HighlightGeometry
ModelViewPanelPresenter::build_highlight_geometry() const {
    HighlightGeometry out;
    out.mode = modelview::HighlightMode::Points;

    if (active_named_selections_.empty()) {
        out.debug_message = "Named selection highlight: no active selections";
        return out;
    }

    std::vector<float> highlight_lines;
    std::unordered_set<uint32_t> merged_vertices;
    std::ostringstream dbg;
    dbg << "Named selection highlight: ";
    bool first = true;
    for (const auto& name : active_named_selections_) {
        if (!first) {
            dbg << ", ";
        }
        first = false;
        dbg << name;

        const auto face_it = selection_face_geometry_.find(name);
        if (face_it != selection_face_geometry_.end() && !face_it->second.empty()) {
            dbg << "(faces)";
            highlight_lines.insert(highlight_lines.end(),
                                   face_it->second.begin(), face_it->second.end());
            continue;
        }

        const auto vert_it = selection_vertices_.find(name);
        if (vert_it != selection_vertices_.end() && !vert_it->second.empty()) {
            dbg << "(verts " << vert_it->second.size() << ")";
            merged_vertices.insert(vert_it->second.begin(), vert_it->second.end());
            continue;
        }

        dbg << "(missing)";
    }

    if (!highlight_lines.empty()) {
        dbg << " -> face edges: " << highlight_lines.size() / 6;
        out.mode = modelview::HighlightMode::Lines;
        out.positions = std::move(highlight_lines);
        out.debug_message = dbg.str();
        return out;
    }

    if (!merged_vertices.empty() && !lod_vertices_.empty()) {
        out.positions.reserve(merged_vertices.size() * 3);
        for (auto idx : merged_vertices) {
            if (idx >= lod_vertices_.size()) {
                continue;
            }
            const auto& p = lod_vertices_[static_cast<size_t>(idx)];
            out.positions.push_back(-p[0]);
            out.positions.push_back(p[1]);
            out.positions.push_back(p[2]);
        }
    }

    if (!out.positions.empty()) {
        dbg << " -> vertices: " << out.positions.size() / 3;
        out.debug_message = dbg.str();
        return out;
    }

    dbg << " -> nothing to highlight";
    out.debug_message = dbg.str();
    return out;
}
