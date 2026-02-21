#include <armatools/roadnet.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace armatools::roadnet {
constexpr double kPi = 3.141592653589793238462643383279502884;

// ---------------------------------------------------------------------------
// Property tables
// ---------------------------------------------------------------------------

const std::unordered_map<RoadType, RoadProps>& ofp_road_props() {
    static const std::unordered_map<RoadType, RoadProps> table = {
        {type_asphalt,     {1, 1, 10, 12, "main road"}},
        {type_silnice,     {2, 2, 8,  10, "road"}},
        {type_cobblestone, {3, 3, 6,  8,  "road"}},
        {type_path,        {4, 4, 4,  6,  "track"}},
        {type_bridge,      {5, 1, 10, 12, "main road"}},
    };
    return table;
}

const std::unordered_map<RoadType, RoadProps>& oprw_road_props() {
    static const std::unordered_map<RoadType, RoadProps> table = {
        {type_highway,  {1, 1, 12, 14, "main road"}},
        {type_asphalt,  {2, 2, 8,  10, "main road"}},
        {type_concrete, {3, 3, 6,  8,  "road"}},
        {type_dirt,     {4, 4, 4,  6,  "track"}},
        {type_road,     {5, 5, 6,  8,  "road"}},
    };
    return table;
}

// ---------------------------------------------------------------------------
// OPRW classification
// ---------------------------------------------------------------------------

RoadType classify_p3d(const std::string& p3d_path) {
    std::string s = p3d_path;
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (s.find("highway") != std::string::npos) return type_highway;
    if (s.find("asphalt") != std::string::npos) return type_asphalt;
    if (s.find("concrete") != std::string::npos) return type_concrete;
    if (s.find("dirt") != std::string::npos || s.find("gravel") != std::string::npos) return type_dirt;
    return type_road;
}

// ---------------------------------------------------------------------------
// OPRW road link extraction
// ---------------------------------------------------------------------------

std::vector<Polyline> extract_from_road_links(const std::vector<std::vector<wrp::RoadLink>>& links) {
    std::vector<Polyline> polylines;

    for (auto& cell_links : links) {
        for (auto& link : cell_links) {
            if (link.positions.size() < 2) continue;

            RoadType rt = classify_p3d(link.p3d_path);
            auto it = oprw_road_props().find(rt);
            RoadProps props = (it != oprw_road_props().end()) ? it->second : RoadProps{};

            std::vector<std::array<double, 2>> points(link.positions.size());
            for (size_t i = 0; i < link.positions.size(); i++) {
                points[i] = {static_cast<double>(link.positions[i][0]),
                             static_cast<double>(link.positions[i][2])};
            }

            double length = 0;
            for (size_t i = 1; i < points.size(); i++) {
                double dx = points[i][0] - points[i-1][0];
                double dz = points[i][1] - points[i-1][1];
                length += std::sqrt(dx*dx + dz*dz);
            }

            polylines.push_back({
                std::move(points), rt, props, length, 0, {}, {}, link.p3d_path
            });
        }
    }
    return polylines;
}

// ---------------------------------------------------------------------------
// OFP internal types
// ---------------------------------------------------------------------------

enum class SegShape { straight, curve, dead_end };

struct SegGeom {
    SegShape shape = SegShape::straight;
    double length = 0;
    double half = 0;
    double angle = 0;
    double radius = 0;
};

struct RoadSeg {
    int obj_idx = 0;
    std::string model;
    RoadType type;
    SegGeom geom;
    std::array<double, 2> center{};
    double elev = 0;
    std::array<double, 2> fwd_dir{};
    std::array<double, 2> front{};
    std::array<double, 2> back{};
};

struct Intersection {
    int obj_idx = 0;
    std::string model;
    std::array<double, 2> center{};
    double elev = 0;
    std::array<double, 2> fwd_dir{};
    bool is_xroad = false;
};

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

static std::array<double, 2> fwd_xz(const std::array<float, 12>& m) {
    double fx = static_cast<double>(m[6]);
    double fz = static_cast<double>(m[8]);
    double n = std::sqrt(fx*fx + fz*fz);
    if (n < 1e-9) return {0, 1};
    return {fx / n, fz / n};
}

static double dist2d(const std::array<double, 2>& a, const std::array<double, 2>& b) {
    double dx = a[0] - b[0];
    double dz = a[1] - b[1];
    return std::sqrt(dx*dx + dz*dz);
}

