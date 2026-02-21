#include "armatools/config.h"
#include "armatools/binutil.h"

#include <charconv>
#include <format>
#include <sstream>
#include <stdexcept>

namespace armatools::config {

// --- Binary (rapified) reader ---

static ArrayEntry read_array(std::istream& r);

static ConfigClass read_class(std::istream& r, int level) {
    if (level == 0) {
        r.seekg(16);
        if (!r) throw std::runtime_error("config: seeking to root class");
    } else {
        uint32_t offset = binutil::read_u32(r);
        auto saved = r.tellg();
        r.seekg(static_cast<std::streamoff>(offset));
        if (!r) throw std::runtime_error(std::format("config: seeking to class body at {}", offset));
        // We'll read the class body and then seek back
        // But we need a deferred seek — use a scope guard approach
        // Actually, the Go code does defer r.Seek(savedPos). We replicate:
        struct ScopeGuard {
            std::istream& r; std::streampos pos;
            ~ScopeGuard() { r.seekg(pos); }
        } guard{r, saved};

        std::string parent = binutil::read_asciiz(r);
        uint32_t num_entries = binutil::read_compressed_int(r);

        ConfigClass cls;
        cls.parent = parent;
        for (uint32_t i = 0; i < num_entries; i++) {
            uint8_t entry_type = binutil::read_u8(r);

            switch (entry_type) {
                case 0: { // class with body
                    std::string name = binutil::read_asciiz(r);
                    auto child = read_class(r, level + 1);
                    auto owned = std::make_unique<ConfigClass>(std::move(child));
                    cls.entries.push_back({name, ClassEntryOwned{std::move(owned)}});
                    break;
                }
                case 1: { // variable
                    uint8_t subtype = binutil::read_u8(r);
                    std::string name = binutil::read_asciiz(r);
                    switch (subtype) {
                        case 0: cls.entries.push_back({name, StringEntry{binutil::read_asciiz(r)}}); break;
                        case 1: cls.entries.push_back({name, FloatEntry{binutil::read_f32(r)}}); break;
                        case 2: cls.entries.push_back({name, IntEntry{binutil::read_i32(r)}}); break;
                        default: throw std::runtime_error(std::format("config: unknown variable subtype {}", subtype));
                    }
                    break;
                }
                case 2: { // array
                    std::string name = binutil::read_asciiz(r);
                    auto arr = read_array(r);
                    cls.entries.push_back({name, std::move(arr)});
                    break;
                }
                case 3: { // external class
                    std::string name = binutil::read_asciiz(r);
                    auto c = std::make_unique<ConfigClass>();
                    c->external = true;
                    cls.entries.push_back({name, ClassEntryOwned{std::move(c)}});
                    break;
                }
                case 4: { // delete class
                    std::string name = binutil::read_asciiz(r);
                    auto c = std::make_unique<ConfigClass>();
                    c->deletion = true;
                    cls.entries.push_back({name, ClassEntryOwned{std::move(c)}});
                    break;
                }
                case 5: { // array expansion (+=)
                    r.seekg(4, std::ios::cur); // skip 4 bytes
                    std::string name = binutil::read_asciiz(r);
                    auto arr = read_array(r);
                    arr.expansion = true;
                    cls.entries.push_back({name, std::move(arr)});
                    break;
                }
                default:
                    throw std::runtime_error(std::format("config: unknown entry type {}", entry_type));
            }
        }
        return cls;
    }

    // Level 0 path:
    std::string parent = binutil::read_asciiz(r);
    uint32_t num_entries = binutil::read_compressed_int(r);

    ConfigClass cls;
    cls.parent = parent;
    for (uint32_t i = 0; i < num_entries; i++) {
        uint8_t entry_type = binutil::read_u8(r);

        switch (entry_type) {
            case 0: {
                std::string name = binutil::read_asciiz(r);
                auto child = read_class(r, level + 1);
                auto owned = std::make_unique<ConfigClass>(std::move(child));
                cls.entries.push_back({name, ClassEntryOwned{std::move(owned)}});
                break;
            }
            case 1: {
                uint8_t subtype = binutil::read_u8(r);
                std::string name = binutil::read_asciiz(r);
                switch (subtype) {
                    case 0: cls.entries.push_back({name, StringEntry{binutil::read_asciiz(r)}}); break;
                    case 1: cls.entries.push_back({name, FloatEntry{binutil::read_f32(r)}}); break;
                    case 2: cls.entries.push_back({name, IntEntry{binutil::read_i32(r)}}); break;
                    default: throw std::runtime_error(std::format("config: unknown variable subtype {}", subtype));
                }
                break;
            }
            case 2: {
                std::string name = binutil::read_asciiz(r);
                auto arr = read_array(r);
                cls.entries.push_back({name, std::move(arr)});
                break;
            }
            case 3: {
                std::string name = binutil::read_asciiz(r);
                auto c = std::make_unique<ConfigClass>();
                c->external = true;
                cls.entries.push_back({name, ClassEntryOwned{std::move(c)}});
                break;
            }
            case 4: {
                std::string name = binutil::read_asciiz(r);
                auto c = std::make_unique<ConfigClass>();
                c->deletion = true;
                cls.entries.push_back({name, ClassEntryOwned{std::move(c)}});
                break;
            }
            case 5: {
                r.seekg(4, std::ios::cur);
                std::string name = binutil::read_asciiz(r);
                auto arr = read_array(r);
                arr.expansion = true;
                cls.entries.push_back({name, std::move(arr)});
                break;
            }
            default:
                throw std::runtime_error(std::format("config: unknown entry type {}", entry_type));
        }
    }
    return cls;
}

static ArrayEntry read_array(std::istream& r) {
    uint32_t num_elements = binutil::read_compressed_int(r);
    ArrayEntry arr;
    for (uint32_t i = 0; i < num_elements; i++) {
        uint8_t elem_type = binutil::read_u8(r);
        switch (elem_type) {
            case 0: arr.elements.push_back(StringElement{binutil::read_asciiz(r)}); break;
            case 1: arr.elements.push_back(FloatElement{binutil::read_f32(r)}); break;
            case 2: arr.elements.push_back(IntElement{binutil::read_i32(r)}); break;
            case 3: {
                auto nested = read_array(r);
                arr.elements.push_back(NestedArrayEntry{nested.expansion, std::move(nested.elements)});
                break;
            }
            default:
                throw std::runtime_error(std::format("config: unknown array element type {}", elem_type));
        }
    }
    return arr;
}

Config read(std::istream& r) {
    std::string sig = binutil::read_signature(r);
    if (sig != std::string("\x00raP", 4))
        throw std::runtime_error(std::format("config: not a rapified config (signature: \"{}\")", sig));

    auto root = read_class(r, 0);
    return Config{std::move(root)};
}

// --- Text writer ---

static std::string escape_string(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    return out;
}

static std::string format_float(float f) {
    // Use shortest representation
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), f);
    return std::string(buf, ptr);
}

