// p3d_odol2mlod - ODOL to MLOD converter for Arma 3 and OFP/CWA
// C++17 port of the C# BisDll library by T_D
// Converts binarized P3D models (ODOL format) to editable MLOD format.
//
// Arma:  Based on https://github.com/Mekz0/P3D-Debinarizer-Arma-3
// OFP:   Based on https://github.com/Faguss/odol2mlod
// Build: cmake (linked against armatools::lzss, armatools::lzo)
// Usage: ./p3d_odol2mlod path/model.p3d
//        ./p3d_odol2mlod inputFolder [outputFolder]

#include "armatools/lzss.h"
#include "armatools/lzo.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// BisDll.Common.Math
// ============================================================================
namespace BisDll { namespace Common { namespace Math {

class Vector3P {
public:
    float xyz[3];

    Vector3P() : xyz{0.f, 0.f, 0.f} {}
    explicit Vector3P(float val) : xyz{val, val, val} {}
    Vector3P(float x, float y, float z) : xyz{x, y, z} {}

    float& X() { return xyz[0]; }
    float& Y() { return xyz[1]; }
    float& Z() { return xyz[2]; }
    float X() const { return xyz[0]; }
    float Y() const { return xyz[1]; }
    float Z() const { return xyz[2]; }

    float& operator[](int i) { return xyz[i]; }
    float operator[](int i) const { return xyz[i]; }

    double Length() const {
        return std::sqrt((double)(xyz[0]*xyz[0] + xyz[1]*xyz[1] + xyz[2]*xyz[2]));
    }

    void Normalize() {
        float len = (float)Length();
        if (len > 0.f) { xyz[0] /= len; xyz[1] /= len; xyz[2] /= len; }
    }

    float Distance(const Vector3P& v) const {
        Vector3P d = *this - v;
        return (float)std::sqrt(d.X()*d.X() + d.Y()*d.Y() + d.Z()*d.Z());
    }

    Vector3P operator-() const { return Vector3P(-xyz[0], -xyz[1], -xyz[2]); }
    Vector3P operator+(const Vector3P& b) const { return Vector3P(xyz[0]+b.xyz[0], xyz[1]+b.xyz[1], xyz[2]+b.xyz[2]); }
    Vector3P operator-(const Vector3P& b) const { return Vector3P(xyz[0]-b.xyz[0], xyz[1]-b.xyz[1], xyz[2]-b.xyz[2]); }
    Vector3P operator*(float b) const { return Vector3P(xyz[0]*b, xyz[1]*b, xyz[2]*b); }
    // dot product
    float dot(const Vector3P& b) const { return xyz[0]*b.xyz[0] + xyz[1]*b.xyz[1] + xyz[2]*b.xyz[2]; }

    bool Equals(const Vector3P& other, float tol = 0.05f) const {
        return std::fabs(xyz[0]-other.xyz[0]) < tol &&
               std::fabs(xyz[1]-other.xyz[1]) < tol &&
               std::fabs(xyz[2]-other.xyz[2]) < tol;
    }
};

class Matrix3P {
public:
    Vector3P columns[3]; // Aside, Up, Dir

    Matrix3P() = default;
    explicit Matrix3P(float val) : columns{Vector3P(val), Vector3P(val), Vector3P(val)} {}
    Matrix3P(const Vector3P& aside, const Vector3P& up, const Vector3P& dir) : columns{aside, up, dir} {}

    Vector3P& Aside() { return columns[0]; }
    Vector3P& Up() { return columns[1]; }
    Vector3P& Dir() { return columns[2]; }
    const Vector3P& Aside() const { return columns[0]; }
    const Vector3P& Up() const { return columns[1]; }
    const Vector3P& Dir() const { return columns[2]; }

    const Vector3P& operator[](int col) const { return columns[col]; }
    Vector3P& operator[](int col) { return columns[col]; }

    float get(int row, int col) const { return columns[col][row]; }
    void set(int row, int col, float v) { columns[col][row] = v; }
};

class Matrix4P {
public:
    Matrix3P orientation;
    Vector3P position;

    Matrix4P() = default;
    explicit Matrix4P(float val) : orientation(val), position(val) {}
    Matrix4P(const Matrix3P& o, const Vector3P& p) : orientation(o), position(p) {}

    const Matrix3P& Orientation() const { return orientation; }
    const Vector3P& Position() const { return position; }
    Matrix3P& Orientation() { return orientation; }
    Vector3P& Position() { return position; }

    float get(int row, int col) const {
        if (col != 3) return orientation[col][row];
        return position[row];
    }
    void set(int row, int col, float v) {
        if (col == 3) position[row] = v;
        else orientation[col][row] = v;
    }
};

}}} // BisDll::Common::Math

using Vec3 = BisDll::Common::Math::Vector3P;
using Mat3 = BisDll::Common::Math::Matrix3P;
using Mat4 = BisDll::Common::Math::Matrix4P;

// ============================================================================
// BisDll.Common
// ============================================================================
namespace BisDll { namespace Common {

struct ColorP {
    float Red = 0.f, Green = 0.f, Blue = 0.f, Alpha = 0.f;
};

struct PackedColor {
    uint32_t value = 0;
    PackedColor() = default;
    explicit PackedColor(uint32_t v) : value(v) {}
};

}} // BisDll::Common

// ============================================================================
// Forward declarations
// ============================================================================
class BinaryReaderEx;
class BinaryWriterEx;

// Compression now provided by armatools::lzss and armatools::lzo libraries

// ============================================================================
// BisDll.Stream - BinaryReaderEx
// ============================================================================
class BinaryReaderEx {
    std::istream& stream_;
    bool ownsStream_ = false;
    std::unique_ptr<std::istream> ownedStream_;

public:
    bool UseCompressionFlag = false;
    bool UseLZOCompression = false;
    int Version = 0;

    explicit BinaryReaderEx(std::istream& s) : stream_(s) {}
    // take ownership
    BinaryReaderEx(std::unique_ptr<std::istream> s)
        : stream_(*s), ownsStream_(true), ownedStream_(std::move(s)) {}

    int64_t Position() const {
        return const_cast<std::istream&>(stream_).tellg();
    }
    void SetPosition(int64_t pos) {
        stream_.seekg(pos);
    }

    void Close() {
        // no-op in C++ (RAII handles it)
    }

    uint8_t ReadByte() {
        uint8_t b;
        stream_.read(reinterpret_cast<char*>(&b), 1);
        return b;
    }

    int8_t ReadSByte() {
        int8_t b;
        stream_.read(reinterpret_cast<char*>(&b), 1);
        return b;
    }

    bool ReadBoolean() {
        return ReadByte() != 0;
    }

    uint16_t ReadUInt16() {
        uint16_t v;
        stream_.read(reinterpret_cast<char*>(&v), 2);
        return v;
    }

    int16_t ReadInt16() {
        int16_t v;
        stream_.read(reinterpret_cast<char*>(&v), 2);
        return v;
    }

    int32_t ReadInt32() {
        int32_t v;
        stream_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }

    uint32_t ReadUInt32() {
        uint32_t v;
        stream_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }

    float ReadSingle() {
        float v;
        stream_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }

    std::vector<uint8_t> ReadBytes(int count) {
        std::vector<uint8_t> buf(static_cast<size_t>(count));
        stream_.read(reinterpret_cast<char*>(buf.data()), count);
        return buf;
    }

    uint32_t ReadUInt24() {
        uint8_t b0 = ReadByte(), b1 = ReadByte(), b2 = ReadByte();
        return (uint32_t)b0 + ((uint32_t)b1 << 8) + ((uint32_t)b2 << 16);
    }

    std::string ReadAscii(int count) {
        std::string s;
        s.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; i++) s += (char)ReadByte();
        return s;
    }

    std::string ReadAsciiz() {
        std::string s;
        char c;
        while ((c = (char)ReadByte()) != 0) s += c;
        return s;
    }

    Vec3 ReadVector3P() {
        float x = ReadSingle(), y = ReadSingle(), z = ReadSingle();
        return Vec3(x, y, z);
    }

    // Read compressed data (LZO or LZSS depending on flags)
    std::vector<uint8_t> ReadCompressed(uint32_t expectedSize) {
        if (expectedSize == 0) return {};
        if (UseLZOCompression) return ReadLZO(expectedSize);
        return ReadLZSS(expectedSize, false);
    }

    std::vector<uint8_t> ReadLZO(uint32_t expectedSize) {
        bool compressed = (expectedSize >= 1024);
        if (UseCompressionFlag) compressed = ReadBoolean();
        if (!compressed) return ReadBytes((int)expectedSize);
        return armatools::lzo::decompress(stream_, expectedSize);
    }

    std::vector<uint8_t> ReadLZSS(uint32_t expectedSize, bool inPAA = false) {
        if (expectedSize < 1024 && !inPAA) return ReadBytes((int)expectedSize);
        return armatools::lzss::decompress(stream_, expectedSize);
    }

    // Read array with count prefix
    std::vector<int32_t> ReadIntArray() {
        int32_t n = ReadInt32();
        std::vector<int32_t> arr(static_cast<size_t>(n));
        for (size_t i = 0; i < static_cast<size_t>(n); i++) arr[i] = ReadInt32();
        return arr;
    }

    std::vector<float> ReadFloatArray() {
        int32_t n = ReadInt32();
        std::vector<float> arr(static_cast<size_t>(n));
        for (size_t i = 0; i < static_cast<size_t>(n); i++) arr[i] = ReadSingle();
        return arr;
    }

    std::vector<std::string> ReadStringArray() {
        int32_t n = ReadInt32();
        std::vector<std::string> arr(static_cast<size_t>(n));
        for (size_t i = 0; i < static_cast<size_t>(n); i++) arr[i] = ReadAsciiz();
        return arr;
    }

    // Generic compressed array read
    template<typename T>
    std::vector<T> ReadCompressedArray(std::function<T(BinaryReaderEx&)> readElement, int elemSize) {
        int32_t n = ReadInt32();
        uint32_t expectedSize = (uint32_t)(n * elemSize);
        auto data = ReadCompressed(expectedSize);
        auto ms = std::make_unique<std::istringstream>(
            std::string(reinterpret_cast<char*>(data.data()), data.size()));
        BinaryReaderEx sub(std::move(ms));
        sub.Version = this->Version;
        std::vector<T> result(static_cast<size_t>(n));
        for (size_t i = 0; i < static_cast<size_t>(n); i++) result[i] = readElement(sub);
        return result;
    }

    std::vector<int32_t> ReadCompressedIntArray() {
        return ReadCompressedArray<int32_t>([](BinaryReaderEx& r) { return r.ReadInt32(); }, 4);
    }

    std::vector<float> ReadCompressedFloatArray() {
        return ReadCompressedArray<float>([](BinaryReaderEx& r) { return r.ReadSingle(); }, 4);
    }

    // Condensed array: may be default-filled or compressed
    template<typename T>
    std::vector<T> ReadCondensedArray(std::function<T(BinaryReaderEx&)> readElement, int sizeOfT) {
        int32_t n = ReadInt32();
        std::vector<T> arr(static_cast<size_t>(n));
        if (ReadBoolean()) {
            // default fill
            T val = readElement(*this);
            for (size_t i = 0; i < static_cast<size_t>(n); i++) arr[i] = val;
            return arr;
        }
        uint32_t expectedSize = (uint32_t)(n * sizeOfT);
        auto data = ReadCompressed(expectedSize);
        auto ms = std::make_unique<std::istringstream>(
            std::string(reinterpret_cast<char*>(data.data()), data.size()));
        BinaryReaderEx sub(std::move(ms));
        sub.Version = this->Version;
        for (size_t i = 0; i < static_cast<size_t>(n); i++) arr[i] = readElement(sub);
        return arr;
    }

    std::vector<int32_t> ReadCondensedIntArray() {
        return ReadCondensedArray<int32_t>([](BinaryReaderEx& r) { return r.ReadInt32(); }, 4);
    }

    int ReadCompactInteger() {
        int v = ReadByte();
        if ((v & 0x80) != 0) {
            int v2 = ReadByte();
            v += (v2 - 1) * 128;
        }
        return v;
    }

    // VertexIndex reading (version dependent)
    int32_t ReadVertexIndex() {
        if (Version >= 69) return ReadInt32();
        uint16_t v = ReadUInt16();
        return (v == 0xFFFF) ? -1 : (int32_t)v;
    }

    std::vector<int32_t> ReadCompressedVertexIndexArray() {
        if (Version >= 69) {
            return ReadCompressedArray<int32_t>(
                [](BinaryReaderEx& r) { return r.ReadInt32(); }, 4);
        }
        auto shorts = ReadCompressedArray<int32_t>(
            [](BinaryReaderEx& r) {
                uint16_t v = r.ReadUInt16();
                return (v == 0xFFFF) ? -1 : (int32_t)v;
            }, 2);
        return shorts;
    }

    // Direct stream access for compression
    std::istream& GetStream() { return stream_; }

private:
    // Helper to construct a sub-reader from a vector of bytes
    static std::unique_ptr<std::istringstream> makeStream(const std::vector<uint8_t>& data) {
        return std::make_unique<std::istringstream>(
            std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    }
};

// ============================================================================
// BisDll.Stream - BinaryWriter
// ============================================================================
class BinaryWriterEx {
    std::ostream& stream_;

public:
    explicit BinaryWriterEx(std::ostream& s) : stream_(s) {}

    int64_t Position() const { return const_cast<std::ostream&>(stream_).tellp(); }
    void SetPosition(int64_t p) { stream_.seekp(p); }

