#pragma once

#include <string>
#include <vector>

// Cross-platform child process handle for fire-and-forget subprocesses.
// Manages launching, monitoring (non-blocking), stopping, and cleanup.
class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Launch a subprocess. Returns true on success.
    bool launch(const std::string& program, const std::vector<std::string>& args);

    // Returns true if a child is currently running or hasn't been reaped yet.
    bool running() const;

    // Check if the child has exited (non-blocking). Returns true if exited.
    bool try_reap();

    // Send termination signal and reap. Safe to call if not running.
    void stop();

private:
#ifdef _WIN32
    void* handle_ = nullptr; // HANDLE
#else
    int pid_ = 0; // pid_t
#endif
};
