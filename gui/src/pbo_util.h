#pragma once

#include "config.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

// Extract a single file entry from a PBO archive on disk.
// Returns the raw bytes of the entry, or an empty vector on failure.
std::vector<uint8_t> extract_from_pbo(const std::string& pbo_path,
                                       const std::string& entry_name);

// Run a subprocess safely using fork/exec (no shell interpretation).
// Returns {exit_status, captured_output}.  exit_status is -1 on fork failure.
struct SubprocessResult {
    int status = -1;
    std::string output;
};
using OutputConsumer = std::function<void(std::string&& chunk)>;
SubprocessResult run_subprocess(const std::string& program,
                                 const std::vector<std::string>& args,
                                 OutputConsumer consumer = {});
std::vector<std::string> apply_tool_verbosity(const Config* cfg,
                                              std::vector<std::string> args,
                                              bool supports_flags = false);

// Resolve a texture path to a file on disk (drive_root or relative to model).
// Returns true if the texture exists on disk at any candidate path.
bool resolve_texture_on_disk(const std::string& texture,
                              const std::string& model_path,
                              const std::string& drive_root);