    void Write(uint8_t v) { stream_.write(reinterpret_cast<const char*>(&v), 1); }
    void Write(int8_t v) { stream_.write(reinterpret_cast<const char*>(&v), 1); }
    void Write(bool v) { uint8_t b = v ? 1 : 0; Write(b); }
    void Write(int16_t v) { stream_.write(reinterpret_cast<const char*>(&v), 2); }
    void Write(uint16_t v) { stream_.write(reinterpret_cast<const char*>(&v), 2); }
    void Write(int32_t v) { stream_.write(reinterpret_cast<const char*>(&v), 4); }
    void Write(uint32_t v) { stream_.write(reinterpret_cast<const char*>(&v), 4); }
    void Write(float v) { stream_.write(reinterpret_cast<const char*>(&v), 4); }

    void writeAscii(const std::string& text, uint32_t len) {
        stream_.write(text.c_str(), static_cast<std::streamsize>(std::min((size_t)text.size(), (size_t)len)));
        uint32_t pad = len - (uint32_t)std::min((size_t)text.size(), (size_t)len);
        for (uint32_t i = 0; i < pad; i++) {
            char z = 0;
            stream_.write(&z, 1);
        }
    }

    void writeAsciiz(const std::string& text) {
        stream_.write(text.c_str(), static_cast<std::streamsize>(text.size()));
        char z = 0;
        stream_.write(&z, 1);
    }

    void writeVec3(const Vec3& v) {
        Write(v.X());
        Write(v.Y());
        Write(v.Z());
    }
};

// ============================================================================
// BisDll.Model - Enums and flags
// ============================================================================
namespace BisDll { namespace Model {

enum class FaceFlags : int32_t {
    DEFAULT = 0,
    SHADOW_OFF = 0x10,
    MERGING_OFF = 0x1000000,
    ZBIAS_LOW = 0x100,
    ZBIAS_MID = 0x200,
    ZBIAS_HIGH = 0x300,
    LIGHTNING_BOTH = 0x20,
    LIGHTNING_POSITION = 0x80,
    LIGHTNING_FLAT = 0x200000,
    LIGHTNING_REVERSED = 0x100000
};

enum class PointFlags : uint32_t {
    NONE = 0u,
    ONLAND = 1u,
    UNDERLAND = 2u,
    ABOVELAND = 4u,
    KEEPLAND = 8u,
    LAND_MASK = 0xFu,
    DECAL = 0x100u,
    VDECAL = 0x200u,
    DECAL_MASK = 0x300u,
    NOLIGHT = 0x10u,
    AMBIENT = 0x20u,
    FULLLIGHT = 0x40u,
    HALFLIGHT = 0x80u,
    LIGHT_MASK = 0xF0u,
    NOFOG = 0x1000u,
    SKYFOG = 0x2000u,
    FOG_MASK = 0x3000u,
    USER_MASK = 0xFF0000u,
    USER_STEP = 0x10000u,
    SPECIAL_MASK = 0xF000000u,
    SPECIAL_HIDDEN = 0x1000000u,
    ALL_FLAGS = 0xFFF33FFu
};

inline PointFlags operator|(PointFlags a, PointFlags b) {
    return static_cast<PointFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline PointFlags& operator|=(PointFlags& a, PointFlags b) { a = a | b; return a; }

// ClipFlags
enum class ClipFlags : int32_t {
    ClipNone = 0,
    ClipFront = 1,
    ClipBack = 2,
    ClipLeft = 4,
    ClipRight = 8,
    ClipBottom = 16,
    ClipTop = 32,
    ClipUser0 = 64,
    ClipAll = 63,
    ClipLandMask = 3840,
    ClipLandStep = 256,
    ClipLandNone = 0,
    ClipLandOn = 256,
    ClipLandUnder = 512,
    ClipLandAbove = 1024,
    ClipLandKeep = 2048,
    ClipDecalMask = 12288,
    ClipDecalStep = 4096,
    ClipDecalNone = 0,
    ClipDecalNormal = 4096,
    ClipDecalVertical = 8192,
    ClipFogMask = 49152,
    ClipFogStep = 16384,
    ClipFogNormal = 0,
    ClipFogDisable = 16384,
    ClipFogSky = 32768,
    ClipLightMask = 983040,
    ClipLightStep = 65536,
    ClipLightNormal = 0,
    ClipLightLine = 524288,
    ClipUserMask = 267386880,
    ClipUserStep = 1048576,
    MaxUserValue = 255,
    ClipHints = 268435200
};

inline ClipFlags operator&(ClipFlags a, ClipFlags b) {
    return static_cast<ClipFlags>(static_cast<int32_t>(a) & static_cast<int32_t>(b));
}
inline ClipFlags operator|(ClipFlags a, ClipFlags b) {
    return static_cast<ClipFlags>(static_cast<int32_t>(a) | static_cast<int32_t>(b));
}

// Resolution constants and helpers
namespace Resolution {
    constexpr float GEOMETRY = 1E+13f;
    constexpr float BUOYANCY = 2E+13f;
    constexpr float PHYSX = 4E+13f;
    constexpr float MEMORY = 1E+15f;
    constexpr float LANDCONTACT = 2E+15f;
    constexpr float ROADWAY = 3E+15f;
    constexpr float PATHS = 4E+15f;
    constexpr float HITPOINTS = 5E+15f;
    constexpr float VIEW_GEOMETRY = 6E+15f;
    constexpr float FIRE_GEOMETRY = 7E+15f;
    constexpr float VIEW_GEOMETRY_CARGO = 8E+15f;
    constexpr float VIEW_GEOMETRY_PILOT = 1.3E+16f;
    constexpr float VIEW_GEOMETRY_GUNNER = 1.5E+16f;
    constexpr float VIEW_COMMANDER = 1E+16f;
    constexpr float SUBPARTS = 1.7E+16f;
    constexpr float SHADOWVOLUME_CARGO = 1.8E+16f;
    constexpr float SHADOWVOLUME_PILOT = 1.9E+16f;
    constexpr float SHADOWVOLUME_GUNNER = 2E+16f;
    constexpr float WRECK = 2.1E+16f;
    constexpr float VIEW_GUNNER = 1000.f;
    constexpr float VIEW_PILOT = 1100.f;
    constexpr float VIEW_CARGO = 1200.f;
    constexpr float SHADOWVOLUME_BASE = 10000.f;
    constexpr float SHADOW_MAX = 20000.f;

    inline bool KeepsNamedSelections(float r) {
        return r == MEMORY || r == FIRE_GEOMETRY || r == GEOMETRY ||
               r == VIEW_GEOMETRY || r == VIEW_GEOMETRY_PILOT ||
               r == VIEW_GEOMETRY_GUNNER || r == VIEW_GEOMETRY_CARGO ||
               r == PATHS || r == HITPOINTS || r == PHYSX || r == BUOYANCY;
    }

    inline std::string getLODName(float res) {
        if (res == MEMORY) return "Memory";
        if (res == LANDCONTACT) return "LandContact";
        if (res == ROADWAY) return "Roadway";
        if (res == PATHS) return "Paths";
        if (res == HITPOINTS) return "HitPoints";
        if (res == VIEW_GEOMETRY) return "ViewGeometry";
        if (res == FIRE_GEOMETRY) return "FireGeometry";
        if (res == VIEW_GEOMETRY_CARGO) return "ViewCargoGeometry";
        if (res == VIEW_COMMANDER) return "ViewCommander";
        if (res == VIEW_GEOMETRY_PILOT) return "ViewPilotGeometry";
        if (res == VIEW_GEOMETRY_GUNNER) return "ViewGunnerGeometry";
        if (res == SUBPARTS) return "SubParts";
        if (res == WRECK) return "Wreck";
        if (res == VIEW_GUNNER) return "ViewGunner";
        if (res == VIEW_PILOT) return "ViewPilot";
        if (res == VIEW_CARGO) return "ViewCargo";
        if (res == GEOMETRY) return "Geometry";
        if (res == PHYSX) return "PhysX";
        if (res >= 10000.f && res <= 20000.f) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "ShadowVolume%.3f", res - 10000.f);
            return buf;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f", res);
        return buf;
    }
} // Resolution

}} // BisDll::Model

// ============================================================================
// BisDll.Model.ODOL - Data structures
// ============================================================================
namespace BisDll { namespace Model { namespace ODOL {

struct Proxy {
    std::string proxyModel;
    Mat4 transformation;
    int32_t sequenceID = 0;
    int32_t namedSelectionIndex = 0;
    int32_t boneIndex = 0;
    int32_t sectionIndex = 0;

    void read(BinaryReaderEx& input) {
        proxyModel = input.ReadAsciiz();
        // Read Matrix4P: 3x3 orientation + vec3 position = 12 floats
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
                transformation.orientation[c][r] = input.ReadSingle();
        for (int r = 0; r < 3; r++)
            transformation.position[r] = input.ReadSingle();
        sequenceID = input.ReadInt32();
        namedSelectionIndex = input.ReadInt32();
        boneIndex = input.ReadInt32();
        if (input.Version >= 40) sectionIndex = input.ReadInt32();
    }
};

struct AnimationRTPair {
    uint8_t SelectionIndex;
    uint8_t Weight;
    AnimationRTPair(uint8_t sel = 0, uint8_t w = 0) : SelectionIndex(sel), Weight(w) {}
};

struct VerySmallArray {
    int32_t nSmall = 0;
    uint8_t smallSpace[8] = {};

    void read(BinaryReaderEx& input) {
        nSmall = input.ReadInt32();
        auto bytes = input.ReadBytes(8);
        std::memcpy(smallSpace, bytes.data(), 8);
    }

    std::vector<AnimationRTPair> getAnimationRTPairs() const {
        std::vector<AnimationRTPair> pairs(static_cast<size_t>(nSmall));
        for (size_t i = 0; i < static_cast<size_t>(nSmall); i++) {
            pairs[i] = AnimationRTPair(smallSpace[i*2], smallSpace[i*2+1]);
        }
        return pairs;
    }
};

using AnimationRTWeight = VerySmallArray;

struct VertexNeighborInfo {
    uint16_t PosA = 0;
    AnimationRTWeight RtwA;
    uint16_t PosB = 0;
    AnimationRTWeight RtwB;

    void read(BinaryReaderEx& input) {
        PosA = input.ReadUInt16();
        input.ReadBytes(2); // padding
        RtwA.read(input);
        PosB = input.ReadUInt16();
        input.ReadBytes(2); // padding
        RtwB.read(input);
    }
};

struct SubSkeletonIndexSet {
    std::vector<int32_t> subSkeletons;
    void read(BinaryReaderEx& input) {
        subSkeletons = input.ReadIntArray();
    }
};

struct Polygon {
    std::vector<int32_t> VertexIndices;

    void read(BinaryReaderEx& input) {
        uint8_t n = input.ReadByte();
        VertexIndices.resize(static_cast<size_t>(n));
        for (size_t i = 0; i < static_cast<size_t>(n); i++) {
            VertexIndices[i] = input.ReadVertexIndex();
        }
    }
};

struct Polygons {
    std::vector<Polygon> Faces;

    void read(BinaryReaderEx& input) {
        uint32_t nFaces = input.ReadUInt32();
        input.ReadUInt32(); // skip
        input.ReadUInt16(); // skip
        Faces.resize(nFaces);
        for (uint32_t i = 0; i < nFaces; i++) {
            Faces[i].read(input);
        }
    }
};

struct StageTexture {
    uint32_t textureFilter = 0;
    std::string texture;
    uint32_t stageID = 0;
    bool useWorldEnvMap = false;

    void read(BinaryReaderEx& input, uint32_t matVersion) {
        if (matVersion >= 5) textureFilter = input.ReadUInt32();
        texture = input.ReadAsciiz();
        if (matVersion >= 8) stageID = input.ReadUInt32();
        if (matVersion >= 11) useWorldEnvMap = input.ReadBoolean();
    }
};

struct StageTransform {
    uint32_t uvSource = 0;
    Mat4 transformation;

    void read(BinaryReaderEx& input) {
        uvSource = input.ReadUInt32();
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
                transformation.orientation[c][r] = input.ReadSingle();
        for (int r = 0; r < 3; r++)
            transformation.position[r] = input.ReadSingle();
    }
};

struct EmbeddedMaterial {
    std::string materialName;
    uint32_t version = 0;
    BisDll::Common::ColorP emissive, ambient, diffuse, forcedDiffuse, specular, specularCopy;
    float specularPower = 0.f;
    uint32_t pixelShader = 0;
    uint32_t vertexShader = 0;
    uint32_t mainLight = 0;
    uint32_t fogMode = 0;
    std::string surfaceFile;
    uint32_t nRenderFlags = 0, renderFlags = 0;
    uint32_t nStages = 0, nTexGens = 0;
    std::vector<StageTexture> stageTextures;
    std::vector<StageTransform> stageTransforms;
    StageTexture stageTI;

    void readColorP(BinaryReaderEx& input, BisDll::Common::ColorP& c) {
        c.Red = input.ReadSingle();
        c.Green = input.ReadSingle();
        c.Blue = input.ReadSingle();
        c.Alpha = input.ReadSingle();
    }

