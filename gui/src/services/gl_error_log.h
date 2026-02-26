#pragma once

// Logs all currently pending OpenGL errors for the current context.
// Returns true when at least one error was drained.
bool log_gl_errors(const char* scope);

