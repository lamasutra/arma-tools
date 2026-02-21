#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace armatools::config {

struct StringEntry { std::string value; };
struct FloatEntry  { float value; };
struct IntEntry    { int32_t value; };

struct StringElement { std::string value; };
struct FloatElement  { float value; };
struct IntElement    { int32_t value; };

struct ArrayEntry;
struct NestedArray { ArrayEntry* array; }; // owned via unique_ptr in practice

using ArrayElement = std::variant<StringElement, FloatElement, IntElement, struct NestedArrayEntry>;

struct NestedArrayEntry {
    bool expansion = false;
    std::vector<ArrayElement> elements;
};

struct ArrayEntry {
    bool expansion = false;
    std::vector<ArrayElement> elements;
};

struct ConfigClass;

struct ClassEntry { ConfigClass* cls; }; // owned

using Entry = std::variant<StringEntry, FloatEntry, IntEntry, ArrayEntry, struct ClassEntryOwned>;

struct ClassEntryOwned {
    std::unique_ptr<ConfigClass> cls;
};

struct NamedEntry {
    std::string name;
    Entry entry;
};

struct ConfigClass {
    std::string parent;
    bool external = false;
    bool deletion = false;
    std::vector<NamedEntry> entries;
};

struct Config {
    ConfigClass root;
};

// read parses a rapified config from a seekable stream.
Config read(std::istream& r);

// parse_text parses a plaintext (derap'd) config.cpp/hpp file.
Config parse_text(std::istream& r);

// write_text writes the config as human-readable text.
void write_text(std::ostream& w, const Config& cfg);

} // namespace armatools::config