    void read(BinaryReaderEx& input) {
        materialName = input.ReadAsciiz();
        version = input.ReadUInt32();
        readColorP(input, emissive);
        readColorP(input, ambient);
        readColorP(input, diffuse);
        readColorP(input, forcedDiffuse);
        readColorP(input, specular);
        readColorP(input, specularCopy);
        specularPower = input.ReadSingle();
        pixelShader = input.ReadUInt32();
        vertexShader = input.ReadUInt32();
        mainLight = input.ReadUInt32();
        fogMode = input.ReadUInt32();
        if (version == 3) input.ReadBoolean();
        if (version >= 6) surfaceFile = input.ReadAsciiz();
        if (version >= 4) { nRenderFlags = input.ReadUInt32(); renderFlags = input.ReadUInt32(); }
        if (version > 6) nStages = input.ReadUInt32();
        if (version > 8) nTexGens = input.ReadUInt32();

        stageTextures.resize(nStages);
        stageTransforms.resize(nTexGens);

        if (version < 8) {
            for (uint32_t i = 0; i < nStages; i++) {
                stageTransforms[i].read(input);
                stageTextures[i].read(input, version);
            }
        } else {
            for (uint32_t i = 0; i < nStages; i++) {
                stageTextures[i].read(input, version);
            }
            for (uint32_t i = 0; i < nTexGens; i++) {
                stageTransforms[i].read(input);
            }
        }
        if (version >= 10) stageTI.read(input, version);
    }
};

struct NamedSelection {
    std::string Name;
    std::vector<int32_t> SelectedFaces;
    bool IsSectional = false;
    std::vector<int32_t> Sections;
    std::vector<int32_t> SelectedVertices;
    std::vector<uint8_t> SelectedVerticesWeights;

    void read(BinaryReaderEx& input) {
        Name = input.ReadAsciiz();
        SelectedFaces = input.ReadCompressedVertexIndexArray();
        input.ReadInt32(); // skip
        IsSectional = input.ReadBoolean();
        Sections = input.ReadCompressedIntArray();
        SelectedVertices = input.ReadCompressedVertexIndexArray();
        int32_t expectedSize = input.ReadInt32();
        SelectedVerticesWeights = input.ReadCompressed((uint32_t)expectedSize);
    }
};

struct Section {
    int32_t faceLowerIndex = 0;
    int32_t faceUpperIndex = 0;
    int32_t minBoneIndex = 0;
    int32_t bonesCount = 0;
    int16_t textureIndex = 0;
    uint32_t special = 0;
    int32_t materialIndex = 0;
    std::string mat;
    uint32_t nStages = 0;
    std::vector<float> areaOverTex;
    bool shortIndices = true;

    std::vector<uint32_t> getFaceIndexes(const std::vector<Polygon>& faces) const {
        uint32_t offset = 0;
        uint32_t step = shortIndices ? 8u : 16u;
        uint32_t extraStep = shortIndices ? 2u : 4u;
        std::vector<uint32_t> result;
        for (uint32_t fi = 0; fi < (uint32_t)faces.size(); fi++) {
            if (offset >= (uint32_t)faceLowerIndex && offset < (uint32_t)faceUpperIndex) {
                result.push_back(fi);
            }
            offset += step;
            if ((int)faces[fi].VertexIndices.size() == 4) {
                offset += extraStep;
            }
            if (offset >= (uint32_t)faceUpperIndex) break;
        }
        return result;
    }

    void read(BinaryReaderEx& input) {
        int version = input.Version;
        shortIndices = version < 69;
        faceLowerIndex = input.ReadInt32();
        faceUpperIndex = input.ReadInt32();
        minBoneIndex = input.ReadInt32();
        bonesCount = input.ReadInt32();
        input.ReadUInt32(); // skip
        textureIndex = input.ReadInt16();
        special = input.ReadUInt32();
        materialIndex = input.ReadInt32();
        if (materialIndex == -1) mat = input.ReadAsciiz();
        if (version >= 36) {
            nStages = input.ReadUInt32();
            areaOverTex.resize(nStages);
            for (uint32_t i = 0; i < nStages; i++) areaOverTex[i] = input.ReadSingle();
            if (version >= 67) {
                int32_t count = input.ReadInt32();
                if (count >= 1) {
                    for (int i = 0; i < 11; i++) input.ReadSingle();
                }
            }
        } else {
            areaOverTex.resize(1);
            areaOverTex[0] = input.ReadSingle();
        }
    }
};

struct Keyframe {
    float time = 0.f;
    std::vector<Vec3> points;

    void read(BinaryReaderEx& input) {
        time = input.ReadSingle();
        uint32_t n = input.ReadUInt32();
        points.resize(n);
        for (uint32_t i = 0; i < n; i++) points[i] = input.ReadVector3P();
    }
};

struct UVSet {
    bool isDiscretized = false;
    float minU = 0.f, minV = 0.f, maxU = 0.f, maxV = 0.f;
    uint32_t nVertices = 0;
    bool defaultFill = false;
    std::vector<uint8_t> defaultValue;
    std::vector<uint8_t> uvData;

    std::vector<float> getUVData() const {
        std::vector<float> arr(nVertices * 2);
        float defU = 0.f, defV = 0.f;
        double scaleU = 1.0, scaleV = 1.0;

        if (isDiscretized) {
            scaleU = (double)(maxU - minU);
            scaleV = (double)(maxV - minV);
        }

        if (defaultFill) {
            if (isDiscretized) {
                int16_t su, sv;
                std::memcpy(&su, defaultValue.data(), 2);
                std::memcpy(&sv, defaultValue.data() + 2, 2);
                defU = (float)(1.52587890625E-05 * (double)(su + 32767) * scaleU) + minU;
                defV = (float)(1.52587890625E-05 * (double)(sv + 32767) * scaleV) + minV;
            } else {
                std::memcpy(&defU, defaultValue.data(), 4);
                std::memcpy(&defV, defaultValue.data() + 4, 4);
            }
        }

        for (uint32_t i = 0; i < nVertices; i++) {
            if (isDiscretized) {
                if (defaultFill) {
                    arr[i*2] = defU;
                    arr[i*2+1] = defV;
                } else {
                    int16_t su, sv;
                    std::memcpy(&su, uvData.data() + i*4, 2);
                    std::memcpy(&sv, uvData.data() + i*4 + 2, 2);
                    arr[i*2] = (float)(1.52587890625E-05 * (double)(su + 32767) * scaleU) + minU;
                    arr[i*2+1] = (float)(1.52587890625E-05 * (double)(sv + 32767) * scaleV) + minV;
                }
            } else {
                if (defaultFill) {
                    arr[i*2] = defU;
                    arr[i*2+1] = defV;
                } else {
                    std::memcpy(&arr[i*2], uvData.data() + i*8, 4);
                    std::memcpy(&arr[i*2+1], uvData.data() + i*8 + 4, 4);
                }
            }
        }
        return arr;
    }

    void read(BinaryReaderEx& input, uint32_t odolVersion) {
        isDiscretized = false;
        if (odolVersion >= 45) {
            isDiscretized = true;
            minU = input.ReadSingle();
            minV = input.ReadSingle();
            maxU = input.ReadSingle();
            maxV = input.ReadSingle();
        }
        nVertices = input.ReadUInt32();
        defaultFill = input.ReadBoolean();
        int elemSize = (odolVersion >= 45) ? 4 : 8;
        if (defaultFill) {
            defaultValue = input.ReadBytes(elemSize);
        } else {
            uvData = input.ReadCompressed(static_cast<uint32_t>(nVertices * static_cast<uint32_t>(elemSize)));
        }
    }
};

struct Skeleton {
    std::string Name;
    bool isDiscrete = false;
    std::vector<std::string> bones; // bones[i*2] = name, bones[i*2+1] = parent
    std::string pivotsNameObsolete;

    void read(BinaryReaderEx& input) {
        Name = input.ReadAsciiz();
        if (Name.empty()) return;
        if (input.Version >= 23) isDiscrete = input.ReadBoolean();
        int32_t nBones = input.ReadInt32();
        bones.resize(static_cast<size_t>(nBones) * 2);
        for (size_t i = 0; i < static_cast<size_t>(nBones); i++) {
            bones[i*2] = input.ReadAsciiz();
            bones[i*2+1] = input.ReadAsciiz();
        }
        if (input.Version > 40) pivotsNameObsolete = input.ReadAsciiz();
    }
};

struct LoadableLodInfo {
    int32_t nFaces = 0;
    uint32_t color = 0;
    int32_t special = 0;
    uint32_t orHints = 0;
    bool hasSkeleton = false;
    int32_t nVertices = 0;
    float faceArea = 0.f;

    void read(BinaryReaderEx& input) {
        nFaces = input.ReadInt32();
        color = input.ReadUInt32();
        special = input.ReadInt32();
        orHints = input.ReadUInt32();
        if (input.Version >= 39) hasSkeleton = input.ReadBoolean();
        if (input.Version >= 51) {
            nVertices = input.ReadInt32();
            faceArea = input.ReadSingle();
        }
    }
};

struct Animations {
    // AnimationClass sub-structure
    struct AnimationClass {
        uint32_t animType = 0;
        std::string animName;
        std::string animSource;
        float minPhase = 0.f, maxPhase = 0.f;
        float minValue = 0.f, maxValue = 0.f;
        float animPeriod = 0.f, initPhase = 0.f;
        uint32_t sourceAddress = 0;

        // Type-specific data
        float angle0 = 0.f, angle1 = 0.f;
        float offset0 = 0.f, offset1 = 0.f;
        Vec3 axisPos, axisDir;
        float angle = 0.f, axisOffset = 0.f;
        float hideValue = 0.f;

        void read(BinaryReaderEx& input) {
            animType = input.ReadUInt32();
            animName = input.ReadAsciiz();
            animSource = input.ReadAsciiz();
            minPhase = input.ReadSingle();
            maxPhase = input.ReadSingle();
            minValue = input.ReadSingle();
            maxValue = input.ReadSingle();
            if (input.Version >= 56) {
                animPeriod = input.ReadSingle();
                initPhase = input.ReadSingle();
            }
            sourceAddress = input.ReadUInt32();
            switch (animType) {
                case 0: case 1: case 2: case 3: // Rotation*
                    angle0 = input.ReadSingle();
                    angle1 = input.ReadSingle();
                    break;
                case 4: case 5: case 6: case 7: // Translation*
                    offset0 = input.ReadSingle();
                    offset1 = input.ReadSingle();
                    break;
                case 8: // Direct
                    axisPos = input.ReadVector3P();
                    axisDir = input.ReadVector3P();
                    angle = input.ReadSingle();
                    axisOffset = input.ReadSingle();
                    break;
                case 9: // Hide
                    hideValue = input.ReadSingle();
                    if (input.Version >= 55) input.ReadSingle(); // skip
                    break;
                default:
                    throw std::runtime_error("Unknown AnimType: " + std::to_string(animType));
            }
        }
    };

    std::vector<AnimationClass> animationClasses;
    int32_t nAnimLODs = 0;
    std::vector<std::vector<std::vector<uint32_t>>> Bones2Anims;
    std::vector<std::vector<int32_t>> Anims2Bones;
    std::vector<std::vector<std::vector<Vec3>>> axisData; // [lod][anim][0..1]

    void read(BinaryReaderEx& input) {
        int32_t nClasses = input.ReadInt32();
        animationClasses.resize(static_cast<size_t>(nClasses));
        for (size_t i = 0; i < static_cast<size_t>(nClasses); i++) animationClasses[i].read(input);

        nAnimLODs = input.ReadInt32();
        Bones2Anims.resize(static_cast<size_t>(nAnimLODs));
        for (size_t i = 0; i < static_cast<size_t>(nAnimLODs); i++) {
            uint32_t nBones = input.ReadUInt32();
            Bones2Anims[i].resize(nBones);
            for (uint32_t j = 0; j < nBones; j++) {
                uint32_t nAnims = input.ReadUInt32();
                Bones2Anims[i][j].resize(nAnims);
                for (uint32_t k = 0; k < nAnims; k++) {
                    Bones2Anims[i][j][k] = input.ReadUInt32();
                }
            }
        }

        Anims2Bones.resize(static_cast<size_t>(nAnimLODs));
        axisData.resize(static_cast<size_t>(nAnimLODs));
        for (size_t l = 0; l < static_cast<size_t>(nAnimLODs); l++) {
            Anims2Bones[l].resize(static_cast<size_t>(nClasses));
            axisData[l].resize(static_cast<size_t>(nClasses));
            for (size_t m = 0; m < static_cast<size_t>(nClasses); m++) {
                Anims2Bones[l][m] = input.ReadInt32();
                if (Anims2Bones[l][m] != -1 &&
                    animationClasses[m].animType != 8 && // Direct
                    animationClasses[m].animType != 9)   // Hide
                {
                    axisData[l][m].resize(2);
                    axisData[l][m][0] = input.ReadVector3P();
                    axisData[l][m][1] = input.ReadVector3P();
                }
            }
        }
    }
};

struct ODOL_ModelInfo {
    int32_t special = 0;
    float BoundingSphere = 0.f;
    float GeometrySphere = 0.f;
    int32_t remarks = 0;
    int32_t andHints = 0;
    int32_t orHints = 0;
    Vec3 AimingCenter;
    uint32_t color = 0;
    uint32_t colorType = 0;
    float viewDensity = 0.f;
    Vec3 bboxMin, bboxMax;
    float propertyLodDensityCoef = 0.f;
    float propertyDrawImportance = 0.f;
    Vec3 bboxMinVisual, bboxMaxVisual;
    Vec3 boundingCenter;
    Vec3 geometryCenter;
    Vec3 centerOfMass;
    Mat3 invInertia;
    bool autoCenter = false, lockAutoCenter = false;
    bool canOcclude = false, canBeOccluded = false;
    bool AICovers = false;
    float htMin = 0.f, htMax = 0.f, afMax = 0.f, mfMax = 0.f;
    float mFact = 0.f, tBody = 0.f;
    bool forceNotAlphaModel = false;
    int32_t sbSource = 0;
    bool prefershadowvolume = false;
    float shadowOffset = 0.f;
    bool animated = false;
    Skeleton skeleton;
    uint8_t mapType = 0;
    std::vector<float> massArray;
    float mass = 0.f, invMass = 0.f;
    float armor = 0.f, invArmor = 0.f;
    float propertyExplosionShielding = 0.f;
    uint8_t geometrySimple = 0, geometryPhys = 0;
    uint8_t memory = 0, geometry = 0;
    uint8_t geometryFire = 0, geometryView = 0;
    uint8_t geometryViewPilot = 0, geometryViewGunner = 0;
    uint8_t geometryViewCargo = 0;
    uint8_t landContact = 0, roadway = 0, paths = 0, hitpoints = 0;
    uint32_t minShadow = 0;
    bool canBlend = false;
    std::string propertyClass;
    std::string propertyDamage;
    bool propertyFrequent = false;
    std::vector<int32_t> preferredShadowVolumeLod;
    std::vector<int32_t> preferredShadowBufferLod;
    std::vector<int32_t> preferredShadowBufferLodVis;

