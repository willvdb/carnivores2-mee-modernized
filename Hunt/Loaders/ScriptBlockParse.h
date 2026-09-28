#pragma once

#include <cstdio>
#include <cstring>

// Consume the body of a script block after its opening '{' has already been
// read. Braces in quoted values and // comments are data, not structure.
inline bool ConsumeScriptBlockBody(FILE* stream)
{
    if (!stream)
        return false;

    char line[256];
    int depth = 1;
    while (depth > 0 && fgets(line, 255, stream))
    {
        bool inSingleQuote = false;
        bool inDoubleQuote = false;
        bool escaped = false;

        for (const char* cursor = line; *cursor; ++cursor)
        {
            const char current = *cursor;
            if (escaped)
            {
                escaped = false;
                continue;
            }

            if ((inSingleQuote || inDoubleQuote) && current == '\\')
            {
                escaped = true;
                continue;
            }

            if (inSingleQuote)
            {
                if (current == '\'')
                    inSingleQuote = false;
                continue;
            }

            if (inDoubleQuote)
            {
                if (current == '"')
                    inDoubleQuote = false;
                continue;
            }

            if (current == '/' && cursor[1] == '/')
                break;
            if (current == '\'')
            {
                inSingleQuote = true;
                continue;
            }
            if (current == '"')
            {
                inDoubleQuote = true;
                continue;
            }
            if (current == '{')
                ++depth;
            else if (current == '}' && --depth == 0)
                return true;
        }

        // Several shipped _RES.TXT files use the section marker as the
        // authoritative boundary even when their nested braces are uneven.
        if (strstr(line, "//==== end of"))
            return true;
    }

    return false;
}
