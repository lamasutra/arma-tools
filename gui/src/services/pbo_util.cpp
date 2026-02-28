#include "pbo_util.h"
#include "cli_logger.h"
#include "log_panel.h"
#include "cli_logger.h"

#include <armatools/pbo.h>
#include "cli_logger.h"
#include <armatools/lzss.h>
#include "cli_logger.h"
#include <armatools/armapath.h>
#include "cli_logger.h"

#include <algorithm>
#include "cli_logger.h"
#include <fstream>
#include "cli_logger.h"

#ifdef _WIN32
#include <windows.h>
#include "cli_logger.h"
#else
#include <sys/wait.h>
#include "cli_logger.h"
#include <unistd.h>
#include "cli_logger.h"
#endif

std::vector<uint8_t> extract_from_pbo(const std::string& pbo_path,
                                       const std::string& entry_name) {
    auto normalize_ci = [](std::string s) {
        std::replace(s.begin(), s.end(), '\\', '/');
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    std::ifstream f(pbo_path, std::ios::binary);
    if (!f.is_open()) return {};

    auto pbo = armatools::pbo::read(f);
    auto target = normalize_ci(entry_name);

    for (const auto& entry : pbo.entries) {
        auto name = normalize_ci(entry.filename);

        if (name == target) {
            f.seekg(static_cast<std::streamoff>(entry.data_offset));
            std::vector<uint8_t> data(entry.data_size);
            f.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
            // Decompress LZSS-compressed entries (OFP-era PBOs)
            if (entry.packing_method != 0 && entry.original_size > 0 &&
                entry.data_size != entry.original_size) {
                return armatools::lzss::decompress_buf(
                    data.data(), data.size(), entry.original_size);
            }
            return data;
        }
    }
    return {};
}

SubprocessResult run_subprocess(const std::string& program,
                                 const std::vector<std::string>& args,
                                 OutputConsumer consumer) {
    // Log the command being invoked
    {
        std::string cmdline = program;
        for (const auto& a : args) cmdline += " " + a;
        LOGD("exec: " + cmdline);
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return {-1, "CreatePipe() failed"};
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    std::string cmdline = "\"" + program + "\"";
    for (const auto& a : args) cmdline += " \"" + a + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr,
                        nullptr, &si, &pi)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return {-1, "CreateProcess() failed"};
    }

    CloseHandle(write_pipe);
    SubprocessResult result;
    char buf[512];
    DWORD n = 0;
    while (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
        std::string chunk(buf, static_cast<size_t>(n));
        result.output.append(chunk);
        if (consumer) consumer(std::move(chunk));
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.status = static_cast<int>(exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
#else
    int pipefd[2];
    if (pipe(pipefd) != 0) return {-1, "pipe() failed"};

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {-1, "fork() failed"};
    }

    if (pid == 0) {
        // Child: redirect stdout+stderr to pipe
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Build argv
        std::vector<const char*> argv;
        argv.push_back(program.c_str());
        for (const auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execvp(program.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    // Parent: read output
    close(pipefd[1]);
    SubprocessResult result;
    char buf[512];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        std::string chunk(buf, static_cast<size_t>(n));
        result.output.append(chunk);
        if (consumer) consumer(std::move(chunk));
    }
    close(pipefd[0]);

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    result.status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
    return result;
#endif
    }

std::vector<std::string> apply_tool_verbosity(const Config* cfg,
                                              std::vector<std::string> args,
                                              bool supports_flags) {
    if (!supports_flags || !cfg) return args;
    int level = cfg->tool_verbosity_level;
    if (level <= 0) return args;
    if (level >= 2) {
        args.insert(args.begin(), "-vv");
    } else {
        args.insert(args.begin(), "-v");
    }
    return args;
}

bool resolve_texture_on_disk(const std::string& texture,
                              const std::string& model_path,
                              const std::string& drive_root) {
    if (texture.empty() || armatools::armapath::is_procedural_texture(texture))
        return false;

    namespace fs = std::filesystem;
    auto normalized = armatools::armapath::to_os(texture);
    auto base_dir = fs::path(model_path).parent_path();

    std::vector<fs::path> candidates;
    candidates.push_back(base_dir / normalized);
    candidates.push_back(base_dir / normalized.filename());

    if (!drive_root.empty()) {
        candidates.push_back(fs::path(drive_root) / normalized);
    }

    // If no extension, try .paa and .pac
    auto ext_str = normalized.extension().string();
    std::transform(ext_str.begin(), ext_str.end(), ext_str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext_str.empty()) {
        for (const auto& dir : {base_dir, fs::path(drive_root)}) {
            if (dir.empty()) continue;
            auto stem = normalized;
            candidates.push_back(dir / fs::path(stem.string() + ".paa"));
            candidates.push_back(dir / fs::path(stem.string() + ".pac"));
            candidates.push_back(dir / fs::path(normalized.filename().string() + ".paa"));
            candidates.push_back(dir / fs::path(normalized.filename().string() + ".pac"));
        }
    }

    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec))
            return true;
    }
    return false;
}