    void read(BinaryReaderEx& input, int nLods) {
        int version = input.Version;
        special = input.ReadInt32();
        BoundingSphere = input.ReadSingle();
        GeometrySphere = input.ReadSingle();
        remarks = input.ReadInt32();
        andHints = input.ReadInt32();
        orHints = input.ReadInt32();
        AimingCenter = input.ReadVector3P();
        color = input.ReadUInt32();
        colorType = input.ReadUInt32();
        viewDensity = input.ReadSingle();
        bboxMin = input.ReadVector3P();
        bboxMax = input.ReadVector3P();
        if (version >= 70) propertyLodDensityCoef = input.ReadSingle();
        if (version >= 71) propertyDrawImportance = input.ReadSingle();
        if (version >= 52) {
            bboxMinVisual = input.ReadVector3P();
            bboxMaxVisual = input.ReadVector3P();
        }
        boundingCenter = input.ReadVector3P();
        geometryCenter = input.ReadVector3P();
        centerOfMass = input.ReadVector3P();
        // invInertia = Matrix3P
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
                invInertia[c][r] = input.ReadSingle();
        autoCenter = input.ReadBoolean();
        lockAutoCenter = input.ReadBoolean();
        canOcclude = input.ReadBoolean();
        canBeOccluded = input.ReadBoolean();
        if (version >= 73) AICovers = input.ReadBoolean();
        if ((version >= 42 && version < 10000) || version >= 10042) {
            htMin = input.ReadSingle();
            htMax = input.ReadSingle();
            afMax = input.ReadSingle();
            mfMax = input.ReadSingle();
        }
        if ((version >= 43 && version < 10000) || version >= 10043) {
            mFact = input.ReadSingle();
            tBody = input.ReadSingle();
        }
        if (version >= 33) forceNotAlphaModel = input.ReadBoolean();
        if (version >= 37) {
            sbSource = input.ReadInt32();
            prefershadowvolume = input.ReadBoolean();
        }
        if (version >= 48) shadowOffset = input.ReadSingle();
        animated = input.ReadBoolean();
        skeleton.read(input);
        mapType = input.ReadByte();
        massArray = input.ReadCompressedFloatArray();
        mass = input.ReadSingle();
        invMass = input.ReadSingle();
        armor = input.ReadSingle();
        invArmor = input.ReadSingle();
        if (version >= 72) propertyExplosionShielding = input.ReadSingle();
        if (version >= 53) geometrySimple = input.ReadByte();
        if (version >= 54) geometryPhys = input.ReadByte();
        memory = input.ReadByte();
        geometry = input.ReadByte();
        geometryFire = input.ReadByte();
        geometryView = input.ReadByte();
        geometryViewPilot = input.ReadByte();
        geometryViewGunner = input.ReadByte();
        input.ReadSByte(); // skip
        geometryViewCargo = input.ReadByte();
        landContact = input.ReadByte();
        roadway = input.ReadByte();
        paths = input.ReadByte();
        hitpoints = input.ReadByte();
        minShadow = input.ReadUInt32();
        if (version >= 38) canBlend = input.ReadBoolean();
        propertyClass = input.ReadAsciiz();
        propertyDamage = input.ReadAsciiz();
        propertyFrequent = input.ReadBoolean();
        if (version >= 31) input.ReadUInt32(); // skip
        if (version >= 57) {
            preferredShadowVolumeLod.resize(static_cast<size_t>(nLods));
            preferredShadowBufferLod.resize(static_cast<size_t>(nLods));
            preferredShadowBufferLodVis.resize(static_cast<size_t>(nLods));
            for (size_t i = 0; i < static_cast<size_t>(nLods); i++) preferredShadowVolumeLod[i] = input.ReadInt32();
            for (size_t i = 0; i < static_cast<size_t>(nLods); i++) preferredShadowBufferLod[i] = input.ReadInt32();
            for (size_t i = 0; i < static_cast<size_t>(nLods); i++) preferredShadowBufferLodVis[i] = input.ReadInt32();
        }
    }
};

// LOD structure
struct LOD {
    uint32_t odolVersion = 0;
    float resolution = 0.f;

    std::vector<Proxy> proxies;
    std::vector<int32_t> subSkeletonsToSkeleton;
    std::vector<SubSkeletonIndexSet> skeletonToSubSkeleton;
    uint32_t vertexCount_ = 0;
    float faceArea = 0.f;
    std::vector<ClipFlags> clipOldFormat;
    std::vector<ClipFlags> clip;
    ClipFlags orHints = ClipFlags::ClipNone;
    ClipFlags andHints = ClipFlags::ClipNone;
    Vec3 bMin, bMax, bCenter;
    float bRadius = 0.f;
    std::vector<std::string> textures;
    std::vector<EmbeddedMaterial> materials;
    std::vector<int32_t> pointToVertex;
    std::vector<int32_t> vertexToPoint;
    Polygons polygons;
    std::vector<Section> sections;
    std::vector<NamedSelection> namedSelections;
    uint32_t nNamedProperties = 0;
    std::vector<std::string> namedPropertiesFlat; // [i*2] = key, [i*2+1] = value
    std::vector<Keyframe> frames;
    int32_t colorTop = 0, color_ = 0, special = 0;
    bool vertexBoneRefIsSimple = false;
    uint32_t sizeOfRestData = 0;
    uint32_t nUVSets = 0;
    std::vector<UVSet> uvSets;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<AnimationRTWeight> vertexBoneRef;
    std::vector<VertexNeighborInfo> neighborBoneRef;

    const std::vector<ClipFlags>& ClipFlagsArr() const {
        if (odolVersion < 50) return clipOldFormat;
        return clip;
    }
    int VertexCount() const { return (int)vertices.size(); }
    const std::vector<Polygon>& Faces() const { return polygons.Faces; }

    void read(BinaryReaderEx& input, float res) {
        odolVersion = (uint32_t)input.Version;
        resolution = res;

        // Proxies
        {
            int32_t n = input.ReadInt32();
            proxies.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) proxies[i].read(input);
        }

        subSkeletonsToSkeleton = input.ReadIntArray();

        // SkeletonToSubSkeleton
        {
            int32_t n = input.ReadInt32();
            skeletonToSubSkeleton.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) skeletonToSubSkeleton[i].read(input);
        }

        if (odolVersion >= 50) {
            vertexCount_ = input.ReadUInt32();
        } else {
            auto arr = input.ReadCondensedIntArray();
            clipOldFormat.resize(arr.size());
            for (size_t i = 0; i < arr.size(); i++) clipOldFormat[i] = (ClipFlags)arr[i];
        }

        if (odolVersion >= 51) faceArea = input.ReadSingle();

        orHints = (ClipFlags)input.ReadInt32();
        andHints = (ClipFlags)input.ReadInt32();
        bMin = input.ReadVector3P();
        bMax = input.ReadVector3P();
        bCenter = input.ReadVector3P();
        bRadius = input.ReadSingle();
        textures = input.ReadStringArray();

        // Materials
        {
            int32_t n = input.ReadInt32();
            materials.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) materials[i].read(input);
        }

        pointToVertex = input.ReadCompressedVertexIndexArray();
        vertexToPoint = input.ReadCompressedVertexIndexArray();

        polygons.read(input);

        // Sections
        {
            int32_t n = input.ReadInt32();
            sections.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) sections[i].read(input);
        }

        // NamedSelections
        {
            int32_t n = input.ReadInt32();
            namedSelections.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) namedSelections[i].read(input);
        }

        nNamedProperties = input.ReadUInt32();
        namedPropertiesFlat.resize(nNamedProperties * 2);
        for (uint32_t i = 0; i < nNamedProperties; i++) {
            namedPropertiesFlat[i*2] = input.ReadAsciiz();
            namedPropertiesFlat[i*2+1] = input.ReadAsciiz();
        }

        // Frames (Keyframes)
        {
            int32_t n = input.ReadInt32();
            frames.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) frames[i].read(input);
        }

        colorTop = input.ReadInt32();
        color_ = input.ReadInt32();
        special = input.ReadInt32();
        vertexBoneRefIsSimple = input.ReadBoolean();
        sizeOfRestData = input.ReadUInt32();

        if (odolVersion >= 50) {
            auto arr = input.ReadCondensedIntArray();
            clip.resize(arr.size());
            for (size_t i = 0; i < arr.size(); i++) clip[i] = (ClipFlags)arr[i];
        }

        // UVSets
        UVSet firstUV;
        firstUV.read(input, odolVersion);
        nUVSets = input.ReadUInt32();
        uvSets.resize(nUVSets);
        uvSets[0] = std::move(firstUV);
        for (uint32_t i = 1; i < nUVSets; i++) {
            uvSets[i].read(input, odolVersion);
        }

        // Vertices (compressed)
        {
            int32_t n = input.ReadInt32();
            uint32_t expectedSize = (uint32_t)(n * 12);
            auto data = input.ReadCompressed(expectedSize);
            auto ms = std::make_unique<std::istringstream>(
                std::string(reinterpret_cast<char*>(data.data()), data.size()));
            BinaryReaderEx sub(std::move(ms));
            sub.Version = input.Version;
            vertices.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) {
                vertices[i] = sub.ReadVector3P();
            }
        }

        // Normals
        if (odolVersion >= 45) {
            // Compressed (Vector3PCompressed = 4 bytes each)
            auto compressed = input.ReadCondensedArray<int32_t>(
                [](BinaryReaderEx& r) { return r.ReadInt32(); }, 4);
            normals.resize(compressed.size());
            constexpr float scaleFactor = -0.0019569471f;
            for (size_t i = 0; i < compressed.size(); i++) {
                int val = compressed[i];
                int x = val & 0x3FF;
                int y = (val >> 10) & 0x3FF;
                int z = (val >> 20) & 0x3FF;
                if (x > 511) x -= 1024;
                if (y > 511) y -= 1024;
                if (z > 511) z -= 1024;
                normals[i] = Vec3((float)x * scaleFactor, (float)y * scaleFactor, (float)z * scaleFactor);
            }
        } else {
            normals = input.ReadCondensedArray<Vec3>(
                [](BinaryReaderEx& r) { return r.ReadVector3P(); }, 12);
        }

        // STCoords (skip, not used in conversion)
        if (odolVersion >= 45) {
            // STPairCompressed = 8 bytes each
            int32_t n = input.ReadInt32();
            uint32_t expectedSize = (uint32_t)(n * 8);
            auto data = input.ReadCompressed(expectedSize);
            // We don't need ST coords for MLOD conversion, just consume
        } else {
            // STPairUncompressed = 24 bytes each
            int32_t n = input.ReadInt32();
            uint32_t expectedSize = (uint32_t)(n * 24);
            auto data = input.ReadCompressed(expectedSize);
        }

        // VertexBoneRef (AnimationRTWeight, 12 bytes each)
        {
            int32_t n = input.ReadInt32();
            uint32_t expectedSize = (uint32_t)(n * 12);
            auto data = input.ReadCompressed(expectedSize);
            auto ms = std::make_unique<std::istringstream>(
                std::string(reinterpret_cast<char*>(data.data()), data.size()));
            BinaryReaderEx sub(std::move(ms));
            sub.Version = input.Version;
            vertexBoneRef.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) vertexBoneRef[i].read(sub);
        }

        // NeighborBoneRef (VertexNeighborInfo, 32 bytes each)
        {
            int32_t n = input.ReadInt32();
            uint32_t expectedSize = (uint32_t)(n * 32);
            auto data = input.ReadCompressed(expectedSize);
            auto ms = std::make_unique<std::istringstream>(
                std::string(reinterpret_cast<char*>(data.data()), data.size()));
            BinaryReaderEx sub(std::move(ms));
            sub.Version = input.Version;
            neighborBoneRef.resize(static_cast<size_t>(n));
            for (size_t i = 0; i < static_cast<size_t>(n); i++) neighborBoneRef[i].read(sub);
        }

        if (odolVersion >= 67) input.ReadUInt32();
        if (odolVersion >= 68) input.ReadByte();
    }
};

// ODOL top-level
struct ODOL_File {
    uint32_t version = 0;
    std::string muzzleFlash;
    uint32_t appID = 0;
    int32_t nLods = 0;
    std::vector<float> resolutions;
    ODOL_ModelInfo modelInfo;
    bool hasAnims = false;
    Animations animations;
    std::vector<uint32_t> lodStartAddresses;
    std::vector<uint32_t> lodEndAddresses;
    std::vector<bool> permanent;
    std::vector<LOD> lods;

