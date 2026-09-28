#pragma once

// LoadDiagnostics.h
// Data-load policy and recovery diagnostics for the text/binary loaders.
//
// Background: the v1.1.8/v1.1.9 hardening pass conflated two different
// goals -- memory safety (never index or copy out of bounds) and grammar
// strictness (reject anything the legacy parser accepted). Mod data that the
// original atoi/atof readers loaded (decimal values on integer fields,
// C-style suffixes, zero spawn ratios, long paths) started halting hunts,
// and every strictness rule had to be narrowed again after release.
//
// The policy layered here keeps memory safety unconditional while making
// value recovery explicit:
//
//   Lenient (default)  accept the legacy file, recover to a safe value
//                      (clamp/truncate/default), record a diagnostic.
//   Strict  (opt-in)   stop at the first recoverable problem. Intended for
//                      CI and mod authoring, never for players.
//
// Memory-safety failures (a value that cannot be made safe, a structurally
// malformed file) stay fatal in both modes; the policy only decides what to
// do with values that can be recovered.
//
// Sources: config.cfg `load_mode <lenient|strict>` (engine pre-read before
// _RES.TXT is parsed) and the C2_STRICT_DATA environment variable, which
// overrides the config so CI can force strict mode per machine.

#include "Core/Strings.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

enum class LoadMode
{
    Lenient,
    Strict,
};

struct LoadDiagnostic
{
    std::string group;   // component, e.g. "ScriptParser"
    std::string field;   // offending assignment or operation
    std::string reason;  // why the value was recovered
    std::string line;    // trimmed source line, when known
};

// Parse "lenient" / "strict" (case-insensitive). Unknown text returns false
// and leaves `out` untouched, so a typo keeps the current mode instead of
// silently selecting a stricter one.
inline bool ParseLoadMode(const char* text, LoadMode& out)
{
    if (!text || *text == '\0')
        return false;
    if (LegacyText::Compare(text, "strict") == 0)
    {
        out = LoadMode::Strict;
        return true;
    }
    if (LegacyText::Compare(text, "lenient") == 0 || LegacyText::Compare(text, "legacy") == 0 ||
        LegacyText::Compare(text, "compat") == 0)
    {
        out = LoadMode::Lenient;
        return true;
    }
    return false;
}

class LoadDiagnostics
{
public:
    static LoadDiagnostics& Instance()
    {
        static LoadDiagnostics instance;
        return instance;
    }

    void SetMode(LoadMode mode) { m_mode = mode; }
    LoadMode Mode() const { return m_mode; }
    bool Strict() const { return m_mode == LoadMode::Strict; }

    void Clear() { m_entries.clear(); }

    void Report(const char* group, const char* field, const char* reason,
                const char* line)
    {
        LoadDiagnostic entry;
        entry.group = group ? group : "";
        entry.field = field ? field : "";
        entry.reason = reason ? reason : "";
        entry.line = TrimLine(line);

        // Identical (group, field, reason, line) repeats add no information;
        // keep one copy so a block-level mistake cannot flood the log.
        for (const LoadDiagnostic& existing : m_entries)
        {
            if (existing.group == entry.group && existing.field == entry.field &&
                existing.reason == entry.reason && existing.line == entry.line)
                return;
        }
        m_entries.push_back(std::move(entry));
    }

    std::size_t Count() const { return m_entries.size(); }
    const std::vector<LoadDiagnostic>& Entries() const { return m_entries; }

    // Multi-line summary for the hunt log. `maxEntries` caps the listing so a
    // badly broken file cannot fill the log; the remainder is counted.
    std::string Summary(std::size_t maxEntries = 20) const
    {
        std::string out;
        if (m_entries.empty())
            return out;

        char header[96];
        snprintf(header, sizeof(header),
                  "Load diagnostics: %u recovered value(s).\n",
                  static_cast<unsigned>(m_entries.size()));
        out += header;

        const std::size_t shown =
            m_entries.size() < maxEntries ? m_entries.size() : maxEntries;
        for (std::size_t i = 0; i < shown; ++i)
        {
            const LoadDiagnostic& entry = m_entries[i];
            out += "  [";
            out += entry.group;
            out += "] ";
            out += entry.field;
            out += ": ";
            out += entry.reason;
            if (!entry.line.empty())
            {
                out += " | line: ";
                out += entry.line;
            }
            out += "\n";
        }
        if (m_entries.size() > shown)
        {
            char more[64];
            snprintf(more, sizeof(more), "  ... %u more\n",
                      static_cast<unsigned>(m_entries.size() - shown));
            out += more;
        }
        return out;
    }

    // Trim leading whitespace and trailing line endings so a diagnostic line
    // reads like the source it came from.
    static std::string TrimLine(const char* line)
    {
        if (!line)
            return std::string();
        const char* begin = line;
        while (*begin == ' ' || *begin == '\t')
            ++begin;
        const char* end = begin + strlen(begin);
        while (end > begin &&
               (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' ||
                end[-1] == '\t'))
            --end;
        return std::string(begin, end);
    }

private:
    LoadMode m_mode = LoadMode::Lenient;
    std::vector<LoadDiagnostic> m_entries;
};

// True when `[begin, end)` equals `name` (case-insensitive). Used by the
// config pre-reader, which runs before the engine's config parser exists.
inline bool LoadKeyEquals(const char* begin, const char* end, const char* name)
{
    const std::size_t length = static_cast<std::size_t>(end - begin);
    return length == strlen(name) && LegacyText::Compare(begin, name, length) == 0;
}

// Scan a config.cfg text body for `load_mode <mode>` and set the diagnostics
// mode. Comments (#) and unrelated keys are ignored; `key=value` spelling is
// accepted as well. Returns true when the key was found and understood.
inline bool InitLoadPolicyFromConfigText(const char* text)
{
    if (!text)
        return false;

    bool found = false;
    const char* cursor = text;
    while (*cursor)
    {
        const char* eol = cursor;
        while (*eol && *eol != '\r' && *eol != '\n')
            ++eol;

        const char* p = cursor;
        while (*p == ' ' || *p == '\t')
            ++p;

        if (*p != '#' && *p != '\0')
        {
            const char* keyBegin = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '=')
                ++p;
            const char* keyEnd = p;
            while (*p == ' ' || *p == '\t' || *p == '=')
                ++p;
            const char* valueBegin = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                ++p;
            const char* valueEnd = p;

            if (LoadKeyEquals(keyBegin, keyEnd, "load_mode") &&
                valueEnd > valueBegin)
            {
                LoadMode mode = LoadMode::Lenient;
                if (ParseLoadMode(std::string(valueBegin, valueEnd).c_str(), mode))
                {
                    LoadDiagnostics::Instance().SetMode(mode);
                    found = true;
                }
            }
        }

        cursor = (*eol) ? eol + 1 : eol;
    }
    return found;
}

// C2_STRICT_DATA=1 (or "strict"/"lenient") overrides the config value. This is
// the CI/mod-authoring switch; players normally keep the lenient default.
inline bool InitLoadPolicyFromEnvironment()
{
    const char* value = std::getenv("C2_STRICT_DATA");
    if (!value) return false;
    LoadMode mode = LoadMode::Lenient;
    bool parsed = ParseLoadMode(value, mode);
    if (std::strcmp(value, "1") == 0) { mode = LoadMode::Strict; parsed = true; }
    if (std::strcmp(value, "0") == 0) { mode = LoadMode::Lenient; parsed = true; }
    if (parsed) LoadDiagnostics::Instance().SetMode(mode);
    return parsed;
}
