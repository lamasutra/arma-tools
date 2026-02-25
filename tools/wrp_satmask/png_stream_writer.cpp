#include "png_stream_writer.h"

#include <cstdio>
#include <stdexcept>

namespace fs = std::filesystem;

PngStreamWriter::PngStreamWriter(const fs::path& path, int width, int height, int channels)
    : width_(width), height_(height), channels_(channels) {
    if (width <= 0 || height <= 0 || (channels != 3 && channels != 4)) {
        throw std::invalid_argument("invalid PNG dimensions or channels");
    }

    file_ = std::fopen(path.string().c_str(), "wb");
    if (!file_) {
        throw std::runtime_error("png: cannot open output file");
    }

    png_ptr_ = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr_) {
        std::fclose(file_);
        throw std::runtime_error("png: png_create_write_struct failed");
    }

    info_ptr_ = png_create_info_struct(png_ptr_);
    if (!info_ptr_) {
        png_destroy_write_struct(&png_ptr_, nullptr);
        std::fclose(file_);
        throw std::runtime_error("png: png_create_info_struct failed");
    }

    if (setjmp(png_jmpbuf(png_ptr_))) {
        throw std::runtime_error("png: initialization failed");
    }

    png_init_io(png_ptr_, file_);
    png_set_IHDR(png_ptr_, info_ptr_, width_, height_, 8,
                 channels_ == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr_, info_ptr_);
}

PngStreamWriter::~PngStreamWriter() {
    try {
        finish();
    } catch (...) {
    }
    if (png_ptr_) {
        png_destroy_write_struct(&png_ptr_, &info_ptr_);
        png_ptr_ = nullptr;
        info_ptr_ = nullptr;
    }
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void PngStreamWriter::write_row(std::span<const uint8_t> row) {
    if (row.size() != static_cast<size_t>(width_) * static_cast<size_t>(channels_)) {
        throw std::runtime_error("png: row size mismatch");
    }
    ensure_initialized();
    if (setjmp(png_jmpbuf(png_ptr_))) {
        throw std::runtime_error("png: write row failed");
    }
    png_write_row(png_ptr_, const_cast<png_bytep>(row.data()));
}

void PngStreamWriter::finish() {
    if (finished_ || !png_ptr_) return;
    if (setjmp(png_jmpbuf(png_ptr_))) {
        throw std::runtime_error("png: write end failed");
    }
    png_write_end(png_ptr_, info_ptr_);
    finished_ = true;
}

void PngStreamWriter::ensure_initialized() {
    if (!png_ptr_ || !info_ptr_) {
        throw std::runtime_error("png: writer not initialized");
    }
}