    void read(BinaryReaderEx& input) {
        std::string sig = input.ReadAscii(4);
        if (sig != "ODOL") throw std::runtime_error("ODOL signature is missing");
        version = input.ReadUInt32();
        if (version > 75) throw std::runtime_error("Unknown ODOL version");
        if (version < 28) throw std::runtime_error("Old ODOL version not supported");

        input.Version = (int)version;
        if (version >= 44) input.UseLZOCompression = true;
        if (version >= 64) input.UseCompressionFlag = true;

        if (version >= 59) appID = input.ReadUInt32();
        if (version >= 74) input.ReadUInt32(); // unknown field 1
        if (version >= 74) input.ReadUInt32(); // unknown field 2
        if (version >= 58) muzzleFlash = input.ReadAsciiz();

        nLods = input.ReadInt32();
        resolutions.resize(static_cast<size_t>(nLods));
        for (int i = 0; i < nLods; i++) resolutions[static_cast<size_t>(i)] = input.ReadSingle();

        modelInfo.read(input, nLods);

        if (version >= 30) {
            hasAnims = input.ReadBoolean();
            if (hasAnims) animations.read(input);
        }

        lodStartAddresses.resize(static_cast<size_t>(nLods));
        lodEndAddresses.resize(static_cast<size_t>(nLods));
        permanent.resize(static_cast<size_t>(nLods));

        for (int i = 0; i < nLods; i++) lodStartAddresses[static_cast<size_t>(i)] = input.ReadUInt32();
        for (int i = 0; i < nLods; i++) lodEndAddresses[static_cast<size_t>(i)] = input.ReadUInt32();
        for (int i = 0; i < nLods; i++) permanent[static_cast<size_t>(i)] = input.ReadBoolean();

        lods.resize(static_cast<size_t>(nLods));
        int64_t currentPos = input.Position();

        for (int i = 0; i < nLods; i++) {
            if (!permanent[static_cast<size_t>(i)]) {
                LoadableLodInfo lli;
                lli.read(input);
                currentPos = input.Position();
            }
            input.SetPosition((int64_t)lodStartAddresses[static_cast<size_t>(i)]);
            lods[static_cast<size_t>(i)].read(input, resolutions[static_cast<size_t>(i)]);
            input.SetPosition(currentPos);
        }

        // Seek to max end address
        uint32_t maxEnd = *std::max_element(lodEndAddresses.begin(), lodEndAddresses.end());
        input.SetPosition((int64_t)maxEnd);
    }
};

}}} // BisDll::Model::ODOL

// ============================================================================
// BisDll.Model.ODOL7 - ODOL version 7 (OFP/CWA) structures
// ============================================================================
namespace BisDll { namespace Model { namespace ODOL7 {

// Helper: read count-prefixed LZSS-compressed array (v7 style)
template<typename T>
static std::vector<T> readCompressedArrayV7(BinaryReaderEx& input) {
    uint32_t count = input.ReadUInt32();
    uint32_t byteSize = (uint32_t)(count * sizeof(T));
    auto data = input.ReadLZSS(byteSize);
    std::vector<T> result(count);
    if (count > 0) {
        std::memcpy(result.data(), data.data(), byteSize);
    }
    return result;
}

struct Face7 {
    uint32_t flags = 0;
    uint16_t textureIndex = 0;
    uint8_t nVerts = 0;
    uint16_t v0 = 0, v1 = 0, v2 = 0, v3 = 0;

    bool IsQuad() const { return nVerts == 4; }
};

struct ShapeSection7 {
    uint32_t startIndex = 0;
    uint32_t endIndex = 0;
    int32_t material = 0;
    int16_t textureIndex = 0;
    int32_t special = 0;

    void read(BinaryReaderEx& input) {
        startIndex = input.ReadUInt32();
        endIndex = input.ReadUInt32();
        material = input.ReadInt32();
        textureIndex = input.ReadInt16();
        special = input.ReadInt32();
    }
};

struct NamedSection7 {
    std::string name;
    std::vector<uint16_t> faceIndices;
    std::vector<uint8_t> faceWeights;
    std::vector<uint32_t> faceSelectionIndices;
    bool needSelection = false;
    std::vector<uint32_t> faceSelectionIndices2;
    std::vector<uint16_t> vertexIndices;
    std::vector<uint8_t> vertexWeights;

    void read(BinaryReaderEx& input) {
        name = input.ReadAsciiz();
        // lowercase
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        faceIndices = readCompressedArrayV7<uint16_t>(input);
        faceWeights = readCompressedArrayV7<uint8_t>(input);
        faceSelectionIndices = readCompressedArrayV7<uint32_t>(input);
        needSelection = input.ReadBoolean();
        faceSelectionIndices2 = readCompressedArrayV7<uint32_t>(input);
        vertexIndices = readCompressedArrayV7<uint16_t>(input);
        vertexWeights = readCompressedArrayV7<uint8_t>(input);
    }
};

struct NamedProperty7 {
    std::string name;
    std::string value;

    void read(BinaryReaderEx& input) {
        name = input.ReadAsciiz();
        value = input.ReadAsciiz();
    }
};

struct AnimationPhase7 {
    float time = 0.f;
    std::vector<Vec3> points;

    void read(BinaryReaderEx& input) {
        time = input.ReadSingle();
        uint32_t n = input.ReadUInt32();
        points.resize(n);
        for (uint32_t i = 0; i < n; i++) points[i] = input.ReadVector3P();
    }
};

struct ProxyObject7 {
    std::string name;
    Mat4 transform;
    int32_t id = 0;
    int32_t sectionIndex = 0;

    void read(BinaryReaderEx& input) {
        name = input.ReadAsciiz();
        // Read Matrix4P: 3x4 (row-major in file: aside, up, dir, pos)
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
                transform.orientation[c][r] = input.ReadSingle();
        for (int r = 0; r < 3; r++)
            transform.position[r] = input.ReadSingle();
        id = input.ReadInt32();
        sectionIndex = input.ReadInt32();
    }
};

struct LOD7 {
    std::vector<uint32_t> vertFlags;
    std::vector<float> uvData; // interleaved u,v pairs per vertex
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    uint32_t hintsOr = 0;
    uint32_t hintsAnd = 0;
    Vec3 bMin, bMax, bCenter;
    float bRadius = 0.f;
    std::vector<std::string> textureNames;
    std::vector<uint16_t> pointToVertices;
    std::vector<uint16_t> vertexToPoints;
    std::vector<Face7> faces;
    std::vector<ShapeSection7> sections;
    std::vector<NamedSection7> namedSections;
    std::vector<NamedProperty7> namedProperties;
    std::vector<AnimationPhase7> animationPhases;
    uint32_t colorTop = 0;
    uint32_t color2 = 0;
    uint32_t flags2 = 0;
    std::vector<ProxyObject7> proxies;

    void read(BinaryReaderEx& input) {
        vertFlags = readCompressedArrayV7<uint32_t>(input);

        // UV data: compressed array of float pairs (2 floats per vertex)
        {
            uint32_t uvCount = input.ReadUInt32();
            uint32_t uvByteSize = uvCount * 8; // 2 floats * 4 bytes
            auto data = input.ReadLZSS(uvByteSize);
            uvData.resize(uvCount * 2);
            if (uvCount > 0) {
                std::memcpy(uvData.data(), data.data(), uvByteSize);
            }
        }

        // Positions: uncompressed array with count prefix
        {
            uint32_t n = input.ReadUInt32();
            positions.resize(n);
            for (uint32_t i = 0; i < n; i++) positions[i] = input.ReadVector3P();
        }

        // Normals: uncompressed array with count prefix
        {
            uint32_t n = input.ReadUInt32();
            normals.resize(n);
            for (uint32_t i = 0; i < n; i++) normals[i] = input.ReadVector3P();
        }

        hintsOr = input.ReadUInt32();
        hintsAnd = input.ReadUInt32();
        bMin = input.ReadVector3P();
        bMax = input.ReadVector3P();
        bCenter = input.ReadVector3P();
        bRadius = input.ReadSingle();

        // Texture names
        {
            uint32_t n = input.ReadUInt32();
            textureNames.resize(n);
            for (uint32_t i = 0; i < n; i++) textureNames[i] = input.ReadAsciiz();
        }

        pointToVertices = readCompressedArrayV7<uint16_t>(input);
        vertexToPoints = readCompressedArrayV7<uint16_t>(input);

        // Faces
        {
            uint32_t faceCount = input.ReadUInt32();
            uint32_t faceDataSize = input.ReadUInt32(); // total byte size (unused)
            (void)faceDataSize;
            faces.reserve(faceCount);
            for (uint32_t i = 0; i < faceCount; i++) {
                Face7 f;
                f.flags = input.ReadUInt32();
                f.textureIndex = input.ReadUInt16();
                f.nVerts = input.ReadByte();
                if (f.nVerts == 3) {
                    f.v0 = input.ReadUInt16();
                    f.v1 = input.ReadUInt16();
                    f.v2 = input.ReadUInt16();
                    f.v3 = f.v0; // not a quad
                } else if (f.nVerts == 4) {
                    f.v0 = input.ReadUInt16();
                    f.v1 = input.ReadUInt16();
                    f.v2 = input.ReadUInt16();
                    f.v3 = input.ReadUInt16();
                } else {
                    throw std::runtime_error("ODOL7: Invalid face vertex count: " + std::to_string(f.nVerts));
                }
                faces.push_back(f);
            }
        }

        // Sections
        {
            uint32_t n = input.ReadUInt32();
            sections.resize(n);
            for (uint32_t i = 0; i < n; i++) sections[i].read(input);
        }

        // Named sections
        {
            uint32_t n = input.ReadUInt32();
            namedSections.resize(n);
            for (uint32_t i = 0; i < n; i++) namedSections[i].read(input);
        }

        // Named properties
        {
            uint32_t n = input.ReadUInt32();
            namedProperties.resize(n);
            for (uint32_t i = 0; i < n; i++) namedProperties[i].read(input);
        }

        // Animation phases
        {
            uint32_t n = input.ReadUInt32();
            animationPhases.resize(n);
            for (uint32_t i = 0; i < n; i++) animationPhases[i].read(input);
        }

        // Colors and flags
        colorTop = input.ReadUInt32();
        color2 = input.ReadUInt32();
        flags2 = input.ReadUInt32();

        // Proxies
        {
            uint32_t n = input.ReadUInt32();
            proxies.resize(n);
            for (uint32_t i = 0; i < n; i++) proxies[i].read(input);
        }
    }
};

union LodType7 {
    float graphical;
    uint32_t functional;
};

struct ODOL7_File {
    uint32_t version = 0;
    uint32_t lodCount = 0;
    std::vector<LOD7> lods;
    std::vector<LodType7> lodDistances;

    // Model-level data (read after LODs)
    uint32_t properties = 0;
    float lodSphere = 0.f;
    float physicsSphere = 0.f;
    uint32_t properties2 = 0;
    uint32_t hintsAnd = 0;
    uint32_t hintsOr = 0;
    Vec3 aimPoint;
    uint32_t color = 0;
    uint32_t color2 = 0;
    float density = 0.f;
    Vec3 bMin, bMax;
    Vec3 lodCenter;
    Vec3 physicsCenter;
    Vec3 massCenter;
    Mat3 invInertia;
    bool autoCenter = false;
    bool autoCenter2 = false;
    bool canOcclude = false;
    bool canBeOccluded = false;
    bool allowAnimation = false;
    uint8_t mapType = 0;
    std::vector<float> masses;
    float mass = 0.f;
    float invMass = 0.f;
    float armor = 0.f;
    float invArmor = 0.f;
    int8_t memoryLodIndex = -1;
    int8_t geometryLodIndex = -1;
    int8_t geometryFireLodIndex = -1;
    int8_t geometryViewLodIndex = -1;
    int8_t geometryViewPilotLodIndex = -1;
    int8_t geometryViewGunnerLodIndex = -1;
    int8_t geometryViewCommanderLodIndex = -1;
    int8_t geometryViewCargoLodIndex = -1;
    int8_t landContactLodIndex = -1;
    int8_t roadwayLodIndex = -1;
    int8_t pathsLodIndex = -1;
    int8_t hitpointsLodIndex = -1;

