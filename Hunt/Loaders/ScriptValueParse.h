#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

// _RES.TXT values include the line ending because the parser passes the text
// after '=' directly from fgets(). The legacy engine read these fields with
// atoi()/atof(), which accept a numeric prefix and ignore everything after
// it; mods rely on that for decimal literals on integer-backed fields,
// C-style suffixes (1.0f, 7L), and stray trailing tokens. The helpers below
// preserve that prefix behavior.
//
// Two layers are provided:
//
//   ParseScriptInt/Float/LegacyInt        legacy-compatible, all-or-nothing
//   ParseScript*Status                     same parse plus a recovery value
//                                          (clamped/default) and a reason
//
// The status variants never fail to produce a memory-safe value: a missing,
// malformed, non-finite, or out-of-range scalar reports the reason and yields
// either the caller's fallback or a clamped value. Callers decide the policy
// (LoadDiagnostics.h): lenient uses the recovered value, strict rejects.

enum class ScriptScalarStatus
{
    Ok,
    Missing,     // null, empty, or whitespace-only
    Malformed,   // does not start with a number
    OutOfRange,  // overflow: `value` is clamped to the destination range
    NotFinite,   // nan/inf: `value` is the caller's fallback
};

struct ScriptIntResult
{
    ScriptScalarStatus status = ScriptScalarStatus::Missing;
    int value = 0;
};

struct ScriptFloatResult
{
    ScriptScalarStatus status = ScriptScalarStatus::Missing;
    float value = 0.0f;
};

inline const char* ScriptScalarStatusReason(ScriptScalarStatus status)
{
    switch (status)
    {
    case ScriptScalarStatus::Ok:
        return "";
    case ScriptScalarStatus::Missing:
        return "missing value";
    case ScriptScalarStatus::Malformed:
        return "non-numeric value";
    case ScriptScalarStatus::OutOfRange:
        return "value out of range";
    case ScriptScalarStatus::NotFinite:
        return "non-finite value";
    }
    return "invalid value";
}

// True when the text is only whitespace (or the line ending): that is a
// missing value, not a malformed one, and keeps the caller's fallback.
inline bool ScriptTextIsBlank(const char* text)
{
    if (!text)
        return true;
    while (*text == ' ' || *text == '\t')
        ++text;
    return *text == '\0' || *text == '\r' || *text == '\n';
}

inline ScriptIntResult ParseScriptIntStatus(const char* text, int fallback)
{
    ScriptIntResult scalar;
    scalar.value = fallback;
    if (ScriptTextIsBlank(text))
        return scalar;

    char* end = nullptr;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (end == text)
    {
        scalar.status = ScriptScalarStatus::Malformed;
        return scalar;
    }
    if (errno == ERANGE ||
        value < static_cast<long>((std::numeric_limits<int>::min)()) ||
        value > static_cast<long>((std::numeric_limits<int>::max)()))
    {
        scalar.status = ScriptScalarStatus::OutOfRange;
        scalar.value = value < 0
                           ? (std::numeric_limits<int>::min)()
                           : (std::numeric_limits<int>::max)();
        return scalar;
    }

    scalar.status = ScriptScalarStatus::Ok;
    scalar.value = static_cast<int>(value);
    return scalar;
}

inline ScriptFloatResult ParseScriptFloatStatus(const char* text, float fallback)
{
    ScriptFloatResult scalar;
    scalar.value = fallback;
    if (ScriptTextIsBlank(text))
        return scalar;

    char* end = nullptr;
    errno = 0;
    const float value = strtof(text, &end);
    if (end == text)
    {
        scalar.status = ScriptScalarStatus::Malformed;
        return scalar;
    }

    if (errno == ERANGE)
    {
        // Overflow clamps to the largest finite float; underflow keeps the
        // (finite, near-zero) value strtof produced.
        scalar.status = ScriptScalarStatus::OutOfRange;
        if (std::isfinite(value))
            scalar.value = value;
        else
            scalar.value = value > 0 ? (std::numeric_limits<float>::max)()
                                     : -(std::numeric_limits<float>::max)();
        return scalar;
    }
    if (!std::isfinite(value))
    {
        scalar.status = ScriptScalarStatus::NotFinite;
        return scalar;
    }

    scalar.status = ScriptScalarStatus::Ok;
    scalar.value = value;
    return scalar;
}

// Integer-backed gameplay fields were authored with decimal literals or a
// C-style long suffix. The old atoi/atof paths truncated those values; parse
// through strtod so `health = 13.5` keeps meaning 13.
inline ScriptIntResult ParseScriptLegacyIntStatus(const char* text, int fallback)
{
    ScriptIntResult scalar;
    scalar.value = fallback;
    if (ScriptTextIsBlank(text))
        return scalar;

    char* end = nullptr;
    errno = 0;
    const double value = strtod(text, &end);
    if (end == text)
    {
        scalar.status = ScriptScalarStatus::Malformed;
        return scalar;
    }
    if (errno == ERANGE)
    {
        scalar.status = ScriptScalarStatus::OutOfRange;
        scalar.value = value < 0.0
                           ? (std::numeric_limits<int>::min)()
                           : (std::numeric_limits<int>::max)();
        return scalar;
    }
    if (!std::isfinite(value))
    {
        scalar.status = ScriptScalarStatus::NotFinite;
        return scalar;
    }
    if (value < static_cast<double>((std::numeric_limits<int>::min)()) ||
        value > static_cast<double>((std::numeric_limits<int>::max)()))
    {
        scalar.status = ScriptScalarStatus::OutOfRange;
        scalar.value = value < 0.0
                           ? (std::numeric_limits<int>::min)()
                           : (std::numeric_limits<int>::max)();
        return scalar;
    }

    scalar.status = ScriptScalarStatus::Ok;
    scalar.value = static_cast<int>(value);
    return scalar;
}

// All-or-nothing legacy-compatible variants. `out` is written only on Ok.
inline bool ParseScriptInt(const char* text, int& out)
{
    const ScriptIntResult scalar = ParseScriptIntStatus(text, 0);
    if (scalar.status != ScriptScalarStatus::Ok)
        return false;
    out = scalar.value;
    return true;
}

inline bool ParseScriptFloat(const char* text, float& out)
{
    const ScriptFloatResult scalar = ParseScriptFloatStatus(text, 0.0f);
    if (scalar.status != ScriptScalarStatus::Ok)
        return false;
    out = scalar.value;
    return true;
}

inline bool ParseScriptLegacyInt(const char* text, int& out)
{
    const ScriptIntResult scalar = ParseScriptLegacyIntStatus(text, 0);
    if (scalar.status != ScriptScalarStatus::Ok)
        return false;
    out = scalar.value;
    return true;
}
