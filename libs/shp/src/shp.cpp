#include "armatools/shp.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <format>
#include <stdexcept>

namespace armatools::shp {

// --- Endian helpers for shapefile format (mixed big/little endian) ---

static void put_be32(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(v & 0xFF);
}

static uint32_t get_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

static void put_le32(uint8_t* dst, uint32_t v) { std::memcpy(dst, &v, 4); }
static void put_le16(uint8_t* dst, uint16_t v) { std::memcpy(dst, &v, 2); }
static void put_le64(uint8_t* dst, uint64_t v) { std::memcpy(dst, &v, 8); }

static uint32_t get_le32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
static uint16_t get_le16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
static uint64_t get_le64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

static double get_le_f64(const uint8_t* p) {
    uint64_t bits = get_le64(p);
    double v;
    std::memcpy(&v, &bits, 8);
    return v;
}

static void put_le_f64(uint8_t* dst, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    put_le64(dst, bits);
}

static void write_le32(std::ofstream& f, int32_t v) {
    f.write(reinterpret_cast<const char*>(&v), 4);
}

static void write_le_f64(std::ofstream& f, double v) {
    f.write(reinterpret_cast<const char*>(&v), 8);
}

// --- Writer implementation ---

struct Writer::Impl {
    std::ofstream shp, shx, dbf;
    std::vector<Field> fields;
    ShapeType shape_type;
    int rec_num = 0;
    int shp_offset = 50; // header is 100 bytes = 50 16-bit words
    double bbox[4] = {0, 0, 0, 0}; // xMin, yMin, xMax, yMax
    bool bbox_init = false;

    int record_size() const {
        int size = 1; // deletion flag
        for (const auto& f : fields) size += f.size;
        return size;
    }

    void write_file_header(std::ofstream& f) {
        std::array<uint8_t, 100> hdr{};
        put_be32(hdr.data(), 9994);
        put_be32(hdr.data() + 24, 50);
        put_le32(hdr.data() + 28, 1000);
        put_le32(hdr.data() + 32, static_cast<uint32_t>(shape_type));
        f.write(reinterpret_cast<const char*>(hdr.data()), 100);
    }

    void write_updated_header(std::ofstream& f, int file_len_words) {
        std::array<uint8_t, 100> hdr{};
        put_be32(hdr.data(), 9994);
        put_be32(hdr.data() + 24, static_cast<uint32_t>(file_len_words));
        put_le32(hdr.data() + 28, 1000);
        put_le32(hdr.data() + 32, static_cast<uint32_t>(shape_type));
        put_le_f64(hdr.data() + 36, bbox[0]);
        put_le_f64(hdr.data() + 44, bbox[1]);
        put_le_f64(hdr.data() + 52, bbox[2]);
        put_le_f64(hdr.data() + 60, bbox[3]);
        f.write(reinterpret_cast<const char*>(hdr.data()), 100);
    }

    void write_dbf_header() {
        int n_fields = static_cast<int>(fields.size());
        int header_size = 32 + n_fields * 32 + 1;
        std::array<uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        hdr[1] = 26; hdr[2] = 1; hdr[3] = 1;
        put_le16(hdr.data() + 8, static_cast<uint16_t>(header_size));
        put_le16(hdr.data() + 10, static_cast<uint16_t>(record_size()));
        dbf.write(reinterpret_cast<const char*>(hdr.data()), 32);

        for (const auto& f : fields) {
            std::array<uint8_t, 32> fd{};
            std::string name = f.name;
            if (name.size() > 10) name.resize(10);
            std::memcpy(fd.data(), name.c_str(), name.size());
            fd[11] = static_cast<uint8_t>(f.type);
            fd[16] = f.size;
            fd[17] = f.dec;
            dbf.write(reinterpret_cast<const char*>(fd.data()), 32);
        }
        uint8_t term = 0x0D;
        dbf.write(reinterpret_cast<const char*>(&term), 1);
    }

