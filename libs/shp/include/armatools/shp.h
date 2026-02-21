#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace armatools::shp {

enum class ShapeType : int32_t {
    poly_line = 3,
    polygon = 5,
};

struct Point {
    double x = 0;
    double y = 0;
};

struct Field {
    std::string name; // max 10 chars
    char type = 'C';  // 'C' = string, 'N' = numeric, 'F' = float
    uint8_t size = 0;
    uint8_t dec = 0;
};

struct BBox {
    double x_min = 0, y_min = 0, x_max = 0, y_max = 0;
};

using AttrValue = std::variant<std::string, int64_t, double>;

// --- Writer ---

class Writer {
public:
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    void write_poly_line(const std::vector<std::vector<Point>>& parts,
                         const std::vector<AttrValue>& attrs);
    void write_polygon(const std::vector<std::vector<Point>>& rings,
                       const std::vector<AttrValue>& attrs);
    void close();

    static std::unique_ptr<Writer> create(const std::string& base_path,
                                           ShapeType shape_type,
                                           const std::vector<Field>& fields);
private:
    Writer() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// --- Reader ---

struct Record {
    ShapeType type{};
    BBox bbox;
    std::vector<std::vector<Point>> parts;
    std::unordered_map<std::string, std::string> attrs;
};

struct File {
    ShapeType type{};
    BBox bbox;
    std::vector<Field> fields;
    std::vector<Record> records;

    // Polylines returns all record parts as flat polylines.
    std::vector<std::vector<std::array<double, 2>>> polylines() const;
};

// open reads an ESRI Shapefile set (.shp + .dbf).
File open(const std::string& base_path);

// read_bbox reads just the bounding box from a .shp file header.
BBox read_bbox(const std::string& shp_path);

// attr_float64 returns a float64 attribute value, or 0 if not found.
double attr_float64(const std::unordered_map<std::string, std::string>& attrs,
                    const std::string& key);

// attr_int returns an int attribute value, or 0 if not found.
int attr_int(const std::unordered_map<std::string, std::string>& attrs,
             const std::string& key);

} // namespace armatools::shp