// ---------------------------------------------------------------------------
// Road model parsing
// ---------------------------------------------------------------------------

struct RoadPrefix {
    const char* prefix;
    RoadType road_type;
};

static const RoadPrefix road_prefixes[] = {
    {"asfaltka", type_asphalt},
    {"asfatlka", type_asphalt}, // OFP typo
    {"silnice",  type_silnice},
    {"cesta",    type_path},
    {"asf",      type_asphalt},
    {"sil",      type_silnice},
    {"ces",      type_path},
    {"kos",      type_cobblestone},
};

static bool parse_suffix(const std::string& s, SegGeom& geom) {
    if (s == "6konec") {
        geom = {SegShape::dead_end, 6, 3, 0, 0};
        return true;
    }

    auto space = s.find(' ');
    if (space != std::string::npos) {
        try {
            double angle = std::stod(s.substr(0, space));
            double radius = std::stod(s.substr(space + 1));
            if (angle > 0 && radius > 0) {
                double chord = 2 * radius * std::sin(angle / 2 * kPi / 180.0);
                geom = {SegShape::curve, chord, chord / 2, angle, radius};
                return true;
            }
        } catch (...) {}
    }

    try {
        double length = std::stod(s);
        if (length > 0) {
            geom = {SegShape::straight, length, length / 2, 0, 0};
            return true;
        }
    } catch (...) {}

    return false;
}

static bool parse_road_model(const std::string& base, RoadType& rt, SegGeom& geom) {
    for (auto& p : road_prefixes) {
        std::string prefix = p.prefix;
        if (base.size() >= prefix.size() && base.substr(0, prefix.size()) == prefix) {
            if (parse_suffix(base.substr(prefix.size()), geom)) {
                rt = p.road_type;
                return true;
            }
        }
    }
    return false;
}

static RoadSeg make_road_seg(int idx, const wrp::ObjectRecord& obj, const RoadType& rt, const SegGeom& geom) {
    auto fwd = fwd_xz(obj.transform);
    double cx = obj.position[0], cz = obj.position[2];
    return {
        idx, obj.model_name, rt, geom,
        {cx, cz}, obj.position[1], fwd,
        {cx + geom.half * fwd[0], cz + geom.half * fwd[1]},
        {cx - geom.half * fwd[0], cz - geom.half * fwd[1]}
    };
}

static void classify_objects(const std::vector<wrp::ObjectRecord>& objects,
                             std::vector<RoadSeg>& segs, std::vector<Intersection>& intxs) {
    for (size_t i = 0; i < objects.size(); i++) {
        auto& obj = objects[i];
        std::string base = base_name(obj.model_name);

        if (base.substr(0, 3) == "kr_") {
            intxs.push_back({
                static_cast<int>(i), obj.model_name,
                {obj.position[0], obj.position[2]}, obj.position[1],
                fwd_xz(obj.transform), base.find('x') != std::string::npos
            });
            continue;
        }

        if (base == "nam_okruzi" || base == "nam_dlazba") {
            intxs.push_back({
                static_cast<int>(i), obj.model_name,
                {obj.position[0], obj.position[2]}, obj.position[1],
                fwd_xz(obj.transform), false
            });
            continue;
        }

        if (base == "most_stred30") {
            SegGeom geom = {SegShape::straight, 50, 25, 0, 0};
            segs.push_back(make_road_seg(static_cast<int>(i), obj, type_bridge, geom));
            continue;
        }

        RoadType rt;
        SegGeom geom;
        if (parse_road_model(base, rt, geom)) {
            segs.push_back(make_road_seg(static_cast<int>(i), obj, rt, geom));
        }
    }
}

// ---------------------------------------------------------------------------
// Network building & tracing
// ---------------------------------------------------------------------------

static constexpr double seg_match_tol = 3.0;
static constexpr double intx_match_tol = 10.0;
static constexpr double cell_size = 10.0;

struct Peer {
    int seg = -1;
    bool front = false;
    int intx = -1;

    bool connected() const { return seg >= 0 || intx >= 0; }
    bool is_segment() const { return seg >= 0; }
    bool is_intx() const { return intx >= 0 && seg < 0; }
};

static std::array<int, 2> hash_cell_key(const std::array<double, 2>& pos) {
    return {static_cast<int>(std::floor(pos[0] / cell_size)),
            static_cast<int>(std::floor(pos[1] / cell_size))};
}