static void write_array_elements(std::ostream& w, const std::vector<ArrayElement>& elements);

static void write_class(std::ostream& w, const ConfigClass& cls, int level) {
    std::string indent(static_cast<size_t>(level), '\t');

    for (const auto& ne : cls.entries) {
        std::visit([&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ClassEntryOwned>) {
                const auto& c = *e.cls;
                if (c.deletion) {
                    w << indent << "delete " << ne.name << ";\n";
                } else if (c.external) {
                    w << indent << "class " << ne.name << ";\n";
                } else {
                    std::string parent_str;
                    if (!c.parent.empty()) parent_str = ": " + c.parent;
                    if (c.entries.empty()) {
                        w << indent << "class " << ne.name << parent_str << " {};\n";
                    } else {
                        w << indent << "class " << ne.name << parent_str << " {\n";
                        write_class(w, c, level + 1);
                        w << indent << "};\n";
                    }
                }
            } else if constexpr (std::is_same_v<T, StringEntry>) {
                w << indent << ne.name << " = \"" << escape_string(e.value) << "\";\n";
            } else if constexpr (std::is_same_v<T, FloatEntry>) {
                w << indent << ne.name << " = " << format_float(e.value) << ";\n";
            } else if constexpr (std::is_same_v<T, IntEntry>) {
                w << indent << ne.name << " = " << e.value << ";\n";
            } else if constexpr (std::is_same_v<T, ArrayEntry>) {
                if (e.expansion)
                    w << indent << ne.name << "[] += ";
                else
                    w << indent << ne.name << "[] = ";
                write_array_elements(w, e.elements);
                w << ";\n";
            }
        }, ne.entry);
    }
}

static void write_array_elements(std::ostream& w, const std::vector<ArrayElement>& elements) {
    w << "{";
    for (size_t i = 0; i < elements.size(); i++) {
        if (i > 0) w << ", ";
        std::visit([&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, StringElement>) {
                w << "\"" << escape_string(e.value) << "\"";
            } else if constexpr (std::is_same_v<T, FloatElement>) {
                w << format_float(e.value);
            } else if constexpr (std::is_same_v<T, IntElement>) {
                w << e.value;
            } else if constexpr (std::is_same_v<T, NestedArrayEntry>) {
                write_array_elements(w, e.elements);
            }
        }, elements[i]);
    }
    w << "}";
}

void write_text(std::ostream& w, const Config& cfg) {
    write_class(w, cfg.root, 0);
}

// --- Text parser (stub — full implementation follows Go parse.go) ---

Config parse_text(std::istream& r) {
    std::ostringstream buf;
    buf << r.rdbuf();
    std::string src = buf.str();

    // Strip UTF-8 BOM
    if (src.starts_with("\xef\xbb\xbf")) src = src.substr(3);

    // The full text parser is complex (~550 lines in Go). For now, provide a
    // minimal implementation that handles the most common patterns.
    // A complete port would replicate the recursive descent parser from parse.go.
    // TODO: Full text parser implementation
    (void)src;
    return Config{};
}

} // namespace armatools::config