    void read(BinaryReaderEx& input) {
        // Signature already consumed; version already read by caller
        input.Version = (int)version;
        input.UseLZOCompression = false;
        input.UseCompressionFlag = false;

        lodCount = input.ReadUInt32();

        // v7: LODs come first, then model info
        lods.resize(lodCount);
        for (uint32_t i = 0; i < lodCount; i++) {
            lods[i].read(input);
        }

        // LOD distances (resolution values)
        lodDistances.resize(lodCount);
        for (uint32_t i = 0; i < lodCount; i++) {
            uint32_t raw = input.ReadUInt32();
            std::memcpy(&lodDistances[i], &raw, 4);
        }

        // Model info
        properties = input.ReadUInt32();
        lodSphere = input.ReadSingle();
        physicsSphere = input.ReadSingle();
        properties2 = input.ReadUInt32();
        hintsAnd = input.ReadUInt32();
        hintsOr = input.ReadUInt32();
        aimPoint = input.ReadVector3P();
        color = input.ReadUInt32();
        color2 = input.ReadUInt32();
        density = input.ReadSingle();
        bMin = input.ReadVector3P();
        bMax = input.ReadVector3P();
        lodCenter = input.ReadVector3P();
        physicsCenter = input.ReadVector3P();
        massCenter = input.ReadVector3P();

        // invInertia = Matrix3
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
                invInertia[c][r] = input.ReadSingle();

        autoCenter = input.ReadBoolean();
        autoCenter2 = input.ReadBoolean();
        canOcclude = input.ReadBoolean();
        canBeOccluded = input.ReadBoolean();
        allowAnimation = input.ReadBoolean();
        mapType = input.ReadByte();

        // Masses (LZSS compressed)
        {
            uint32_t n = input.ReadUInt32();
            uint32_t byteSize = n * 4;
            auto data = input.ReadLZSS(byteSize);
            masses.resize(n);
            if (n > 0) {
                std::memcpy(masses.data(), data.data(), byteSize);
            }
        }

        mass = input.ReadSingle();
        invMass = input.ReadSingle();
        armor = input.ReadSingle();
        invArmor = input.ReadSingle();

        // LOD indices
        memoryLodIndex = input.ReadSByte();
        geometryLodIndex = input.ReadSByte();
        geometryFireLodIndex = input.ReadSByte();
        geometryViewLodIndex = input.ReadSByte();
        geometryViewPilotLodIndex = input.ReadSByte();
        geometryViewGunnerLodIndex = input.ReadSByte();
        geometryViewCommanderLodIndex = input.ReadSByte();
        geometryViewCargoLodIndex = input.ReadSByte();
        landContactLodIndex = input.ReadSByte();
        roadwayLodIndex = input.ReadSByte();
        pathsLodIndex = input.ReadSByte();
        hitpointsLodIndex = input.ReadSByte();
    }
};

}}} // BisDll::Model::ODOL7

// ============================================================================
// BisDll.Model.MLOD - Data structures
// ============================================================================
namespace BisDll { namespace Model { namespace MLOD {

struct Vertex {
    int32_t PointIndex = 0;
    int32_t NormalIndex = 0;
    float U = 0.f;
    float V = 0.f;

    Vertex() = default;
    Vertex(int pi, int ni, float u, float v) : PointIndex(pi), NormalIndex(ni), U(u), V(v) {}

    void write(BinaryWriterEx& output) const {
        output.Write(PointIndex);
        output.Write(NormalIndex);
        output.Write(U);
        output.Write(V);
    }
};

struct Point {
    Vec3 pos;
    PointFlags flags = PointFlags::NONE;

    Point() = default;
    Point(const Vec3& p, PointFlags f) : pos(p), flags(f) {}

    void write(BinaryWriterEx& output) const {
        output.writeVec3(pos);
        output.Write((uint32_t)flags);
    }
};

struct Face {
    int32_t NumberOfVertices = 0;
    Vertex Vertices[4];
    FaceFlags Flags = FaceFlags::DEFAULT;
    std::string Texture;
    std::string Material;

    Face() = default;
    Face(int nv, const Vertex* verts, FaceFlags flags, const std::string& tex, const std::string& mat)
        : NumberOfVertices(nv), Flags(flags), Texture(tex), Material(mat) {
        for (int i = 0; i < 4; i++) {
            if (i < nv) Vertices[i] = verts[i];
        }
    }

    void write(BinaryWriterEx& output) const {
        output.Write(NumberOfVertices);
        for (int i = 0; i < 4; i++) {
            if (i < NumberOfVertices) {
                Vertices[i].write(output);
            } else {
                output.Write((int32_t)0);
                output.Write((int32_t)0);
                output.Write(0.f);
                output.Write(0.f);
            }
        }
        output.Write((int32_t)Flags);
        output.writeAsciiz(Texture);
        output.writeAsciiz(Material);
    }
};

// Tagg base
struct Tagg {
    uint32_t DataSize = 0;
    std::string Name;
    virtual ~Tagg() = default;
    virtual void write(BinaryWriterEx& output) const = 0;
};

struct NamedSelectionTagg : Tagg {
    std::vector<uint8_t> points;
    std::vector<uint8_t> faces;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        for (auto b : points) output.Write(b);
        for (auto b : faces) output.Write(b);
    }
};

struct MassTagg : Tagg {
    std::vector<float> mass;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        for (auto m : mass) output.Write(m);
    }
};

struct UVSetTagg : Tagg {
    uint32_t uvSetNr = 0;
    // faceUVs[faceIdx] = array of [nVerts][2]
    std::vector<std::vector<std::pair<float,float>>> faceUVs;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        output.Write(uvSetNr);
        for (const auto& face : faceUVs) {
            for (const auto& uv : face) {
                output.Write(uv.first);
                output.Write(uv.second);
            }
        }
    }
};

struct PropertyTagg : Tagg {
    std::string name;
    std::string value;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        output.writeAscii(name, 64);
        output.writeAscii(value, 64);
    }
};

struct AnimationTagg : Tagg {
    float frameTime = 0.f;
    std::vector<Vec3> framePoints;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        output.Write(frameTime);
        for (const auto& p : framePoints) output.writeVec3(p);
    }
};

struct SharpEdgesTagg : Tagg {
    std::vector<std::pair<uint32_t,uint32_t>> pointIndices;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        for (const auto& p : pointIndices) {
            output.Write(p.first);
            output.Write(p.second);
        }
    }
};

struct LockTagg : Tagg {
    std::vector<bool> lockedPoints;
    std::vector<bool> lockedFaces;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        for (auto b : lockedPoints) output.Write(b);
        for (auto b : lockedFaces) output.Write(b);
    }
};

struct SelectedTagg : Tagg {
    std::vector<uint8_t> weightedPoints;
    std::vector<uint8_t> faces;

    void write(BinaryWriterEx& output) const override {
        output.Write(true);
        output.writeAsciiz(Name);
        output.Write(DataSize);
        for (auto b : weightedPoints) output.Write(b);
        for (auto b : faces) output.Write(b);
    }
};

// MLOD LOD
struct MLOD_LOD {
    float resolution = 0.f;
    uint32_t unk1 = 0;
    std::vector<Point> points;
    std::vector<Vec3> normals;
    std::vector<Face> faces;
    std::vector<std::shared_ptr<Tagg>> taggs;

    MLOD_LOD() = default;
    explicit MLOD_LOD(float res) : resolution(res) {}

    void write(BinaryWriterEx& output) const {
        int nPoints = (int)points.size();
        int nNormals = (int)normals.size();
        int nFaces = (int)faces.size();

        output.writeAscii("P3DM", 4);
        output.Write((int32_t)28);
        output.Write((int32_t)256);
        output.Write((int32_t)nPoints);
        output.Write((int32_t)nNormals);
        output.Write((int32_t)nFaces);
        output.Write(unk1);

        for (int i = 0; i < nPoints; i++) points[static_cast<size_t>(i)].write(output);
        for (int i = 0; i < nNormals; i++) output.writeVec3(normals[static_cast<size_t>(i)]);
        for (int i = 0; i < nFaces; i++) faces[static_cast<size_t>(i)].write(output);

        output.writeAscii("TAGG", 4);
        for (const auto& tagg : taggs) {
            tagg->write(output);
        }

        // End of file tagg
        output.Write(true);
        output.writeAsciiz("#EndOfFile#");
        output.Write((int32_t)0);
        output.Write(resolution);
    }
};

// MLOD top-level
struct MLOD_File {
    std::vector<MLOD_LOD> lods;

    MLOD_File() = default;
    explicit MLOD_File(std::vector<MLOD_LOD>&& l) : lods(std::move(l)) {}

    void write(BinaryWriterEx& output) const {
        output.writeAscii("MLOD", 4);
        output.Write((int32_t)257);
        output.Write((int32_t)lods.size());
        for (const auto& lod : lods) {
            lod.write(output);
        }
    }

    void writeToFile(const std::string& path) const {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) throw std::runtime_error("Cannot create output file: " + path);
        BinaryWriterEx writer(ofs);
        write(writer);
        ofs.close();
    }
};

}}} // BisDll::Model::MLOD