struct Network {
    std::vector<RoadSeg> segs;
    std::vector<Intersection> intxs;
    std::vector<std::array<Peer, 2>> adj; // [back, front] per segment

    void build();
    std::vector<Polyline> trace_all();

private:
    std::pair<int, int> find_chain_start(int seg_idx, std::vector<bool>& visited);
    Polyline trace_from(int start_idx, int start_port, std::vector<bool>& visited);
};

void Network::build() {
    adj.resize(segs.size());

    struct EpEntry {
        int seg_idx;
        bool is_front;
        std::array<double, 2> pos;
    };

    // Spatial hash for endpoints
    std::map<std::array<int, 2>, std::vector<EpEntry>> hash;
    std::vector<EpEntry> all_eps;

    for (size_t i = 0; i < segs.size(); i++) {
        for (bool is_front : {false, true}) {
            auto& pos = is_front ? segs[i].front : segs[i].back;
            EpEntry ep{static_cast<int>(i), is_front, pos};
            all_eps.push_back(ep);
            hash[hash_cell_key(pos)].push_back(ep);
        }
    }

    // Find best segment-segment matches
    struct MatchCandidate {
        EpEntry a, b;
        double dist;
    };
    std::vector<MatchCandidate> candidates;

    for (auto& ep : all_eps) {
        auto ck = hash_cell_key(ep.pos);
        double best_dist = seg_match_tol;
        EpEntry best_peer{};
        bool found = false;

        for (int dx = -1; dx <= 1; dx++) {
            for (int dz = -1; dz <= 1; dz++) {
                auto nk = std::array<int, 2>{ck[0] + dx, ck[1] + dz};
                auto it = hash.find(nk);
                if (it == hash.end()) continue;
                for (auto& other : it->second) {
                    if (other.seg_idx == ep.seg_idx) continue;
                    double d = dist2d(ep.pos, other.pos);
                    if (d < best_dist) {
                        best_dist = d;
                        best_peer = other;
                        found = true;
                    }
                }
            }
        }
        if (found) {
            candidates.push_back({ep, best_peer, best_dist});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](auto& a, auto& b) { return a.dist < b.dist; });

    std::unordered_set<int64_t> matched;
    auto port_key = [](int seg_idx, bool is_front) -> int64_t {
        return static_cast<int64_t>(seg_idx) * 2 + (is_front ? 1 : 0);
    };

    for (auto& c : candidates) {
        int64_t ka = port_key(c.a.seg_idx, c.a.is_front);
        int64_t kb = port_key(c.b.seg_idx, c.b.is_front);
        if (matched.count(ka) || matched.count(kb)) continue;
        matched.insert(ka);
        matched.insert(kb);

        size_t pa = c.a.is_front ? 1 : 0;
        size_t pb = c.b.is_front ? 1 : 0;
        adj[static_cast<size_t>(c.a.seg_idx)][pa] = {c.b.seg_idx, c.b.is_front, -1};
        adj[static_cast<size_t>(c.b.seg_idx)][pb] = {c.a.seg_idx, c.a.is_front, -1};
    }

    // Match unmatched endpoints to intersections
    if (!intxs.empty()) {
        std::map<std::array<int, 2>, std::vector<int>> intx_hash;
        for (size_t i = 0; i < intxs.size(); i++) {
            intx_hash[hash_cell_key(intxs[i].center)].push_back(static_cast<int>(i));
        }

        for (size_t seg_idx = 0; seg_idx < segs.size(); seg_idx++) {
            for (size_t port_idx = 0; port_idx < 2; port_idx++) {
                if (adj[seg_idx][port_idx].connected()) continue;
                auto& pos = (port_idx == 1) ? segs[seg_idx].front : segs[seg_idx].back;
                auto ck = hash_cell_key(pos);
                double best_dist = intx_match_tol;
                int best_intx = -1;

                for (int dx = -1; dx <= 1; dx++) {
                    for (int dz = -1; dz <= 1; dz++) {
                        auto nk = std::array<int, 2>{ck[0] + dx, ck[1] + dz};
                        auto it = intx_hash.find(nk);
                        if (it == intx_hash.end()) continue;
                        for (int intx_idx : it->second) {
                            double d = dist2d(pos, intxs[static_cast<size_t>(intx_idx)].center);
                            if (d < best_dist) {
                                best_dist = d;
                                best_intx = intx_idx;
                            }
                        }
                    }
                }
                if (best_intx >= 0) {
                    adj[seg_idx][port_idx] = {-1, false, best_intx};
                }
            }
        }
    }
}

