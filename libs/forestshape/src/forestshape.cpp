#include <armatools/forestshape.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace armatools::forestshape {

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct ForestBlock {
    int obj_idx = 0;
    std::string model;
    std::array<double, 2> pos{};
    ForestType type;
    bool is_square = false;
    int yaw = 0; // normalized: 0, 90, 180, 270
};

struct CellKey {
    int col = 0, row = 0;
    bool operator==(const CellKey& o) const { return col == o.col && row == o.row; }
};

struct CellKeyHash {
    size_t operator()(const CellKey& k) const {
        return std::hash<int64_t>{}(static_cast<int64_t>(k.col) * 100003 + k.row);
    }
};

struct CellInfo {
    bool is_square = false;
    int tri_yaw = 0;
};

static constexpr double grid_cell_size = 50.0;
static constexpr double grid_half = 25.0;

enum Dir { dir_n = 0, dir_e = 1, dir_s = 2, dir_w = 3 };

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string base_name(const std::string& model_name) {
    std::string s = model_name;
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto pos = s.find_last_of("\\/");
    if (pos != std::string::npos) s = s.substr(pos + 1);
    if (s.size() > 4 && s.substr(s.size() - 4) == ".p3d") s.resize(s.size() - 4);
    return s;
}

static int normalize_yaw(double yaw) {
    double deg = std::fmod(yaw, 360.0);
    if (deg < 0) deg += 360;
    int q = static_cast<int>(std::round(deg / 90.0)) % 4;
    return q * 90;
}