// ============================================================================
// BisDll.Model - Conversion (ODOL to MLOD)
// ============================================================================
namespace BisDll { namespace Model { namespace Conversion {

using namespace BisDll::Model::ODOL;
using namespace BisDll::Model::MLOD;
using namespace BisDll::Model;

struct PointWeight {
    int pointIndex;
    uint8_t weight;
    PointWeight(int idx = 0, uint8_t w = 0) : pointIndex(idx), weight(w) {}
};

inline bool EqualsFloat(float f1, float f2, float tol = 0.0001f) {
    return std::fabs(f1 - f2) <= tol;
}

static PointFlags clipFlagsToPointFlags(ClipFlags clipFlags) {
    PointFlags pf = PointFlags::NONE;

    if ((clipFlags & ClipFlags::ClipLandStep) != ClipFlags::ClipNone) {
        pf |= PointFlags::ONLAND;
    } else if ((clipFlags & ClipFlags::ClipLandUnder) != ClipFlags::ClipNone) {
        pf |= PointFlags::UNDERLAND;
    } else if ((clipFlags & ClipFlags::ClipLandAbove) != ClipFlags::ClipNone) {
        pf |= PointFlags::ABOVELAND;
    } else if ((clipFlags & ClipFlags::ClipLandKeep) != ClipFlags::ClipNone) {
        pf |= PointFlags::KEEPLAND;
    }

    if ((clipFlags & ClipFlags::ClipDecalStep) != ClipFlags::ClipNone) {
        pf |= PointFlags::DECAL;
    } else if ((clipFlags & ClipFlags::ClipDecalVertical) != ClipFlags::ClipNone) {
        pf |= PointFlags::VDECAL;
    }

    // Light flags (using raw values from the C# decompiled code)
    if (((int32_t)clipFlags & 209715200) != 0) {
        pf |= PointFlags::NOLIGHT;
    } else if (((int32_t)clipFlags & 212860928) != 0) {
        pf |= PointFlags::FULLLIGHT;
    } else if (((int32_t)clipFlags & 211812352) != 0) {
        pf |= PointFlags::HALFLIGHT;
    } else if (((int32_t)clipFlags & 210763776) != 0) {
        pf |= PointFlags::AMBIENT;
    }

    if ((clipFlags & ClipFlags::ClipFogStep) != ClipFlags::ClipNone) {
        pf |= PointFlags::NOFOG;
    } else if ((clipFlags & ClipFlags::ClipFogSky) != ClipFlags::ClipNone) {
        pf |= PointFlags::SKYFOG;
    }

    int userVal = ((int32_t)clipFlags & (int32_t)ClipFlags::ClipUserMask) / 1048576;
    return static_cast<PointFlags>((uint32_t)pf | (uint32_t)(65536 * userVal));
}

static void ConvertPoints(const ODOL_File& odol, MLOD_LOD& dstLod, const LOD& srcLod) {
    Vec3 boundingCenter = odol.modelInfo.boundingCenter;
    int n = (int)srcLod.vertices.size();
    dstLod.points.resize(static_cast<size_t>(n));
    const auto& clipFlags = srcLod.ClipFlagsArr();
    for (int i = 0; i < n; i++) {
        Vec3 pos = srcLod.vertices[static_cast<size_t>(i)] + boundingCenter;
        ClipFlags cf = (i < (int)clipFlags.size()) ? clipFlags[static_cast<size_t>(i)] : ClipFlags::ClipNone;
        dstLod.points[static_cast<size_t>(i)] = Point(pos, clipFlagsToPointFlags(cf));
    }
}

static void ConvertFaces(const ODOL_File& odol, MLOD_LOD& dstLod, const LOD& srcLod) {
    (void)odol;
    std::vector<MLOD::Face> faceList;
    faceList.reserve(static_cast<size_t>(srcLod.VertexCount() * 2));

    // Cache UV data outside the loop (property getter recomputes each call in C#)
    auto uvData = srcLod.uvSets[0].getUVData();

    for (const auto& section : srcLod.sections) {
        auto faceIndexes = section.getFaceIndexes(srcLod.Faces());

        for (uint32_t fi : faceIndexes) {
            const auto& poly = srcLod.Faces()[fi];
            int nv = (int)poly.VertexIndices.size();
            MLOD::Vertex verts[4];
            for (int k = 0; k < nv; k++) {
                int vi = poly.VertexIndices[static_cast<size_t>(nv - 1 - k)];
                verts[static_cast<size_t>(k)] = MLOD::Vertex(vi, vi, uvData[static_cast<size_t>(vi*2)], uvData[static_cast<size_t>(vi*2+1)]);
            }
            std::string texture = (section.textureIndex == -1) ? "" : srcLod.textures[static_cast<size_t>(section.textureIndex)];
            std::string material = (section.materialIndex == -1) ? "" : srcLod.materials[static_cast<size_t>(section.materialIndex)].materialName;
            faceList.emplace_back(nv, verts, FaceFlags::DEFAULT, texture, material);
        }
    }
    dstLod.faces = std::move(faceList);
}

static void ReconstructNamedSelectionBySections(const LOD& src,
    std::map<std::string, std::vector<PointWeight>>& points,
    std::map<std::string, std::vector<int>>& faces)
{
    for (const auto& ns : src.namedSelections) {
        if (!ns.IsSectional) continue;
        std::vector<int> faceIdxs;
        std::vector<PointWeight> ptWeights;
        for (int si : ns.Sections) {
            auto sectionFaces = src.sections[static_cast<size_t>(si)].getFaceIndexes(src.Faces());
            for (uint32_t fi : sectionFaces) {
                faceIdxs.push_back((int)fi);
                for (int vi : src.Faces()[fi].VertexIndices) {
                    ptWeights.emplace_back(vi, 255);
                }
            }
        }
        faces[ns.Name] = std::move(faceIdxs);
        points[ns.Name] = std::move(ptWeights);
    }
}

static void ReconstructProxies(const LOD& src,
    std::map<std::string, std::vector<PointWeight>>& points,
    std::map<std::string, std::vector<int>>& faces)
{
    for (int i = 0; i < (int)src.Faces().size(); i++) {
        const auto& polygon = src.Faces()[static_cast<size_t>(i)];
        if ((int)polygon.VertexIndices.size() != 3) continue;

        int vi0 = polygon.VertexIndices[0];
        int vi1 = polygon.VertexIndices[1];
        int vi2 = polygon.VertexIndices[2];

        Vec3 v = src.vertices[static_cast<size_t>(vi0)];
        Vec3 v2 = src.vertices[static_cast<size_t>(vi1)];
        Vec3 v3 = src.vertices[static_cast<size_t>(vi2)];

        float d01 = v.Distance(v2);
        float d02 = v.Distance(v3);
        float d12 = v2.Distance(v3);

        if (d01 > d02) { std::swap(v2, v3); std::swap(d01, d02); }
        if (d01 > d12) { std::swap(v, v3); std::swap(d01, d12); }
        if (d02 > d12) { std::swap(v, v2); std::swap(d02, d12); }

        Vec3 origin = v;
        Vec3 dirVec = v2 - v;
        Vec3 upVec = v3 - v;
        dirVec.Normalize();
        upVec.Normalize();

        if (!EqualsFloat(upVec.dot(dirVec), 0.f, 0.05f)) continue;

        for (int j = 0; j < (int)src.proxies.size(); j++) {
            Vec3 proxyPos = src.proxies[static_cast<size_t>(j)].transformation.Position();
            Vec3 proxyUp = src.proxies[static_cast<size_t>(j)].transformation.Orientation().Up();
            Vec3 proxyDir = src.proxies[static_cast<size_t>(j)].transformation.Orientation().Dir();

            if (origin.Equals(proxyPos) && dirVec.Equals(proxyDir) && upVec.Equals(proxyUp)) {
                int nsIdx = src.proxies[static_cast<size_t>(j)].namedSelectionIndex;
                const std::string& name = src.namedSelections[static_cast<size_t>(nsIdx)].Name;
                if (faces.find(name) == faces.end()) {
                    faces[name] = {i};
                    points[name] = {
                        PointWeight(vi0, 255),
                        PointWeight(vi1, 255),
                        PointWeight(vi2, 255)
                    };
                    break;
                }
            }
        }
    }
}

static void ReconstructNamedSelectionsByBones(const LOD& src, const Skeleton& skeleton,
    std::map<std::string, std::vector<PointWeight>>& points)
{
    if (src.vertexBoneRef.empty()) return;

    uint16_t vertIdx = 0;
    for (const auto& boneRef : src.vertexBoneRef) {
        auto pairs = boneRef.getAnimationRTPairs();
        for (const auto& pair : pairs) {
            uint8_t selIdx = pair.SelectionIndex;
            uint8_t weight = pair.Weight;
            int skelIdx = src.subSkeletonsToSkeleton[selIdx];
            const std::string& boneName = skeleton.bones[static_cast<size_t>(skelIdx * 2)];
            PointWeight pw(vertIdx, weight);
            points[boneName].push_back(pw);
        }
        vertIdx++;
    }
}

static void ApplySelectedPointsAndFaces(MLOD_LOD& dstLod,
    const std::map<std::string, std::vector<PointWeight>>& nsPoints,
    const std::map<std::string, std::vector<int>>* nsFaces)
{
    for (auto& tagg : dstLod.taggs) {
        auto* nst = dynamic_cast<NamedSelectionTagg*>(tagg.get());
        if (!nst) continue;

        auto it = nsPoints.find(nst->Name);
        if (it != nsPoints.end()) {
            for (const auto& pw : it->second) {
                uint8_t b = (uint8_t)(-(int8_t)pw.weight);  // C# (byte)(-item.weight)
                if (b != 0) {
                    nst->points[static_cast<size_t>(pw.pointIndex)] = b;
                }
            }
        }

        if (nsFaces) {
            auto it2 = nsFaces->find(nst->Name);
            if (it2 != nsFaces->end()) {
                for (int fi : it2->second) {
                    nst->faces[static_cast<size_t>(fi)] = 1;
                }
            }
        }
    }
}

static std::vector<std::shared_ptr<NamedSelectionTagg>> createNamedSelectionTaggs(const LOD& src) {
    int nPoints = src.VertexCount();
    int nFaces = (int)src.Faces().size();
    std::vector<std::shared_ptr<NamedSelectionTagg>> result;

    for (const auto& ns : src.namedSelections) {
        auto tagg = std::make_shared<NamedSelectionTagg>();
        tagg->Name = ns.Name;
        tagg->DataSize = (uint32_t)(nPoints + nFaces);
        tagg->points.assign(static_cast<size_t>(nPoints), 0);
        tagg->faces.assign(static_cast<size_t>(nFaces), 0);

        bool hasWeights = !ns.SelectedVerticesWeights.empty();
        int wIdx = 0;
        for (int vi : ns.SelectedVertices) {
            uint8_t b;
            if (!hasWeights) {
                b = 1;
            } else {
                b = (uint8_t)(-(int8_t)ns.SelectedVerticesWeights[static_cast<size_t>(wIdx++)]);
            }
            tagg->points[static_cast<size_t>(vi)] = b;
        }
        for (int fi : ns.SelectedFaces) {
            tagg->faces[static_cast<size_t>(fi)] = 1;
        }
        result.push_back(tagg);
    }
    return result;
}

static std::vector<std::shared_ptr<AnimationTagg>> createAnimTaggs(const LOD& src) {
    std::vector<std::shared_ptr<AnimationTagg>> result;
    for (const auto& kf : src.frames) {
        int n = (int)kf.points.size();
        auto tagg = std::make_shared<AnimationTagg>();
        tagg->Name = "#Animation#";
        tagg->DataSize = (uint32_t)(n * 12 + 4);
        tagg->frameTime = kf.time;
        tagg->framePoints = kf.points;
        result.push_back(tagg);
    }
    return result;
}

static std::shared_ptr<MassTagg> createMassTagg(int nPoints, float totalMass) {
    auto tagg = std::make_shared<MassTagg>();
    tagg->Name = "#Mass#";
    tagg->DataSize = (uint32_t)(nPoints * 4);
    tagg->mass.resize(static_cast<size_t>(nPoints));
    float perPoint = totalMass / (float)nPoints;
    for (int i = 0; i < nPoints; i++) tagg->mass[static_cast<size_t>(i)] = perPoint;
    return tagg;
}

static std::vector<std::shared_ptr<UVSetTagg>> createUVSetTaggs(const LOD& src) {
    int nFaces = (int)src.Faces().size();
    std::vector<std::shared_ptr<UVSetTagg>> result;

    for (uint32_t s = 0; s < (uint32_t)src.uvSets.size(); s++) {
        auto tagg = std::make_shared<UVSetTagg>();
        tagg->Name = "#UVSet#";
        tagg->uvSetNr = s;
        tagg->faceUVs.resize(static_cast<size_t>(nFaces));

        auto uvData = src.uvSets[s].getUVData();
        uint32_t dataSize = 4;

        for (int j = 0; j < nFaces; j++) {
            const auto& poly = src.Faces()[static_cast<size_t>(j)];
            int nv = (int)poly.VertexIndices.size();
            tagg->faceUVs[static_cast<size_t>(j)].resize(static_cast<size_t>(nv));
            for (int k = 0; k < nv; k++) {
                int vi = poly.VertexIndices[static_cast<size_t>(nv - 1 - k)];
                tagg->faceUVs[static_cast<size_t>(j)][static_cast<size_t>(k)] = {uvData[static_cast<size_t>(vi*2)], uvData[static_cast<size_t>(vi*2+1)]};
                dataSize += 8;
            }
        }
        tagg->DataSize = dataSize;
        result.push_back(tagg);
    }
    return result;
}

static std::vector<std::shared_ptr<PropertyTagg>> createPropertyTaggs(const LOD& src) {
    std::vector<std::shared_ptr<PropertyTagg>> result;
    for (uint32_t i = 0; i < src.nNamedProperties; i++) {
        auto tagg = std::make_shared<PropertyTagg>();
        tagg->Name = "#Property#";
        tagg->DataSize = 128;
        tagg->name = src.namedPropertiesFlat[i*2];
        tagg->value = src.namedPropertiesFlat[i*2+1];
        result.push_back(tagg);
    }
    return result;
}

static MLOD_LOD OdolLod2MLOD(const ODOL_File& odol, const LOD& src) {
    MLOD_LOD dst(src.resolution);
    int vertexCount = src.VertexCount();

    ConvertPoints(odol, dst, src);
    dst.normals = src.normals;
    ConvertFaces(odol, dst, src);

    float mass = odol.modelInfo.mass;

    // Taggs
    if (src.resolution == Resolution::GEOMETRY) {
        dst.taggs.push_back(createMassTagg(vertexCount, mass));
    }

    for (auto& t : createUVSetTaggs(src)) dst.taggs.push_back(t);
    for (auto& t : createPropertyTaggs(src)) dst.taggs.push_back(t);
    for (auto& t : createNamedSelectionTaggs(src)) dst.taggs.push_back(t);
    for (auto& t : createAnimTaggs(src)) dst.taggs.push_back(t);

    if (Resolution::KeepsNamedSelections(src.resolution)) {
        return dst;
    }

    // Reconstruct named selections by sections
    std::map<std::string, std::vector<PointWeight>> sectionPoints;
    std::map<std::string, std::vector<int>> sectionFaces;
    ReconstructNamedSelectionBySections(src, sectionPoints, sectionFaces);

    // Reconstruct proxies
    std::map<std::string, std::vector<PointWeight>> proxyPoints;
    std::map<std::string, std::vector<int>> proxyFaces;
    ReconstructProxies(src, proxyPoints, proxyFaces);

    // Reconstruct by bones
    std::map<std::string, std::vector<PointWeight>> bonePoints;
    ReconstructNamedSelectionsByBones(src, odol.modelInfo.skeleton, bonePoints);

    ApplySelectedPointsAndFaces(dst, sectionPoints, &sectionFaces);
    ApplySelectedPointsAndFaces(dst, proxyPoints, &proxyFaces);
    ApplySelectedPointsAndFaces(dst, bonePoints, nullptr);

    return dst;
}

static MLOD_File ODOL2MLOD(const ODOL_File& odol) {
    int nLods = odol.nLods;
    std::vector<MLOD_LOD> mlodLods(static_cast<size_t>(nLods));
    for (int i = 0; i < nLods; i++) {
        std::cerr << "  Converting LOD " << (i+1) << "/" << nLods
                  << " (" << Resolution::getLODName(odol.resolutions[static_cast<size_t>(i)]) << ")" << std::endl;
        mlodLods[static_cast<size_t>(i)] = OdolLod2MLOD(odol, odol.lods[static_cast<size_t>(i)]);
    }
    return MLOD_File(std::move(mlodLods));
}

// ---- ODOL v7 (OFP/CWA) conversion ----

static PointFlags convertPointLightFlagsV7(uint32_t flags_odol) {
    uint32_t flags_mlod = 0u;

    if ((flags_odol >> 20) == 0xC8) flags_mlod |= 0x10;  // Shining / NOLIGHT
    if ((flags_odol >> 20) == 0xC9) flags_mlod |= 0x20;  // Always in shadow / AMBIENT
    if ((flags_odol >> 20) == 0xCA) flags_mlod |= 0x80;  // Half lighted / HALFLIGHT
    if ((flags_odol >> 20) == 0xCB) flags_mlod |= 0x40;  // Fully lighted / FULLLIGHT

    if (flags_mlod == 0)
        flags_mlod |= (flags_odol >> 4) & 0xFF0000u; // User value

    flags_mlod |= (flags_odol >> 8) & 0xFu;    // Surface (land flags)
    flags_mlod |= (flags_odol >> 4) & 0x300u;  // Decal
    flags_mlod |= (flags_odol >> 2) & 0x3000u; // Fog

    return static_cast<PointFlags>(flags_mlod);
}

static int32_t convertFaceFlagsV7(uint32_t flags_odol) {
    int32_t flags_mlod = 0;

    if ((flags_odol & 0x40) != 0u) flags_mlod |= 0x8;        // unknown
    if ((flags_odol & 0x20) != 0u) flags_mlod |= 0x10;       // shadow off
    if ((flags_odol & 0x4000000) != 0u) flags_mlod |= 0x100;  // zbias low
    if ((flags_odol & 0x8000000) != 0u) flags_mlod |= 0x200;  // zbias middle
    if ((flags_odol & 0xC000000) != 0u) flags_mlod |= 0x300;  // zbias high
    if ((flags_odol & 0x20000000) != 0u) flags_mlod |= 0x1000000; // texture merging off

    return flags_mlod;
}

static MLOD_LOD OdolV7Lod2MLOD(const ODOL7::ODOL7_File& odol, const ODOL7::LOD7& src,
                                 float resolution, int lodIndex) {
    MLOD_LOD dst(resolution);
    int nPositions = (int)src.positions.size();

    // Points: position + lodCenter offset, converted flags
    dst.points.resize(static_cast<size_t>(nPositions));
    for (int i = 0; i < nPositions; i++) {
        Vec3 pos = src.positions[static_cast<size_t>(i)] + odol.lodCenter;
        PointFlags pf = PointFlags::NONE;
        if (i < (int)src.vertFlags.size()) {
            pf = convertPointLightFlagsV7(src.vertFlags[static_cast<size_t>(i)]);
        }
        dst.points[static_cast<size_t>(i)] = Point(pos, pf);
    }

    // Normals: write per-face normals (each face vertex gets its own normal)
    // Count total normals needed
    uint32_t normalCount = 0;
    for (const auto& face : src.faces) {
        normalCount += face.IsQuad() ? 4u : 3u;
    }
    dst.normals.resize(normalCount);
    {
        uint32_t ni = 0;
        for (const auto& face : src.faces) {
            dst.normals[ni++] = src.normals[face.v0];
            dst.normals[ni++] = src.normals[face.v1];
            dst.normals[ni++] = src.normals[face.v2];
            if (face.IsQuad()) {
                dst.normals[ni++] = src.normals[face.v3];
            }
        }
    }

    // Faces: convert with vertex order matching odol2mlod reference
    // Reference order: tri = v1, v0, v2; quad = v1, v0, v3, v2
    dst.faces.reserve(src.faces.size());
    {
        uint32_t normalIndex = 0;
        for (const auto& face : src.faces) {
            MLOD::Face mf;
            std::string texture;
            if (face.textureIndex < (uint16_t)src.textureNames.size()) {
                texture = src.textureNames[face.textureIndex];
            }

            mf.Flags = static_cast<FaceFlags>(convertFaceFlagsV7(face.flags));
            mf.Texture = texture;
            mf.Material = "";

            if (face.IsQuad()) {
                mf.NumberOfVertices = 4;
                mf.Vertices[0] = Vertex(face.v1, (int)normalIndex + 1, src.uvData[static_cast<size_t>(face.v1 * 2)], src.uvData[static_cast<size_t>(face.v1 * 2 + 1)]);
                mf.Vertices[1] = Vertex(face.v0, (int)normalIndex + 0, src.uvData[static_cast<size_t>(face.v0 * 2)], src.uvData[static_cast<size_t>(face.v0 * 2 + 1)]);
                mf.Vertices[2] = Vertex(face.v3, (int)normalIndex + 3, src.uvData[static_cast<size_t>(face.v3 * 2)], src.uvData[static_cast<size_t>(face.v3 * 2 + 1)]);
                mf.Vertices[3] = Vertex(face.v2, (int)normalIndex + 2, src.uvData[static_cast<size_t>(face.v2 * 2)], src.uvData[static_cast<size_t>(face.v2 * 2 + 1)]);
                normalIndex += 4;
            } else {
                mf.NumberOfVertices = 3;
                mf.Vertices[0] = Vertex(face.v1, (int)normalIndex + 1, src.uvData[static_cast<size_t>(face.v1 * 2)], src.uvData[static_cast<size_t>(face.v1 * 2 + 1)]);
                mf.Vertices[1] = Vertex(face.v0, (int)normalIndex + 0, src.uvData[static_cast<size_t>(face.v0 * 2)], src.uvData[static_cast<size_t>(face.v0 * 2 + 1)]);
                mf.Vertices[2] = Vertex(face.v2, (int)normalIndex + 2, src.uvData[static_cast<size_t>(face.v2 * 2)], src.uvData[static_cast<size_t>(face.v2 * 2 + 1)]);
                normalIndex += 3;
            }

            dst.faces.push_back(mf);
        }
    }

    // Taggs

    // #Mass# for geometry LOD
    if (lodIndex == (int)odol.geometryLodIndex && !odol.masses.empty()) {
        auto tagg = std::make_shared<MassTagg>();
        tagg->Name = "#Mass#";
        if (odol.masses.size() == (size_t)nPositions) {
            tagg->mass.assign(odol.masses.begin(), odol.masses.end());
        } else {
            // Distribute mass evenly
            tagg->mass.resize(static_cast<size_t>(nPositions));
            float perPoint = odol.mass / (float)nPositions;
            for (int i = 0; i < nPositions; i++) tagg->mass[static_cast<size_t>(i)] = perPoint;
        }
        tagg->DataSize = (uint32_t)(nPositions * 4);
        dst.taggs.push_back(tagg);
    }

    // #Property# taggs
    for (const auto& prop : src.namedProperties) {
        auto tagg = std::make_shared<PropertyTagg>();
        tagg->Name = "#Property#";
        tagg->DataSize = 128;
        tagg->name = prop.name;
        tagg->value = prop.value;
        dst.taggs.push_back(tagg);
    }

    // Named selection taggs
    {
        int nFaces = (int)src.faces.size();
        for (const auto& ns : src.namedSections) {
            auto tagg = std::make_shared<NamedSelectionTagg>();
            tagg->Name = ns.name;
            tagg->DataSize = (uint32_t)(nPositions + nFaces);
            tagg->points.assign(static_cast<size_t>(nPositions), 0);
            tagg->faces.assign(static_cast<size_t>(nFaces), 0);

            // Vertex weights
            if (ns.vertexWeights.empty()) {
                for (uint16_t vi : ns.vertexIndices) {
                    if (vi < nPositions) tagg->points[vi] = 0x01;
                }
            } else {
                for (size_t i = 0; i < ns.vertexIndices.size() && i < ns.vertexWeights.size(); i++) {
                    uint16_t vi = ns.vertexIndices[i];
                    if (vi < nPositions) {
                        tagg->points[vi] = (uint8_t)(-(int8_t)ns.vertexWeights[i]);
                    }
                }
            }

            // Face indices
            for (uint16_t fi : ns.faceIndices) {
                if (fi < nFaces) tagg->faces[fi] = 1;
            }

            dst.taggs.push_back(tagg);
        }
    }

    // #Animation# taggs
    for (const auto& anim : src.animationPhases) {
        auto tagg = std::make_shared<AnimationTagg>();
        tagg->Name = "#Animation#";
        tagg->DataSize = (uint32_t)(anim.points.size() * 12 + 4);
        tagg->frameTime = anim.time;
        tagg->framePoints = anim.points;
        dst.taggs.push_back(tagg);
    }

    return dst;
}

static MLOD_File ODOL7toMLOD(const ODOL7::ODOL7_File& odol) {
    int nLods = (int)odol.lodCount;
    std::vector<MLOD_LOD> mlodLods(static_cast<size_t>(nLods));
    for (int i = 0; i < nLods; i++) {
        float res = odol.lodDistances[static_cast<size_t>(i)].graphical;
        std::cerr << "  Converting LOD " << (i + 1) << "/" << nLods
                  << " (" << Resolution::getLODName(res) << ")" << std::endl;
        mlodLods[static_cast<size_t>(i)] = OdolV7Lod2MLOD(odol, odol.lods[static_cast<size_t>(i)], res, i);
    }
    return MLOD_File(std::move(mlodLods));
}

}}} // BisDll::Model::Conversion

