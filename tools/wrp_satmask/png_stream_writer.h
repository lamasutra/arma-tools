#pragma once

#include <filesystem>
#include <span>
#include <png.h>

class PngStreamWriter {
public:
    PngStreamWriter(const std::filesystem::path& path, int width, int height, int channels);
    ~PngStreamWriter();

    void write_row(std::span<const uint8_t> row);
    void finish();

private:
    void ensure_initialized();

    FILE* file_ = nullptr;
    struct png_struct_def* png_ptr_ = nullptr;
    struct png_info_def* info_ptr_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    bool finished_ = false;
};