static bool covers_direction(const CellInfo& ci, Dir dir) {
    if (ci.is_square) return true;
    switch (ci.tri_yaw) {
    case 0:   return dir == dir_n || dir == dir_w;
    case 90:  return dir == dir_n || dir == dir_e;
    case 180: return dir == dir_s || dir == dir_e;
    case 270: return dir == dir_s || dir == dir_w;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Forest block classification
// ---------------------------------------------------------------------------

static std::vector<ForestBlock> classify_forest(const std::vector<wrp::ObjectRecord>& objects) {
    std::vector<ForestBlock> blocks;

    for (size_t i = 0; i < objects.size(); i++) {
        auto& obj = objects[i];
        std::string base = base_name(obj.model_name);

        if (base.substr(0, 3) != "les") continue;
        if (base.find("mlaz") != std::string::npos) continue;
        if (base.find("singlestrom") != std::string::npos) continue;

        ForestBlock fb;
        fb.obj_idx = static_cast<int>(i);
        fb.model = obj.model_name;
        fb.pos = {obj.position[0], obj.position[2]};
        fb.is_square = (base.find("trojuhelnik") == std::string::npos);
        fb.yaw = normalize_yaw(obj.rotation.yaw);
        fb.type = (base.find("jehl") != std::string::npos) ? forest_conifer : forest_mixed;

        blocks.push_back(std::move(fb));
    }
    return blocks;
}

// ---------------------------------------------------------------------------
// Forest grid
// ---------------------------------------------------------------------------

struct ForestGrid {
    std::unordered_map<CellKey, CellInfo, CellKeyHash> cells;
    double phase_x = 0, phase_z = 0;

    CellKey snap(const std::array<double, 2>& pos) const {
        return {
            static_cast<int>(std::round((pos[0] - phase_x) / grid_cell_size)),
            static_cast<int>(std::round((pos[1] - phase_z) / grid_cell_size))
        };
    }

    std::array<double, 2> vertex_world(int vx, int vy) const {
        return {
            phase_x + static_cast<double>(vx) * grid_cell_size - grid_half,
            phase_z + static_cast<double>(vy) * grid_cell_size - grid_half
        };
    }
};

static double pos_mod(double x, double m) {
    double r = std::fmod(x, m);
    if (r < 0) r += m;
    return r;
}

static void detect_phase(const std::vector<ForestBlock>& blocks, double& px, double& pz) {
    for (auto& b : blocks) {
        if (!b.is_square) {
            px = pos_mod(b.pos[0], grid_cell_size);
            pz = pos_mod(b.pos[1], grid_cell_size);
            return;
        }
    }
    double sx = std::round(blocks[0].pos[0] / 5) * 5;
    double sz = std::round(blocks[0].pos[1] / 5) * 5;
    px = pos_mod(sx, grid_cell_size);
    pz = pos_mod(sz, grid_cell_size);
}

static ForestGrid build_forest_grid(const std::vector<ForestBlock>& blocks) {
    ForestGrid g;
    if (blocks.empty()) return g;

    detect_phase(blocks, g.phase_x, g.phase_z);

    for (auto& b : blocks) {
        CellKey ck = g.snap(b.pos);
        auto it = g.cells.find(ck);
        if (it != g.cells.end() && it->second.is_square) continue;
        if (b.is_square) {
            g.cells[ck] = {true, 0};
        } else {
            g.cells[ck] = {false, b.yaw};
        }
    }
    return g;
}

// ---------------------------------------------------------------------------
// Boundary tracing
// ---------------------------------------------------------------------------

struct Vtx {
    int x = 0, y = 0;
    bool operator==(const Vtx& o) const { return x == o.x && y == o.y; }
};

struct VtxHash {
    size_t operator()(const Vtx& v) const {
        return std::hash<int64_t>{}(static_cast<int64_t>(v.x) * 100003 + v.y);
    }
};

static double shoelace(const std::vector<std::array<double, 2>>& ring) {
    if (ring.size() < 3) return 0;
    double area = 0;
    for (size_t i = 0; i < ring.size() - 1; i++) {
        area += ring[i][0] * ring[i+1][1] - ring[i+1][0] * ring[i][1];
    }
    return area / 2;
}

static std::vector<Polygon> trace_polygons(ForestGrid& g) {
    if (g.cells.empty()) return {};

    // Flood fill to find connected components
    std::unordered_set<CellKey, CellKeyHash> visited;
    std::vector<std::vector<CellKey>> components;

    struct NeighborCheck {
        int dc, dr;
        Dir dir_from, dir_to;
    };
    static const NeighborCheck neighbors[] = {
        {-1, 0, dir_w, dir_e},
        {+1, 0, dir_e, dir_w},
        {0, -1, dir_s, dir_n},
        {0, +1, dir_n, dir_s},
    };

    for (auto& [ck, ci] : g.cells) {
        if (visited.count(ck)) continue;
        visited.insert(ck);

        std::vector<CellKey> comp;
        std::queue<CellKey> queue;
        queue.push(ck);

        while (!queue.empty()) {
            CellKey cur = queue.front();
            queue.pop();
            comp.push_back(cur);
            auto& cur_ci = g.cells[cur];

            for (auto& nb : neighbors) {
                CellKey nk{cur.col + nb.dc, cur.row + nb.dr};
                if (visited.count(nk)) continue;
                auto nit = g.cells.find(nk);
                if (nit == g.cells.end()) continue;
                if (covers_direction(cur_ci, nb.dir_from) &&
                    covers_direction(nit->second, nb.dir_to)) {
                    visited.insert(nk);
                    queue.push(nk);
                }
            }
        }
        components.push_back(std::move(comp));
    }

    // Sort by size (largest first)
    std::sort(components.begin(), components.end(),
              [](auto& a, auto& b) { return a.size() > b.size(); });

    std::vector<Polygon> polygons;

    for (auto& comp : components) {
        std::unordered_map<CellKey, CellInfo, CellKeyHash> comp_set;
        for (auto& ck : comp) comp_set[ck] = g.cells[ck];

        auto is_boundary = [&](int c, int r, Dir dir) -> bool {
            CellKey nk;
            Dir facing;
            switch (dir) {
            case dir_s: nk = {c, r - 1}; facing = dir_n; break;
            case dir_e: nk = {c + 1, r}; facing = dir_w; break;
            case dir_n: nk = {c, r + 1}; facing = dir_s; break;
            case dir_w: nk = {c - 1, r}; facing = dir_e; break;
            }
            auto it = comp_set.find(nk);
            if (it == comp_set.end()) return true;
            return !covers_direction(it->second, facing);
        };

        // Collect directed boundary edges
        std::unordered_map<Vtx, std::vector<Vtx>, VtxHash> edge_map;
        auto add_edge = [&](Vtx from, Vtx to) {
            edge_map[from].push_back(to);
        };

        double area = 0;

        for (auto& ck : comp) {
            int c = ck.col, r = ck.row;
            auto& ci = comp_set[ck];

            if (ci.is_square) {
                if (is_boundary(c, r, dir_s)) add_edge({c, r}, {c+1, r});
                if (is_boundary(c, r, dir_e)) add_edge({c+1, r}, {c+1, r+1});
                if (is_boundary(c, r, dir_n)) add_edge({c+1, r+1}, {c, r+1});
                if (is_boundary(c, r, dir_w)) add_edge({c, r+1}, {c, r});
                area += grid_cell_size * grid_cell_size;
            } else {
                switch (ci.tri_yaw) {
                case 0: // NW
                    if (is_boundary(c, r, dir_n)) add_edge({c+1, r+1}, {c, r+1});
                    if (is_boundary(c, r, dir_w)) add_edge({c, r+1}, {c, r});
                    add_edge({c, r}, {c+1, r+1});
                    break;
                case 90: // NE
                    if (is_boundary(c, r, dir_n)) add_edge({c+1, r+1}, {c, r+1});
                    if (is_boundary(c, r, dir_e)) add_edge({c+1, r}, {c+1, r+1});
                    add_edge({c, r+1}, {c+1, r});
                    break;
                case 180: // SE
                    if (is_boundary(c, r, dir_e)) add_edge({c+1, r}, {c+1, r+1});
                    if (is_boundary(c, r, dir_s)) add_edge({c, r}, {c+1, r});
                    add_edge({c+1, r+1}, {c, r});
                    break;
                case 270: // SW
                    if (is_boundary(c, r, dir_s)) add_edge({c, r}, {c+1, r});
                    if (is_boundary(c, r, dir_w)) add_edge({c, r+1}, {c, r});
                    add_edge({c+1, r}, {c, r+1});
                    break;
                }
                area += grid_cell_size * grid_cell_size / 2;
            }
        }

        // Chain edges into closed rings
        int max_edges = 4 * static_cast<int>(comp.size()) + 4;
        std::vector<std::vector<std::array<double, 2>>> rings;

        for (auto& [start, _] : edge_map) {
            while (!edge_map[start].empty()) {
                std::vector<std::array<double, 2>> ring;
                Vtx cur = start;
                for (int iter = 0; iter < max_edges; iter++) {
                    ring.push_back(g.vertex_world(cur.x, cur.y));
                    auto& edges = edge_map[cur];
                    if (edges.empty()) {
                        ring.push_back(g.vertex_world(start.x, start.y));
                        break;
                    }
                    Vtx nxt = edges.back();
                    edges.pop_back();
                    if (nxt == start) {
                        ring.push_back(g.vertex_world(start.x, start.y));
                        break;
                    }
                    cur = nxt;
                }
                if (ring.size() >= 4) rings.push_back(std::move(ring));
            }
        }

        Polygon poly;
        poly.cell_count = static_cast<int>(comp.size());
        poly.area = area;

        if (rings.size() == 1) {
            poly.exterior = std::move(rings[0]);
        } else if (rings.size() > 1) {
            double max_area = 0;
            size_t max_idx = 0;
            for (size_t i = 0; i < rings.size(); i++) {
                double a = std::abs(shoelace(rings[i]));
                if (a > max_area) { max_area = a; max_idx = i; }
            }
            poly.exterior = std::move(rings[max_idx]);
            for (size_t i = 0; i < rings.size(); i++) {
                if (i != max_idx) poly.holes.push_back(std::move(rings[i]));
            }
        }

        polygons.push_back(std::move(poly));
    }

    return polygons;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<Polygon> extract_from_objects(const std::vector<wrp::ObjectRecord>& objects) {
    auto blocks = classify_forest(objects);
    if (blocks.empty()) return {};

    // Group by forest type
    std::unordered_map<ForestType, std::vector<ForestBlock>> by_type;
    for (auto& b : blocks) by_type[b.type].push_back(std::move(b));

    std::vector<Polygon> polygons;
    for (auto& ft : {forest_mixed, forest_conifer}) {
        auto it = by_type.find(ft);
        if (it == by_type.end()) continue;

        auto grid = build_forest_grid(it->second);
        auto polys = trace_polygons(grid);
        for (auto& p : polys) p.type = ft;
        polygons.insert(polygons.end(),
                        std::make_move_iterator(polys.begin()),
                        std::make_move_iterator(polys.end()));
    }

    // Sort by area (largest first), assign IDs
    std::sort(polygons.begin(), polygons.end(),
              [](auto& a, auto& b) { return a.area > b.area; });
    for (size_t i = 0; i < polygons.size(); i++) {
        polygons[i].id = static_cast<int>(i) + 1;
    }

    return polygons;
}

} // namespace armatools::forestshape