// ============================================================================
// Main - odol2mlod
// ============================================================================

static bool convertP3dFile(const std::string& srcPath, const std::string& dstPath = "") {
    std::cerr << "Reading the p3d ('" << srcPath << "')..." << std::endl;

    std::ifstream ifs(srcPath, std::ios::binary);
    if (!ifs) {
        std::cerr << "Cannot open file: " << srcPath << std::endl;
        return false;
    }

    // Read signature
    char sig[4];
    ifs.read(sig, 4);
    ifs.seekg(0);
    std::string sigStr(sig, 4);

    if (sigStr == "MLOD") {
        std::cerr << "'" << srcPath << "' is already in editable MLOD format" << std::endl;
        return false;
    }

    if (sigStr != "ODOL") {
        std::cerr << "'" << srcPath << "' is not a valid P3D file (unknown signature: " << sigStr << ")" << std::endl;
        return false;
    }

    // Peek at version to decide which reader to use
    BinaryReaderEx reader(ifs);
    reader.ReadAscii(4); // consume "ODOL" signature
    uint32_t odolVersion = reader.ReadUInt32();
    ifs.seekg(0); // rewind

    // Identify probable game from ODOL version
    {
        const char* game = "Unknown";
        if (odolVersion <= 7)        game = "Operation Flashpoint / Cold War Assault";
        else if (odolVersion < 28)   game = "Unknown (transitional)";
        else if (odolVersion <= 39)  game = "Arma: Armed Assault";
        else if (odolVersion <= 48)  game = "Arma 2";
        else if (odolVersion <= 58)  game = "Arma 2: Operation Arrowhead";
        else if (odolVersion <= 75)  game = "Arma 3";
        std::cerr << "ODOL v" << odolVersion << " detected (" << game << ")." << std::endl;
    }

    if (odolVersion >= 8 && odolVersion < 28) {
        std::cerr << "ODOL v" << odolVersion << " is not supported." << std::endl;
        std::cerr << "  (Versions 8-27 are transitional formats between OFP and Arma.)" << std::endl;
        std::cerr << "  Supported: v7 (OFP/CWA) and v28+ (Arma series)." << std::endl;
        return false;
    }

    BisDll::Model::MLOD::MLOD_File mlod;

    if (odolVersion <= 7) {
        // ODOL v7 (OFP/CWA) path
        reader.ReadAscii(4); // consume "ODOL"
        BisDll::Model::ODOL7::ODOL7_File odol7;
        odol7.version = reader.ReadUInt32();
        odol7.read(reader);
        ifs.close();

        std::cerr << "ODOL v" << odol7.version << " (OFP/CWA) loaded successfully ("
                  << odol7.lodCount << " LODs)." << std::endl;
        std::cerr << "Start conversion..." << std::endl;

        mlod = BisDll::Model::Conversion::ODOL7toMLOD(odol7);
    } else {
        // ODOL v28+ (Arma series) path
        ifs.seekg(0);
        BinaryReaderEx reader2(ifs);
        BisDll::Model::ODOL::ODOL_File odol;
        odol.read(reader2);
        ifs.close();

        std::cerr << "ODOL v" << odol.version << " loaded successfully ("
                  << odol.nLods << " LODs)." << std::endl;
        std::cerr << "Start conversion..." << std::endl;

        mlod = BisDll::Model::Conversion::ODOL2MLOD(odol);
    }

    std::cerr << "Conversion successful." << std::endl;

    std::string outputPath = dstPath;
    if (outputPath.empty()) {
        fs::path p(srcPath);
        fs::path dir = p.parent_path();
        std::string stem = p.stem().string();
        outputPath = (dir / (stem + "_mlod.p3d")).string();
    }

    std::cerr << "Saving..." << std::endl;
    mlod.writeToFile(outputPath);
    std::cerr << "MLOD successfully saved to '" << outputPath << "'" << std::endl;

    return true;
}

static void convertP3dFiles(const std::vector<std::string>& srcFiles, const std::string& dstFolder) {
    std::cerr << "Start conversion of " << srcFiles.size() << " p3d files:" << std::endl;
    int failures = 0;
    for (const auto& src : srcFiles) {
        std::string dstPath;
        if (!dstFolder.empty()) {
            dstPath = (fs::path(dstFolder) / fs::path(src).filename()).string();
        }
        if (!convertP3dFile(src, dstPath)) {
            failures++;
        }
    }
    if (failures == 0) {
        std::cerr << "Conversions finished successfully." << std::endl;
    } else {
        std::cerr << failures << " conversion(s) were not successful." << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cerr << "===============================" << std::endl;
    std::cerr << " p3d_odol2mlod (C++)" << std::endl;
    std::cerr << " Arma 3 (ODOL v28-75)" << std::endl;
    std::cerr << " OFP/CWA  (ODOL v7)" << std::endl;
    std::cerr << " Based on BisDll by T_D" << std::endl;
    std::cerr << " OFP support based on" << std::endl;
    std::cerr << "  github.com/Faguss/odol2mlod" << std::endl;
    std::cerr << "===============================" << std::endl;

    const char* usage =
        "p3d_odol2mlod converts binarized p3d models (ODOL format) to editable MLOD format.\n"
        "Supports Arma 3/2/1 (ODOL v28+) and OFP/CWA (ODOL v7).\n\n"
        "Usage:\n"
        "  p3d_odol2mlod path/model.p3d                - converts the given p3d\n"
        "  p3d_odol2mlod inputFolder [outputFolder]     - converts all p3d in inputFolder\n";

    std::cerr << usage << std::endl;

    try {
        if (argc < 2) {
            std::cerr << "Error: Please provide a p3d file or folder as argument." << std::endl;
            return 1;
        }

        if (argc == 2) {
            std::string path = argv[1];
            if (fs::is_regular_file(path)) {
                if (fs::path(path).extension() == ".p3d") {
                    convertP3dFile(fs::absolute(path).string());
                } else {
                    std::cerr << "The file '" << path << "' does not have the .p3d extension." << std::endl;
                }
            } else if (fs::is_directory(path)) {
                std::vector<std::string> files;
                for (const auto& entry : fs::directory_iterator(path)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".p3d") {
                        files.push_back(entry.path().string());
                    }
                }
                convertP3dFiles(files, "");
            } else {
                std::cerr << "The file or directory '" << path << "' was not found." << std::endl;
            }
        } else if (argc == 3) {
            std::string inputDir = argv[1];
            std::string outputDir = argv[2];
            if (!fs::is_directory(inputDir)) {
                std::cerr << "The folder '" << inputDir << "' does not exist." << std::endl;
                return 1;
            }
            if (!fs::is_directory(outputDir)) {
                std::cerr << "The folder '" << outputDir << "' does not exist." << std::endl;
                return 1;
            }
            std::vector<std::string> files;
            for (const auto& entry : fs::directory_iterator(inputDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".p3d") {
                    files.push_back(entry.path().string());
                }
            }
            convertP3dFiles(files, outputDir);
        } else {
            std::cerr << usage << std::endl;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