    void write_dbf_record(const std::vector<AttrValue>& attrs) {
        uint8_t del = 0x20;
        dbf.write(reinterpret_cast<const char*>(&del), 1);

        for (size_t i = 0; i < fields.size(); i++) {
            const auto& f = fields[i];
            std::string s;
            if (f.type == 'C') {
                std::string val;
                if (i < attrs.size()) {
                    if (auto* sv = std::get_if<std::string>(&attrs[i])) val = *sv;
                    else if (auto* iv = std::get_if<int64_t>(&attrs[i])) val = std::to_string(*iv);
                    else if (auto* dv = std::get_if<double>(&attrs[i])) val = std::format("{}", *dv);
                }
                s = val;
                s.resize(f.size, ' ');
            } else if (f.type == 'N') {
                if (f.dec > 0) {
                    double val = 0;
                    if (i < attrs.size()) {
                        if (auto* dv = std::get_if<double>(&attrs[i])) val = *dv;
                        else if (auto* iv = std::get_if<int64_t>(&attrs[i])) val = static_cast<double>(*iv);
                    }
                    s = std::format("{:>{}.{}f}", val, f.size, f.dec);
                } else {
                    int64_t val = 0;
                    if (i < attrs.size()) {
                        if (auto* iv = std::get_if<int64_t>(&attrs[i])) val = *iv;
                        else if (auto* dv = std::get_if<double>(&attrs[i])) val = static_cast<int64_t>(*dv);
                    }
                    s = std::format("{:>{}d}", val, f.size);
                }
                if (s.size() > f.size)
                    s = std::string(f.size, '*');
            } else if (f.type == 'F') {
                double val = 0;
                if (i < attrs.size()) {
                    if (auto* dv = std::get_if<double>(&attrs[i])) val = *dv;
                    else if (auto* iv = std::get_if<int64_t>(&attrs[i])) val = static_cast<double>(*iv);
                }
                s = std::format("{:>{}.{}f}", val, f.size, f.dec);
                if (s.size() > f.size)
                    s = std::string(f.size, '*');
            } else {
                s.resize(f.size, ' ');
            }
            if (s.size() > f.size) s.resize(f.size);
            dbf.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
    }

    void write_poly_shape(const std::vector<std::vector<Point>>& parts,
                          const std::vector<AttrValue>& attrs) {
        int total_points = 0;
        double x_min = DBL_MAX, y_min = DBL_MAX, x_max = -DBL_MAX, y_max = -DBL_MAX;
        for (const auto& part : parts) {
            total_points += static_cast<int>(part.size());
            for (const auto& p : part) {
                if (p.x < x_min) x_min = p.x;
                if (p.x > x_max) x_max = p.x;
                if (p.y < y_min) y_min = p.y;
                if (p.y > y_max) y_max = p.y;
            }
        }

        if (!bbox_init) {
            bbox[0] = x_min; bbox[1] = y_min; bbox[2] = x_max; bbox[3] = y_max;
            bbox_init = true;
        } else {
            if (x_min < bbox[0]) bbox[0] = x_min;
            if (y_min < bbox[1]) bbox[1] = y_min;
            if (x_max > bbox[2]) bbox[2] = x_max;
            if (y_max > bbox[3]) bbox[3] = y_max;
        }

        int num_parts = static_cast<int>(parts.size());
        int content_bytes = 4 + 32 + 4 + 4 + num_parts * 4 + total_points * 16;
        int content_words = content_bytes / 2;

        rec_num++;

        // SHX index entry (big-endian)
        std::array<uint8_t, 8> shx_buf{};
        put_be32(shx_buf.data(), static_cast<uint32_t>(shp_offset));
        put_be32(shx_buf.data() + 4, static_cast<uint32_t>(content_words));
        shx.write(reinterpret_cast<const char*>(shx_buf.data()), 8);

        // SHP record header (big-endian)
        std::array<uint8_t, 8> rec_hdr{};
        put_be32(rec_hdr.data(), static_cast<uint32_t>(rec_num));
        put_be32(rec_hdr.data() + 4, static_cast<uint32_t>(content_words));
        shp.write(reinterpret_cast<const char*>(rec_hdr.data()), 8);

        // Shape type (little-endian)
        write_le32(shp, static_cast<int32_t>(shape_type));
        // Bounding box
        write_le_f64(shp, x_min); write_le_f64(shp, y_min);
        write_le_f64(shp, x_max); write_le_f64(shp, y_max);
        // numParts, numPoints
        write_le32(shp, num_parts); write_le32(shp, total_points);
        // Part start indices
        int32_t idx = 0;
        for (const auto& part : parts) {
            write_le32(shp, idx);
            idx += static_cast<int32_t>(part.size());
        }
        // Points
        for (const auto& part : parts)
            for (const auto& p : part) {
                write_le_f64(shp, p.x);
                write_le_f64(shp, p.y);
            }

        shp_offset += 4 + content_words;
        write_dbf_record(attrs);
    }
};

std::unique_ptr<Writer> Writer::create(const std::string& base_path,
                                        ShapeType shape_type,
                                        const std::vector<Field>& fields) {
    auto w = std::unique_ptr<Writer>(new Writer());
    w->impl_ = std::make_unique<Impl>();
    auto& impl = *w->impl_;
    impl.shape_type = shape_type;
    impl.fields = fields;

    impl.shp.open(base_path + ".shp", std::ios::binary);
    impl.shx.open(base_path + ".shx", std::ios::binary);
    impl.dbf.open(base_path + ".dbf", std::ios::binary);
    if (!impl.shp || !impl.shx || !impl.dbf)
        throw std::runtime_error("shp: failed to create shapefile set");

    // Write .cpg
    {
        std::ofstream cpg(base_path + ".cpg");
        cpg << "UTF-8";
    }

    impl.write_file_header(impl.shp);
    impl.write_file_header(impl.shx);
    impl.write_dbf_header();

    return w;
}

Writer::~Writer() {
    if (impl_) {
        try { close(); } catch (...) {}
    }
}

void Writer::write_poly_line(const std::vector<std::vector<Point>>& parts,
                              const std::vector<AttrValue>& attrs) {
    if (impl_->shape_type != ShapeType::poly_line)
        throw std::runtime_error("shp: write_poly_line called on non-polyline writer");
    impl_->write_poly_shape(parts, attrs);
}

void Writer::write_polygon(const std::vector<std::vector<Point>>& rings,
                            const std::vector<AttrValue>& attrs) {
    if (impl_->shape_type != ShapeType::polygon)
        throw std::runtime_error("shp: write_polygon called on non-polygon writer");
    impl_->write_poly_shape(rings, attrs);
}

void Writer::close() {
    if (!impl_) return;
    auto& impl = *impl_;

    // Update SHP header
    impl.shp.seekp(0);
    impl.write_updated_header(impl.shp, impl.shp_offset);

    // Update SHX header
    int shx_len = 50 + impl.rec_num * 4;
    impl.shx.seekp(0);
    impl.write_updated_header(impl.shx, shx_len);

    // Update DBF record count
    impl.dbf.seekp(4);
    uint32_t count = static_cast<uint32_t>(impl.rec_num);
    impl.dbf.write(reinterpret_cast<const char*>(&count), 4);

    impl.shp.close();
    impl.shx.close();
    impl.dbf.close();
    impl_.reset();
}

// --- Reader ---

static Record parse_record(const uint8_t* data, size_t len) {
    if (len < 4) throw std::runtime_error("shp: record too short");
    auto st = static_cast<ShapeType>(get_le32(data));
    if (st != ShapeType::poly_line && st != ShapeType::polygon)
        return Record{st, {}, {}, {}};
    if (len < 44) throw std::runtime_error("shp: poly record too short");

    Record rec;
    rec.type = st;
    rec.bbox.x_min = get_le_f64(data + 4);
    rec.bbox.y_min = get_le_f64(data + 12);
    rec.bbox.x_max = get_le_f64(data + 20);
    rec.bbox.y_max = get_le_f64(data + 28);

    int num_parts = static_cast<int>(get_le32(data + 36));
    int num_points = static_cast<int>(get_le32(data + 40));

    std::vector<int> part_starts(static_cast<size_t>(num_parts));
    size_t offset = 44;
    for (size_t i = 0; i < static_cast<size_t>(num_parts); i++) {
        if (offset + 4 > len) throw std::runtime_error("shp: truncated part indices");
        part_starts[i] = static_cast<int>(get_le32(data + offset));
        offset += 4;
    }

    std::vector<Point> all_points(static_cast<size_t>(num_points));
    for (size_t i = 0; i < static_cast<size_t>(num_points); i++) {
        if (offset + 16 > len) throw std::runtime_error("shp: truncated points");
        all_points[i].x = get_le_f64(data + offset);
        all_points[i].y = get_le_f64(data + offset + 8);
        offset += 16;
    }

    rec.parts.resize(static_cast<size_t>(num_parts));
    for (size_t i = 0; i < static_cast<size_t>(num_parts); i++) {
        int start = part_starts[i];
        int end = (i + 1 < static_cast<size_t>(num_parts)) ? part_starts[i + 1] : num_points;
        if (start >= 0 && end <= num_points && start < end)
            rec.parts[i].assign(all_points.begin() + start, all_points.begin() + end);
    }
    return rec;
}

static void parse_dbf(const std::vector<uint8_t>& data, std::vector<Field>& fields,
                       std::vector<std::unordered_map<std::string, std::string>>& records) {
    if (data.size() < 32) return;
    int num_records = static_cast<int>(get_le32(data.data() + 4));
    int header_size = get_le16(data.data() + 8);
    int record_size = get_le16(data.data() + 10);

    size_t pos = 32;
    while (pos < static_cast<size_t>(header_size - 1) && pos + 32 <= data.size()) {
        if (data[pos] == 0x0D) break;
        std::string name(reinterpret_cast<const char*>(data.data() + pos), 11);
        auto nul = name.find('\0');
        if (nul != std::string::npos) name.resize(nul);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        fields.push_back({name, static_cast<char>(data[pos + 11]),
                          data[pos + 16], data[pos + 17]});
        pos += 32;
    }

    pos = static_cast<size_t>(header_size);
    for (int i = 0; i < num_records && pos + static_cast<size_t>(record_size) <= data.size(); i++) {
        const uint8_t* rec_data = data.data() + pos + 1; // skip deletion flag
        std::unordered_map<std::string, std::string> attrs;
        int f_offset = 0;
        for (const auto& f : fields) {
            if (f_offset + f.size > record_size - 1) break;
            std::string val(reinterpret_cast<const char*>(rec_data + f_offset), f.size);
            // Trim spaces
            auto start = val.find_first_not_of(' ');
            auto end = val.find_last_not_of(' ');
            if (start != std::string::npos)
                attrs[f.name] = val.substr(start, end - start + 1);
            else
                attrs[f.name] = "";
            f_offset += f.size;
        }
        records.push_back(std::move(attrs));
        pos += static_cast<size_t>(record_size);
    }
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error(std::format("shp: cannot open {}", path));
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

File open(const std::string& base_path) {
    std::string base = base_path;
    for (const auto& ext : {".shp", ".shx", ".dbf", ".SHP", ".SHX", ".DBF"}) {
        if (base.size() > 4 && base.substr(base.size() - 4) == std::string(ext).substr(0, 4)) {
            base.resize(base.size() - 4);
            break;
        }
    }

    auto shp_data = read_file(base + ".shp");
    if (shp_data.size() < 100)
        throw std::runtime_error(std::format("shp: file too short ({} bytes)", shp_data.size()));

    uint32_t file_code = get_be32(shp_data.data());
    if (file_code != 9994)
        throw std::runtime_error(std::format("shp: bad file code {}", file_code));

    int file_len = static_cast<int>(get_be32(shp_data.data() + 24)) * 2;
    auto shape_type = static_cast<ShapeType>(get_le32(shp_data.data() + 32));

    File result;
    result.type = shape_type;
    result.bbox.x_min = get_le_f64(shp_data.data() + 36);
    result.bbox.y_min = get_le_f64(shp_data.data() + 44);
    result.bbox.x_max = get_le_f64(shp_data.data() + 52);
    result.bbox.y_max = get_le_f64(shp_data.data() + 60);

    // Parse records
    int pos = 100;
    while (pos < file_len && pos < static_cast<int>(shp_data.size())) {
        if (pos + 8 > static_cast<int>(shp_data.size())) break;
        int content_len = static_cast<int>(get_be32(shp_data.data() + pos + 4)) * 2;
        pos += 8;
        if (pos + content_len > static_cast<int>(shp_data.size())) break;
        result.records.push_back(parse_record(shp_data.data() + pos,
                                              static_cast<size_t>(content_len)));
        pos += content_len;
    }

    // Parse DBF
    try {
        auto dbf_data = read_file(base + ".dbf");
        std::vector<std::unordered_map<std::string, std::string>> dbf_records;
        parse_dbf(dbf_data, result.fields, dbf_records);
        for (size_t i = 0; i < result.records.size() && i < dbf_records.size(); i++)
            result.records[i].attrs = std::move(dbf_records[i]);
    } catch (...) {
        // DBF is optional
    }

    return result;
}

BBox read_bbox(const std::string& shp_path) {
    std::ifstream f(shp_path, std::ios::binary);
    if (!f) throw std::runtime_error(std::format("shp: cannot open {}", shp_path));
    std::array<uint8_t, 100> hdr{};
    f.read(reinterpret_cast<char*>(hdr.data()), 100);
    if (!f) throw std::runtime_error("shp: reading header");
    if (get_be32(hdr.data()) != 9994)
        throw std::runtime_error(std::format("shp: bad file code {}", get_be32(hdr.data())));
    return {get_le_f64(hdr.data() + 36), get_le_f64(hdr.data() + 44),
            get_le_f64(hdr.data() + 52), get_le_f64(hdr.data() + 60)};
}

std::vector<std::vector<std::array<double, 2>>> File::polylines() const {
    std::vector<std::vector<std::array<double, 2>>> result;
    for (const auto& rec : records) {
        for (const auto& part : rec.parts) {
            if (part.size() < 2) continue;
            std::vector<std::array<double, 2>> points;
            points.reserve(part.size());
            for (const auto& p : part) points.push_back({p.x, p.y});
            result.push_back(std::move(points));
        }
    }
    return result;
}

double attr_float64(const std::unordered_map<std::string, std::string>& attrs,
                    const std::string& key) {
    auto it = attrs.find(key);
    if (it == attrs.end()) return 0;
    try { return std::stod(it->second); } catch (...) { return 0; }
}

int attr_int(const std::unordered_map<std::string, std::string>& attrs,
             const std::string& key) {
    auto it = attrs.find(key);
    if (it == attrs.end()) return 0;
    try { return std::stoi(it->second); } catch (...) { return 0; }
}

} // namespace armatools::shp
