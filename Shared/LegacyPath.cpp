#include "LegacyPath.h"

namespace LegacyPath {
namespace fs = std::filesystem;
namespace {
template<class Char> Char FoldASCII(Char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<Char>(c + ('a' - 'A')) : c;
}

bool EqualASCII(const fs::path& a, const fs::path& b)
{
    const auto& left = a.native();
    const auto& right = b.native();
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (FoldASCII(left[i]) != FoldASCII(right[i])) return false;
    return true;
}
}

std::string Result::Message() const
{
    if (*this) return {};
    const char* reason = "filesystem error";
    switch (error) {
    case Error::InvalidPath: reason = "empty path or embedded NUL"; break;
    case Error::UnsupportedRoot: reason = "Windows drive/UNC path requires Windows"; break;
    case Error::Missing: reason = "not found"; break;
    case Error::Ambiguous: reason = "ambiguous ASCII case-insensitive match"; break;
    default: break;
    }
    std::string message = "Legacy path '" + requested + "': " + reason;
    if (!component.empty()) message += "; component '" + component.string() + "'";
    if (!directory.empty()) message += " in '" + directory.string() + "'";
    if (systemError) message += " (" + systemError.message() + ")";
    return message;
}

Result Resolve(std::string_view logical, const fs::path& root)
{
    Result result;
    result.requested = std::string(logical);
    auto fail = [&](Error error, std::error_code ec = {}) {
        result.error = error;
        result.systemError = ec;
        result.path.clear();
        return result;
    };
    if (logical.empty() || logical.find('\0') != std::string_view::npos)
        return fail(Error::InvalidPath);

    std::string normalized(logical);
    for (char& c : normalized) if (c == '\\') c = '/';
#ifndef _WIN32
    // Do not silently reinterpret a Windows drive or network root as a POSIX
    // relative filename or mount. Native POSIX absolute paths remain supported.
    if ((normalized.size() >= 2 && normalized[1] == ':' &&
         ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
          (normalized[0] >= 'a' && normalized[0] <= 'z'))) ||
        normalized.compare(0, 2, "//") == 0)
        return fail(Error::UnsupportedRoot);
#endif
    try {
        const fs::path request(normalized);
        fs::path current = request.has_root_path() ? request.root_path() : root;
        std::error_code ec;
        for (const auto& component : request.relative_path()) {
            result.directory = current;
            result.component = component;
            if (!fs::is_directory(current, ec))
                return fail(Error::Filesystem, ec ? ec : std::make_error_code(std::errc::not_a_directory));
            if (component.empty() || component == "." || component == "..") {
                // A trailing slash requires a directory, as does file/.. .
                // Leaving these components intact preserves symlink traversal.
                current /= component;
                continue;
            }

            fs::path match;
            std::size_t matches = 0;
            bool exact = false;
            // Enumerate even on Windows: exists(request) is case-insensitive
            // there and cannot recover actual spelling or identify an exact name.
            fs::directory_iterator it(current, ec), end;
            if (ec) return fail(Error::Filesystem, ec);
            for (; it != end; it.increment(ec)) {
                if (ec) return fail(Error::Filesystem, ec);
                const auto name = it->path().filename();
                if (name.native() == component.native()) {
                    match = name;
                    exact = true;
                    break;
                }
                if (EqualASCII(name, component)) {
                    match = name;
                    ++matches;
                }
            }
            if (ec) return fail(Error::Filesystem, ec);
            if (!exact && matches > 1) return fail(Error::Ambiguous);
            if (!exact && matches == 0) return fail(Error::Missing);
            current /= match;
        }
        // Reject dangling symlinks while allowing normal links and directories.
        if (!fs::exists(current, ec))
            return fail(ec ? Error::Filesystem : Error::Missing, ec);
        result.path = current;
        result.path.make_preferred();
        return result;
    } catch (const fs::filesystem_error& error) {
        return fail(Error::Filesystem, error.code());
    }
}
}
