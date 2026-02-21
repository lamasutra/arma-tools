#include "process_util.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

ChildProcess::~ChildProcess() {
    stop();
}

bool ChildProcess::launch(const std::string& program, const std::vector<std::string>& args) {
    stop(); // Clean up any previous child

#ifdef _WIN32
    // Build command line: "program" "arg1" "arg2" ...
    std::string cmdline = "\"" + program + "\"";
    for (const auto& a : args)
        cmdline += " \"" + a + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi))
        return false;

    CloseHandle(pi.hThread);
    handle_ = pi.hProcess;
    return true;
#else
    pid_t pid = fork();
    if (pid == 0) {
        // Child: build argv
        std::vector<const char*> argv;
        argv.push_back(program.c_str());
        for (const auto& a : args)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execvp(program.c_str(), const_cast<char* const*>(argv.data()));
        _exit(1);
    } else if (pid > 0) {
        pid_ = pid;
        return true;
    }
    return false;
#endif
}

bool ChildProcess::running() const {
#ifdef _WIN32
    return handle_ != nullptr;
#else
    return pid_ > 0;
#endif
}

bool ChildProcess::try_reap() {
#ifdef _WIN32
    if (handle_ == nullptr) return true;
    DWORD result = WaitForSingleObject(handle_, 0);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(handle_);
        handle_ = nullptr;
        return true;
    }
    return false;
#else
    if (pid_ <= 0) return true;
    int status = 0;
    pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r > 0) {
        pid_ = 0;
        return true;
    }
    return false;
#endif
}

void ChildProcess::stop() {
#ifdef _WIN32
    if (handle_ != nullptr) {
        TerminateProcess(handle_, 1);
        WaitForSingleObject(handle_, 500);
        CloseHandle(handle_);
        handle_ = nullptr;
    }
#else
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        waitpid(pid_, nullptr, WNOHANG);
        pid_ = 0;
    }
#endif
}
