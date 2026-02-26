#include "gl_error_log.h"

#include "log_panel.h"

#include <epoxy/gl.h>

#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace {

std::string gl_error_name(GLenum err) {
    switch (err) {
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
    case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
#endif
    default: return "GL_UNKNOWN_ERROR";
    }
}

std::mutex g_gl_error_mutex;
std::unordered_map<std::string, uint64_t> g_gl_error_counts;

void log_throttled_error(const std::string& scope, GLenum err) {
    const std::string key = scope + "|" + std::to_string(static_cast<unsigned>(err));
    std::lock_guard<std::mutex> lock(g_gl_error_mutex);
    uint64_t& count = g_gl_error_counts[key];
    count++;
    if (count <= 3 || (count % 100) == 0) {
        std::ostringstream hs;
        hs << "0x" << std::hex << static_cast<unsigned>(err);
        app_log(LogLevel::Error,
                "OpenGL error in " + scope + ": " + gl_error_name(err)
                + " (" + hs.str() + ")"
                + " count=" + std::to_string(count));
    }
}

} // namespace

bool log_gl_errors(const char* scope) {
    bool had_error = false;
    const std::string where = scope ? scope : "unknown";
    for (;;) {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR) break;
        had_error = true;
        log_throttled_error(where, err);
    }
    return had_error;
}