std::pair<int, int> Network::find_chain_start(int seg_idx, std::vector<bool>& visited) {
    int cur = seg_idx;
    int entry_port = 0;

    std::unordered_set<int> seen;
    for (;;) {
        seen.insert(cur);
        auto& p = adj[static_cast<size_t>(cur)][static_cast<size_t>(entry_port)];
        if (!p.is_segment()) return {cur, entry_port};
        if (seen.count(p.seg) || visited[static_cast<size_t>(p.seg)]) return {cur, entry_port};

        int next_cur = p.seg;
        int next_entry = p.front ? 0 : 1;
        cur = next_cur;
        entry_port = next_entry;
    }
}

Polyline Network::trace_from(int start_idx, int start_port, std::vector<bool>& visited) {
    auto& seg = segs[static_cast<size_t>(start_idx)];
    Polyline pl;
    pl.type = seg.type;

    auto& start_peer = adj[static_cast<size_t>(start_idx)][static_cast<size_t>(start_port)];
    if (start_peer.is_intx()) {
        pl.start_kind = "intersection";
        pl.points.push_back(intxs[static_cast<size_t>(start_peer.intx)].center);
    } else {
        pl.start_kind = "dead_end";
        pl.points.push_back(start_port == 0 ? seg.back : seg.front);
    }

    int cur = start_idx;
    int exit_port = 1 - start_port;

    for (;;) {
        auto& s = segs[static_cast<size_t>(cur)];
        visited[static_cast<size_t>(cur)] = true;
        pl.seg_count++;
        pl.length += s.geom.length;
        pl.points.push_back(s.center);

        auto& p = adj[static_cast<size_t>(cur)][static_cast<size_t>(exit_port)];
        if (p.is_intx()) {
            pl.end_kind = "intersection";
            pl.points.push_back(intxs[static_cast<size_t>(p.intx)].center);
            break;
        }
        if (!p.is_segment()) {
            pl.end_kind = "dead_end";
            pl.points.push_back(exit_port == 1 ? s.front : s.back);
            break;
        }
        if (visited[static_cast<size_t>(p.seg)]) {
            pl.end_kind = "loop";
            break;
        }

        auto& next_seg = segs[static_cast<size_t>(p.seg)];
        if (next_seg.type != pl.type) {
            pl.end_kind = "type_change";
            pl.points.push_back(exit_port == 1 ? s.front : s.back);
            break;
        }

        cur = p.seg;
        exit_port = p.front ? 0 : 1;
    }

    return pl;
}

std::vector<Polyline> Network::trace_all() {
    std::vector<bool> visited(segs.size(), false);
    std::vector<Polyline> polylines;

    // First pass: start from dead ends and intersections
    for (size_t i = 0; i < segs.size(); i++) {
        if (visited[i]) continue;
        auto [chain_start, start_port] = find_chain_start(static_cast<int>(i), visited);
        if (visited[static_cast<size_t>(chain_start)]) continue;
        auto pl = trace_from(chain_start, start_port, visited);
        if (pl.seg_count > 0) polylines.push_back(std::move(pl));
    }

    // Second pass: loops
    for (size_t i = 0; i < segs.size(); i++) {
        if (visited[i]) continue;
        auto pl = trace_from(static_cast<int>(i), 1, visited);
        if (pl.seg_count > 0) {
            pl.start_kind = "loop";
            pl.end_kind = "loop";
            if (pl.points.size() > 1) pl.points.push_back(pl.points[0]);
            polylines.push_back(std::move(pl));
        }
    }

    return polylines;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<Polyline> extract_from_objects(const std::vector<wrp::ObjectRecord>& objects) {
    std::vector<RoadSeg> segs;
    std::vector<Intersection> intxs;
    classify_objects(objects, segs, intxs);

    if (segs.empty()) return {};

    Network net;
    net.segs = std::move(segs);
    net.intxs = std::move(intxs);
    net.build();
    auto traced = net.trace_all();

    std::vector<Polyline> polylines;
    polylines.reserve(traced.size());
    for (auto& pl : traced) {
        if (pl.points.size() < 2) continue;
        auto it = ofp_road_props().find(pl.type);
        if (it != ofp_road_props().end()) pl.props = it->second;
        polylines.push_back(std::move(pl));
    }
    return polylines;
}

} // namespace armatools::roadnet
