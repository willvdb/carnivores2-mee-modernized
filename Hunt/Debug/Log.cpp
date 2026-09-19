// ==========================================================================
// Log.cpp — Structured logging implementation
// ==========================================================================

#include "Hunt.h"
#include "Log.h"
#include "Session/Session.h"

#include <cstdio>
#include <cstdarg>
#include <ctime>

static FILE* g_logFile = nullptr;

void LogInit(const char* filename)
{
    if (g_logFile) Platform::CloseTextFile(g_logFile);
    g_logFile = Platform::OpenTextFile(filename, "a");
    if (g_logFile) {
        LOG_INFO("Log started");
    }
}

void LogWrite(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    if (!g_logFile) return;

    // Timestamp
    time_t now;
    time(&now);
    struct tm* t = localtime(&now);

    // Severity label
    const char* label = "?";
    switch (level) {
        case LogLevel::Debug: label = "DEBUG"; break;
        case LogLevel::Info:  label = "INFO";  break;
        case LogLevel::Warn:  label = "WARN";  break;
        case LogLevel::Error: label = "ERROR"; break;
    }

    // Format user message
    va_list args;
    va_start(args, fmt);
    char msg[4096];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // Write: [2026-06-29 12:34:56] [INFO] [file:line] message
    fprintf(g_logFile, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s:%d] %s\n",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec,
        label, file, line, msg);

    const bool flushed = fflush(g_logFile) == 0;
    EngineSession::Check(flushed && !ferror(g_logFile));
}

void LogClose()
{
    if (g_logFile) {
        LOG_INFO("Log closed");
        Platform::CloseTextFile(g_logFile);
        g_logFile = nullptr;
    }
}
