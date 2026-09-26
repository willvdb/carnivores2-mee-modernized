#include "cli.hpp"
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/core.hpp"
#include "c2/frontend/discovery.hpp"
#include "c2/frontend/profile_files.hpp"
#include "acceptance.hpp"
#include "content_internal.hpp"
#include "discovery_internal.hpp"
#include "manifest_access.hpp"
#include "planning_internal.hpp"
#include "planning_store.hpp"
#include "probe_process.hpp"
#include "session_journal.hpp"
#include "session_runner.hpp"
#include "sessions.hpp"
#include "store_ops.hpp"
#include "store_paths.hpp"
#include "store_write.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
namespace c2::frontend::cli {
namespace fs = std::filesystem;
using compat::Value;
namespace {
// argparse usage failures (reported through the JSON envelope, exit 2).
class UsageError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

enum class Type { flag, text, path, integer, floating };
struct Option {
    Option(std::string n, Type t, bool a = false, bool r = false, std::vector<std::string> c = {})
        : name(std::move(n)), type(t), append(a), required(r), choices(std::move(c)) {}
    std::string name;
    Type type;
    bool append, required;
    std::vector<std::string> choices;
};
struct Command {
    std::vector<std::string> words;
    std::vector<std::string> positionals;
    std::vector<Option> options;
    std::vector<std::string> exclusive_required; // one-of group (catalog)
};
const std::vector<std::string> times{"0", "1", "2"};
Option time_option() { return {"--time", Type::integer, false, false, times}; }
// frontend.py parser(), command by command, in its declaration order.
std::vector<Command> commands() {
    std::vector<Command> c;
    c.push_back({{"status"}, {}, {}, {}});
    c.push_back({{"recover-backup"}, {}, {}, {}});
    c.push_back({{"hunter", "list"}, {}, {}, {}});
    c.push_back({{"hunter", "create"}, {"name"}, {}, {}});
    c.push_back({{"hunter", "select"}, {"id"}, {}, {}});
    c.push_back({{"hunter", "rename"}, {"id", "name"}, {}, {}});
    c.push_back({{"hunter", "archive"}, {"id"}, {}, {}});
    c.push_back({{"expedition", "list"}, {}, {}, {}});
    c.push_back({{"expedition", "discover"}, {"path"}, {{"--register-managed", Type::flag}}, {}});
    c.push_back({{"expedition", "register"}, {"path"}, {
        {"--dialect", Type::text, false, false, {"c2-classic", "iceage-triassic", "mee-newer", "mee-older", "unknown"}},
        {"--family", Type::text}, {"--release", Type::text}}, {}});
    c.push_back({{"expedition", "relocate"}, {"id", "path"}, {}, {}});
    c.push_back({{"expedition", "refresh"}, {"id"}, {}, {}});
    c.push_back({{"profiles"}, {"instance"}, {}, {}});
    c.push_back({{"associate"}, {"hunter", "instance", "state_key"}, {
        {"--origin", Type::text, false, true, {"personal", "bundled-example", "unknown"}},
        {"--ownership", Type::text, false, false, {"referenced", "managed"}},
        {"--import-copy", Type::flag}}, {}});
    c.push_back({{"refresh-state"}, {"association"}, {}, {}});
    c.push_back({{"catalog"}, {}, {{"--instance", Type::text}, {"--path", Type::path}}, {"--instance", "--path"}});
    c.push_back({{"launch-dry-run"}, {"association"}, {
        {"--area", Type::text, false, true}, {"--license", Type::text, true}, {"--weapon", Type::text, true},
        {"--equipment", Type::text, true}, {"--mode", Type::text, false, false, {"hunt", "observer"}}, time_option()}, {}});
    c.push_back({{"simulate-return"}, {"association"}, {{"--exit-code", Type::integer}}, {}});
    c.push_back({{"session", "prepare-synthetic"}, {"association"}, {
        {"--area", Type::text, false, true},
        {"--scenario", Type::text, false, false, {}}, time_option(), {"--timeout", Type::floating}}, {}});
    for (const char* action : {"inspect", "run", "reconcile", "recover"})
        c.push_back({{"session", action}, {"id"}, {}, {}});
    c.push_back({{"genesis-observer-plan"}, {"association"}, {
        {"--area", Type::text, false, true}, time_option(), {"--engine", Type::path}}, {}});
    const std::vector<Option> observer_trust{{"--engine", Type::path, false, true},
        {"--trusted-engine-sha256", Type::text, false, true}, {"--experimental-native-observer", Type::flag, false, true}};
    auto prepare = observer_trust;
    prepare.push_back({"--area", Type::text, false, true});
    prepare.push_back(time_option());
    prepare.push_back({"--timeout", Type::floating});
    c.push_back({{"native-observer", "prepare"}, {"id"}, prepare, {}});
    c.push_back({{"native-observer", "run"}, {"id"}, observer_trust, {}});
    const std::vector<Option> selection{{"--area", Type::text, false, true}, {"--license", Type::text, true, true},
        {"--weapon", Type::text, true, true}, time_option()};
    const std::vector<Option> hunt_trust{{"--engine", Type::path, false, true},
        {"--trusted-engine-sha256", Type::text, false, true}, {"--experimental-native-hunt", Type::flag, false, true}};
    auto hunt_prepare = selection;
    hunt_prepare.insert(hunt_prepare.end(), hunt_trust.begin(), hunt_trust.end());
    hunt_prepare.push_back({"--timeout", Type::floating});
    c.push_back({{"native-hunt", "plan"}, {"id"}, selection, {}});
    c.push_back({{"native-hunt", "prepare"}, {"id"}, hunt_prepare, {}});
    c.push_back({{"native-hunt", "run"}, {"id"}, hunt_trust, {}});
    c.push_back({{"native-hunt", "inspect"}, {"id"}, {}, {}});
    c.push_back({{"managed-state", "upgrade"}, {}, {}, {}});
    c.push_back({{"managed-state", "inspect"}, {"association"}, {}, {}});
    c.push_back({{"managed-state", "preview"}, {"id"}, {{"--expected-generation", Type::text, false, true}}, {}});
    c.push_back({{"managed-state", "accept"}, {"id"}, {{"--expected-generation", Type::text, false, true},
        {"--candidate-sha256", Type::text, false, true}}, {}});
    c.push_back({{"managed-state", "recover-acceptance"}, {"id"}, {}, {}});
    c.push_back({{"host-settings"}, {}, {{"--json", Type::text}}, {}});
    for (auto& command : c)
        if (command.words == std::vector<std::string>{"session", "prepare-synthetic"})
            for (auto& option : command.options)
                if (option.name == "--scenario")
                    for (const auto& s : sessions::scenarios()) option.choices.emplace_back(s.begin(), s.end());
    return c;
}

std::string utf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string repr(const std::string& s) { return "'" + s + "'"; }
// argparse's negative-number test: ^-\d+$|^-\d*\.\d+$
bool negative_number(const std::string& s) {
    if (s.size() < 2 || s[0] != '-') return false;
    const auto dot = s.find('.');
    auto digits = [&](std::size_t a, std::size_t b) {
        for (auto i = a; i < b; ++i) if (s[i] < '0' || s[i] > '9') return false;
        return true;
    };
    if (dot == std::string::npos) return digits(1, s.size());
    return dot + 1 < s.size() && digits(1, dot) && digits(dot + 1, s.size());
}

// Python int(): surrounding whitespace, optional sign, digits with single
// underscores between digits; canonical arbitrary-magnitude decimal result.
std::optional<std::string> python_int(std::string s) {
    const auto first = s.find_first_not_of(" \t\n\r\f\v"), last = s.find_last_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) return std::nullopt;
    s = s.substr(first, last - first + 1);
    bool negative = false;
    if (s[0] == '+' || s[0] == '-') { negative = s[0] == '-'; s.erase(0, 1); }
    std::string digits;
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '_' && k > 0 && k + 1 < s.size() && s[k - 1] != '_' && s[k + 1] != '_') continue;
        if (s[k] < '0' || s[k] > '9') return std::nullopt;
        digits += s[k];
    }
    if (digits.empty()) return std::nullopt;
    digits.erase(0, std::min(digits.find_first_not_of('0'), digits.size() - 1));
    return (negative && digits != "0" ? "-" : "") + digits;
}
// Python float() over ASCII spellings (sign, inf/infinity/nan, underscores).
std::optional<double> python_float(std::string s) {
    const auto first = s.find_first_not_of(" \t\n\r\f\v"), last = s.find_last_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) return std::nullopt;
    s = s.substr(first, last - first + 1);
    double sign = 1;
    if (s[0] == '+' || s[0] == '-') { sign = s[0] == '-' ? -1 : 1; s.erase(0, 1); }
    std::string lower;
    for (char ch : s) lower += static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (lower == "inf" || lower == "infinity") return sign * HUGE_VAL;
    if (lower == "nan") return std::nan("");
    std::string cleaned;
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '_') {
            const bool between = k > 0 && k + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[k - 1])) &&
                                 std::isdigit(static_cast<unsigned char>(s[k + 1]));
            if (!between) return std::nullopt;
            continue;
        }
        cleaned += s[k];
    }
    if (cleaned.empty() || cleaned[0] == '+' || cleaned[0] == '-' || cleaned.find_first_of("iInN") != std::string::npos)
        return std::nullopt;
    double value = 0;
    const auto [end, error] = std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
    if (end != cleaned.data() + cleaned.size()) return std::nullopt;
    if (error == std::errc::result_out_of_range) {
        // Python float() rounds overflow to infinity and underflow to zero.
        const auto e = cleaned.find_first_of("eE");
        const bool big = e == std::string::npos || cleaned[e + 1] != '-';
        return sign * (big ? HUGE_VAL : 0.0);
    }
    return sign * value;
}

struct Parsed {
    const Command* command = nullptr;
    std::map<std::string, std::vector<fs::path>> values;
    std::set<std::string> flags;
    std::vector<fs::path> positionals;
    std::optional<fs::path> store, probe;
    bool help = false, version = false;
};

// argparse _parse_optional for a --long option table: exact name, name=value,
// unique prefix (ambiguity refused), else positional/unknown.
enum class Token { positional, option, unknown };
Token classify(const std::string& arg, const std::vector<std::string>& names, std::string& name,
               std::optional<std::string>& explicit_value) {
    if (arg.empty() || arg[0] != '-') return Token::positional;
    for (const auto& n : names) if (arg == n) { name = n; return Token::option; }
    if (arg.size() == 1) return Token::positional;
    const auto equal = arg.find('=');
    const auto key = arg.substr(0, equal);
    if (equal != std::string::npos)
        for (const auto& n : names) if (key == n) { name = n; explicit_value = arg.substr(equal + 1); return Token::option; }
    if (arg.rfind("--", 0) == 0 && key.size() > 2) {
        std::vector<std::string> matches;
        for (const auto& n : names) if (n.rfind(key, 0) == 0) matches.push_back(n);
        if (matches.size() > 1) {
            std::string all;
            for (const auto& m : matches) all += (all.empty() ? "" : ", ") + m;
            throw UsageError("ambiguous option: " + key + " could match " + all);
        }
        if (matches.size() == 1) {
            name = matches[0];
            if (equal != std::string::npos) explicit_value = arg.substr(equal + 1);
            return Token::option;
        }
    }
    if (negative_number(arg) || arg.find(' ') != std::string::npos) return Token::positional;
    return Token::unknown;
}
bool looks_like_option(const std::string& arg) {
    return arg.size() > 1 && arg[0] == '-' && !negative_number(arg) && arg.find(' ') == std::string::npos;
}

// argparse converts and checks choices for every occurrence as it consumes it,
// before any command (and therefore any store directory, lock or write) runs.
void check_value(const Option& option, const std::string& raw) {
    const auto& name = option.name;
    std::string converted = raw;
    if (option.type == Type::integer) {
        const auto number = python_int(raw);
        if (!number) throw UsageError("argument " + name + ": invalid int value: " + repr(raw));
        converted = *number;
    } else if (option.type == Type::floating && !python_float(raw)) {
        throw UsageError("argument " + name + ": invalid float value: " + repr(raw));
    }
    if (!option.choices.empty() &&
        std::find(option.choices.begin(), option.choices.end(), converted) == option.choices.end()) {
        std::string list;
        for (const auto& c : option.choices) list += (list.empty() ? "" : ", ") + c;
        throw UsageError("argument " + name + ": invalid choice: " + repr(raw) + " (choose from " + list + ")");
    }
}
Parsed parse(const std::vector<fs::path>& args) {
    Parsed p;
    std::size_t i = 0;
    const std::vector<std::string> root{"--store", "--probe", "--help", "--version", "-h"};
    for (; i < args.size(); ++i) {
        const auto arg = utf8(args[i]);
        std::string name;
        std::optional<std::string> value;
        const auto token = classify(arg, root, name, value);
        if (token == Token::positional) break;
        if (token == Token::unknown) throw UsageError("unrecognized arguments: " + arg);
        if (name == "--help" || name == "-h") { p.help = true; return p; }
        if (name == "--version") {
            // Native-only: accepted solely as the entire command line.
            if (args.size() != 1) throw UsageError("unrecognized arguments: " + arg);
            p.version = true;
            return p;
        }
        fs::path path;
        if (value) path = fs::u8path(*value);
        else {
            if (i + 1 == args.size() || looks_like_option(utf8(args[i + 1])))
                throw UsageError("argument " + name + ": expected one argument");
            path = args[++i];
        }
        (name == "--store" ? p.store : p.probe) = std::move(path);
    }
    if (i == args.size()) throw UsageError("the following arguments are required: command");
    static const auto table = commands();
    std::vector<std::string> words;
    for (const Command* match = nullptr; !match;) {
        if (i == args.size()) throw UsageError("the following arguments are required: action");
        words.push_back(utf8(args[i++]));
        if (words.back() == "-h" || words.back() == "--help") { p.help = true; return p; }
        bool prefix = false;
        for (const auto& command : table) {
            if (command.words == words) match = &command;
            else if (command.words.size() > words.size() &&
                     std::equal(words.begin(), words.end(), command.words.begin())) prefix = true;
        }
        if (!match && !prefix) {
            std::vector<std::string> choices; // declaration order, as argparse lists them
            for (const auto& command : table)
                if (command.words.size() >= words.size() &&
                    std::equal(words.begin(), words.end() - 1, command.words.begin()) &&
                    std::find(choices.begin(), choices.end(), command.words[words.size() - 1]) == choices.end())
                    choices.push_back(command.words[words.size() - 1]);
            std::string list;
            for (const auto& c : choices) list += (list.empty() ? "" : ", ") + c;
            throw UsageError("argument " + std::string(words.size() == 1 ? "command" : "action") +
                             ": invalid choice: " + repr(words.back()) + " (choose from " + list + ")");
        }
        p.command = match;
    }
    std::vector<std::string> names{"--help", "-h"};
    for (const auto& o : p.command->options) names.push_back(o.name);
    std::vector<std::string> unrecognized;
    bool only_positionals = false;
    // Python 3.12 drops the first '--' only when a positional consumes an
    // argument after it; otherwise it is reported as unrecognized.
    std::optional<std::size_t> separator; // index into unrecognized
    bool separator_used = false;
    for (; i < args.size(); ++i) {
        const auto arg = utf8(args[i]);
        if (!only_positionals && arg == "--") {
            only_positionals = true;
            separator = unrecognized.size();
            unrecognized.push_back("--");
            continue;
        }
        std::string name;
        std::optional<std::string> value;
        const auto token = only_positionals ? Token::positional : classify(arg, names, name, value);
        if (token == Token::positional) {
            p.positionals.push_back(args[i]);
            if (separator && p.positionals.size() <= p.command->positionals.size()) separator_used = true;
            continue;
        }
        if (token == Token::unknown) { unrecognized.push_back(arg); continue; }
        if (name == "--help" || name == "-h") { p.help = true; return p; }
        const auto& option = *std::find_if(p.command->options.begin(), p.command->options.end(),
                                           [&](const Option& o) { return o.name == name; });
        if (option.type == Type::flag) {
            if (value) throw UsageError("argument " + name + ": ignored explicit argument " + repr(*value));
            p.flags.insert(name);
            continue;
        }
        fs::path path;
        if (value) path = fs::u8path(*value);
        else {
            if (i + 1 == args.size() || looks_like_option(utf8(args[i + 1])))
                throw UsageError("argument " + name + ": expected one argument");
            path = args[++i];
        }
        check_value(option, utf8(path));
        auto& slot = p.values[name];
        if (!option.append) slot.clear();
        slot.push_back(std::move(path));
    }
    if (separator && separator_used) unrecognized.erase(unrecognized.begin() + static_cast<std::ptrdiff_t>(*separator));
    if (!p.command->exclusive_required.empty()) {
        const auto& group = p.command->exclusive_required;
        std::vector<std::string> present;
        for (const auto& n : group) if (p.values.count(n)) present.push_back(n);
        if (present.size() > 1)
            throw UsageError("argument " + present[1] + ": not allowed with argument " + present[0]);
        if (present.empty()) {
            std::string list;
            for (const auto& n : group) list += (list.empty() ? "" : " ") + n;
            throw UsageError("one of the arguments " + list + " is required");
        }
    }
    std::vector<std::string> missing;
    for (std::size_t k = p.positionals.size(); k < p.command->positionals.size(); ++k)
        missing.push_back(p.command->positionals[k]);
    for (const auto& o : p.command->options)
        if (o.required && !p.values.count(o.name) && !p.flags.count(o.name)) missing.push_back(o.name);
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        throw UsageError("the following arguments are required: " + list);
    }
    for (std::size_t k = p.command->positionals.size(); k < p.positionals.size(); ++k)
        unrecognized.push_back(utf8(p.positionals[k]));
    if (!unrecognized.empty()) {
        std::string list;
        for (const auto& u : unrecognized) list += (list.empty() ? "" : " ") + u;
        throw UsageError("unrecognized arguments: " + list);
    }
    return p;
}

Value string_value(std::u32string s) { Value v; v.kind = compat::Kind::string; v.string = std::move(s); return v; }
Value ascii(std::string_view s) { return string_value(std::u32string(s.begin(), s.end())); }
Value text(const fs::path& p) { return string_value(store_paths::native_points(p)); }
Value boolean(bool b) { Value v; v.kind = compat::Kind::boolean; v.boolean = b; return v; }
Value array() { Value v; v.kind = compat::Kind::array; return v; }
Value object() { Value v; v.kind = compat::Kind::object; return v; }

// str(Path(value)) as argparse type=Path produces it: Path('') is '.', and on
// POSIX pathlib collapses repeated separators and '.' components and drops a
// trailing separator (keeping exactly two leading slashes), so a spelling
// such as './C:x' is judged as 'C:x' by the foreign-path rules. On Windows
// fs::path is passed through (PureWindowsPath keeps such prefixes).
fs::path dot(const fs::path& p) {
#ifdef _WIN32
    return p.empty() ? fs::path(".") : p;
#else
    const std::string& s = p.native();
    std::string root;
    if (s.rfind("//", 0) == 0 && s.rfind("///", 0) != 0) root = "//";
    else if (!s.empty() && s[0] == '/') root = "/";
    std::string joined;
    std::size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find('/', start);
        if (end == std::string::npos) end = s.size();
        const auto part = s.substr(start, end - start);
        if (!part.empty() && part != ".") joined += (joined.empty() ? "" : "/") + part;
        start = end + 1;
    }
    const auto result = root + joined;
    return result.empty() ? fs::path(".") : fs::path(result);
#endif
}
struct Arguments {
    const Parsed& p;
    bool has(const std::string& name) const { return p.values.count(name) > 0; }
    bool flag(const std::string& name) const { return p.flags.count(name) > 0; }
    // type=Path: Path('') is '.', never an absent value.
    fs::path path(const std::string& name) const { return dot(p.values.at(name).back()); }
    std::u32string string(const std::string& name, std::string_view fallback) const {
        if (!has(name)) return std::u32string(fallback.begin(), fallback.end());
        return store_paths::native_points(path(name));
    }
    std::optional<std::u32string> optional_string(const std::string& name) const {
        if (!has(name)) return std::nullopt;
        return string(name, "");
    }
    Value strings(const std::string& name) const {
        auto list = array();
        if (has(name)) for (const auto& v : p.values.at(name)) list.array.push_back(text(v));
        return list;
    }
    std::vector<std::u32string> string_list(const std::string& name) const {
        std::vector<std::u32string> list;
        if (has(name)) for (const auto& v : p.values.at(name)) list.push_back(store_paths::native_points(v));
        return list;
    }
    // type=int values and non-string argparse defaults stay Python ints.
    Value integer(const std::string& name, std::string_view fallback) const {
        Value v;
        v.kind = compat::Kind::integer;
        if (!has(name)) { v.integer = std::string(fallback); return v; }
        v.integer = *python_int(utf8(path(name)));
        return v;
    }
    // type=float; an omitted --timeout keeps the int default (argparse never
    // converts a non-string default), which the journal records as an int.
    Value floating(const std::string& name, std::string_view int_default) const {
        if (!has(name)) return integer(name, int_default);
        Value v;
        v.kind = compat::Kind::floating;
        v.floating = *python_float(utf8(path(name)));
        return v;
    }
    std::u32string positional(std::size_t k) const { return store_paths::native_points(p.positionals[k]); }
};

std::string emit(const Value& v) { return compat::display(v); }
Value selection_value(const Arguments& a, std::u32string_view mode) {
    auto selection = object();
    if (mode == U"hunt") {
        selection.object = {{U"area", string_value(a.string("--area", ""))}, {U"licenses", a.strings("--license")},
            {U"weapons", a.strings("--weapon")}, {U"equipment", array()}, {U"mode", ascii("hunt")},
            {U"time_of_day", a.integer("--time", "1")}};
    } else {
        selection.object = {{U"area", string_value(a.string("--area", ""))}, {U"mode", ascii("observer")},
            {U"time_of_day", a.integer("--time", "1")}, {U"licenses", array()}, {U"weapons", array()},
            {U"equipment", array()}};
    }
    return selection;
}
std::string narrow(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) out += c < 128 ? static_cast<char>(c) : '?';
    return out;
}
InstanceObservation checked_instance(const Manifest& manifest, const std::u32string& id) {
    auto instance = get_instance(manifest, id);
    if (instance.path_flavor() != session_journal::path_flavor())
        throw StoreError("foreign installation path requires relocation");
    return instance;
}
std::u32string dialect_hint(const InstanceObservation& instance) {
    return discovery_internal::instance_value(instance).at(U"dialect_hint").string;
}
// A returned run reconciles automatically, as frontend.py does.
Value after_run(const Store& store, const Value& result, std::u32string_view identity,
                const std::optional<fs::path>& probe, const Seams& seams) {
    if (result.at(U"state").kind == compat::Kind::string && result.at(U"state").string == U"returned")
        return reconciliation::reconcile_session(store, identity, probe, seams.policies);
    return result;
}

std::string execute(const Parsed& parsed, const Seams& seams) {
    const Arguments a{parsed};
    const auto& words = parsed.command->words;
    std::optional<fs::path> probe;
    if (parsed.probe) probe = dot(*parsed.probe);
    auto is = [&](std::initializer_list<const char*> w) {
        return words == std::vector<std::string>(w.begin(), w.end());
    };
    Store store(parsed.store ? *parsed.store : Store::default_directory());
    auto* interrupt = &interrupt_flag();
    const auto& policies = seams.policies;
    if (words[0] == "managed-state") {
        if (is({"managed-state", "upgrade"})) return emit(store_ops::upgrade_store(store));
        if (is({"managed-state", "inspect"}))
            return store.read().resolve_generation(a.positional(0)).export_history_json();
        if (is({"managed-state", "preview"}))
            return emit(acceptance::preview_acceptance(store, a.positional(0),
                a.string("--expected-generation", ""), probe, policies));
        if (is({"managed-state", "accept"}))
            return emit(acceptance::accept_candidate(store, a.positional(0), a.string("--expected-generation", ""),
                a.string("--candidate-sha256", ""), probe, policies));
        return emit(acceptance::recover_acceptance(store, a.positional(0)));
    }
    if (words[0] == "native-hunt") {
        const auto id = a.positional(0);
        if (is({"native-hunt", "plan"}))
            return emit(planning_store::plan_hunt(store, id, selection_value(a, U"hunt"), probe, policies.hunt));
        if (is({"native-hunt", "prepare"})) {
            const auto selection = selection_value(a, U"hunt");
            const auto timeout = a.floating("--timeout", "900");
            return emit(native_session::prepare(native_session::preparation_adapter(store), store, id, selection,
                a.path("--engine"), string_value(a.string("--trusted-engine-sha256", "")), true, timeout, probe, policies));
        }
        if (is({"native-hunt", "inspect"})) return emit(session_journal::read(store, id));
        const auto result = session_runner::run_native(std::nullopt, true, store, id, a.path("--engine"),
            string_value(a.string("--trusted-engine-sha256", "")), true, probe, nullptr, policies, interrupt);
        return emit(after_run(store, result, id, probe, seams));
    }
    if (words[0] == "native-observer") {
        const auto id = a.positional(0);
        if (is({"native-observer", "prepare"})) {
            const auto selection = selection_value(a, U"observer");
            const auto timeout = a.floating("--timeout", "900");
            return emit(native_session::prepare(native_session::Adapter::observer, store, id, selection,
                a.path("--engine"), string_value(a.string("--trusted-engine-sha256", "")), true, timeout, probe, policies));
        }
        const auto result = session_runner::run_native(native_session::Adapter::observer, false, store, id,
            a.path("--engine"), string_value(a.string("--trusted-engine-sha256", "")), true, probe, nullptr, policies, interrupt);
        return emit(after_run(store, result, id, probe, seams));
    }
    if (words[0] == "session") {
        const auto id = a.positional(0);
        if (is({"session", "prepare-synthetic"})) {
            const auto time = a.integer("--time", "1");
            const auto timeout = a.floating("--timeout", "5");
            return emit(sessions::prepare_session(store, id, a.string("--area", ""),
                a.string("--scenario", "unchanged"), time, timeout, probe));
        }
        if (is({"session", "inspect"})) return emit(session_journal::read(store, id));
        if (is({"session", "recover"})) return emit(session_runner::recover_session(store, id, probe, policies));
        if (is({"session", "reconcile"})) return emit(reconciliation::reconcile_session(store, id, probe, policies));
        const auto result = session_runner::run_session(store, id, probe, nullptr, std::nullopt, policies, interrupt);
        return emit(after_run(store, result, id, probe, seams));
    }
    if (is({"genesis-observer-plan"})) {
        const auto time = a.integer("--time", "1");
        std::optional<fs::path> engine;
        if (a.has("--engine")) engine = a.path("--engine");
        return emit(planning_store::plan_observer(store, a.string("--area", ""), a.positional(0),
            planning::Integer{time.integer}, probe, engine, policies.observer));
    }
    if (is({"recover-backup"})) {
        store_ops::restore_backup(store);
        auto result = object();
        result.object = {{U"result", ascii("backup-restored")},
            {U"store", text(store.directory() / "lodge.json")}};
        return emit(result);
    }
    const auto manifest = store.read();
    if (is({"status"})) return manifest.export_json(ManifestView::status);
    if (is({"hunter", "list"})) return manifest.export_json(ManifestView::hunters);
    if (is({"expedition", "list"})) return manifest.export_json(ManifestView::expeditions);
    if (is({"profiles"})) {
        const auto instance = checked_instance(manifest, a.positional(0));
        const auto inventory = inventory_profiles(content_internal::native_units(instance.path()));
        auto result = array();
        const auto dialect = narrow(dialect_hint(instance));
        for (const auto& state : inventory.states()) result.array.push_back(probe_process::inspect_set(state, probe, dialect));
        return emit(result);
    }
    if (is({"catalog"})) {
        if (a.has("--path")) return catalog::project(a.path("--path")).export_json();
        const auto instance = checked_instance(manifest, a.string("--instance", ""));
        return catalog::project(content_internal::native_units(instance.path()), dialect_hint(instance)).export_json();
    }
    if (is({"host-settings"}) && !a.has("--json")) return manifest.export_json(ManifestView::host_settings);
    if (is({"expedition", "discover"}) && !a.flag("--register-managed"))
        return emit(store_ops::discover_view(manifest, dot(parsed.positionals[0])));
    if (is({"refresh-state"})) return emit(planning_store::refresh_state(store, a.positional(0), probe));
    if (is({"launch-dry-run"})) {
        planning::LaunchSelection selection;
        selection.area = a.string("--area", "");
        selection.licenses = a.string_list("--license");
        selection.weapons = a.string_list("--weapon");
        selection.equipment = a.string_list("--equipment");
        selection.mode = a.string("--mode", "hunt");
        selection.time_of_day = planning::Integer{a.integer("--time", "1").integer};
        return planning_store::launch_dry_run(store, a.positional(0), selection, probe).export_json();
    }
    std::optional<Value> exit_code;
    if (is({"simulate-return"})) exit_code = a.integer("--exit-code", "0");
    Value result;
    store_write::transaction(store, [&](Value& data) {
        if (words[0] == "hunter") {
            std::optional<std::u32string> id, name;
            if (words[1] == "create") name = a.positional(0);
            else id = a.positional(0);
            if (words[1] == "rename") name = a.positional(1);
            result = store_ops::hunter(data, std::u32string(words[1].begin(), words[1].end()), id, name);
        } else if (is({"expedition", "register"})) {
            result = store_ops::register_instance(data, dot(parsed.positionals[0]), U"registered",
                a.string("--dialect", "unknown"), a.optional_string("--family"), a.optional_string("--release"), std::nullopt);
        } else if (is({"expedition", "relocate"})) {
            result = store_ops::relocate(data, a.positional(0), dot(parsed.positionals[1]));
        } else if (is({"expedition", "refresh"})) {
            result = store_ops::refresh_instance(data, a.positional(0));
        } else if (is({"expedition", "discover"})) {
            result = store_ops::discover_register(data, dot(parsed.positionals[0]));
        } else if (is({"associate"})) {
            const auto ownership = a.optional_string("--ownership");
            const bool import_copy = a.flag("--import-copy");
            if (import_copy && ownership == std::u32string(U"referenced"))
                throw StoreError("--import-copy conflicts with referenced ownership");
            result = store_ops::associate(store, data, a.positional(0), a.positional(1), a.positional(2),
                a.string("--origin", ""), import_copy ? std::optional<std::u32string>(U"managed") : ownership, probe);
        } else if (is({"simulate-return"})) {
            const auto identity = a.positional(0);
            const auto& associations = data.at(U"associations");
            if (!associations.contains(identity)) throw StoreError("unknown association ID");
            const auto instance = get_instance(ManifestAccess::snapshot(store, data),
                associations.at(identity).at(U"instance_id").string);
            const bool success = exit_code->integer == "0";
            result = object();
            result.object = {{U"kind", ascii("simulated-return")}, {U"process_executed", boolean(false)},
                {U"exit_code", *exit_code}, {U"result", ascii(success ? "simulated-success" : "simulated-failure")},
                {U"installation", discovery_internal::observation_value(inspect_instance(instance))}};
            result.object.emplace_back(U"state", planning_store::refresh_association(store, data, identity, probe));
            result.object.emplace_back(U"launch_tested", ascii("unknown"));
            result.object.emplace_back(U"hunt_save_round_trip_validated", ascii("unknown"));
            result.object.emplace_back(U"presentation_action", ascii("resume-and-refresh"));
        } else if (is({"host-settings"})) {
            result = store_ops::update_host_settings(data, utf8(a.path("--json")));
        } else {
            throw StoreError("unsupported command");
        }
    });
    return emit(result);
}

const char* usage =
    "usage: c2-frontend-native [--store STORE] [--probe PROBE] COMMAND ...\n"
    "Native Carnivores lodge frontend (no Python). Commands (as frontend.py):\n"
    "  status | recover-backup | host-settings [--json JSON]\n"
    "  hunter list | create NAME | select ID | rename ID NAME | archive ID\n"
    "  expedition list | discover PATH [--register-managed] | register PATH [--dialect D] [--family F] [--release R]\n"
    "             relocate ID PATH | refresh ID\n"
    "  profiles INSTANCE | associate HUNTER INSTANCE STATE_KEY --origin O [--ownership O] [--import-copy]\n"
    "  refresh-state ASSOCIATION | catalog (--instance ID | --path PATH)\n"
    "  launch-dry-run ASSOCIATION --area A [--license L]... [--weapon W]... [--equipment E]... [--mode M] [--time T]\n"
    "  simulate-return ASSOCIATION [--exit-code N] | genesis-observer-plan ASSOCIATION --area A [--time T] [--engine PATH]\n"
    "  session prepare-synthetic ASSOCIATION --area A [--scenario S] [--time T] [--timeout SECONDS]\n"
    "  session inspect|run|reconcile|recover ID\n"
    "  native-observer prepare ID --engine PATH --trusted-engine-sha256 H --experimental-native-observer --area A [--time T] [--timeout S]\n"
    "  native-observer run ID --engine PATH --trusted-engine-sha256 H --experimental-native-observer\n"
    "  native-hunt plan ID --area A --license L... --weapon W... [--time T]\n"
    "  native-hunt prepare ID --area A --license L... --weapon W... --engine PATH --trusted-engine-sha256 H --experimental-native-hunt [--timeout S]\n"
    "  native-hunt run ID --engine PATH --trusted-engine-sha256 H --experimental-native-hunt | native-hunt inspect ID\n"
    "  managed-state upgrade | inspect ASSOCIATION | preview ID --expected-generation G\n"
    "  managed-state accept ID --expected-generation G --candidate-sha256 H | recover-acceptance ID\n"
    "Options: --help, --version\n";

// json.dumps({'error': message}): default separators, ASCII escapes. Controls
// are quoted first so paths/OS messages cannot break the envelope.
void diagnostic(std::ostream& err, std::string_view message) {
    std::string json = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : message) {
        if (c == '"' || c == '\\') { json += '\\'; json += static_cast<char>(c); }
        else if (c < 32) { json += "\\u00"; json += hex[c >> 4]; json += hex[c & 15]; }
        else json += static_cast<char>(c);
    }
    json += '"';
    try {
        auto error = object();
        error.object.emplace_back(U"error", compat::parse(json));
        err << compat::dumps(error, false) << '\n';
    } catch (...) { err << "{\"error\": \"native diagnostic unavailable\"}\n"; }
}
}

std::atomic<bool>& interrupt_flag() noexcept {
    static std::atomic<bool> flag{false};
    return flag;
}

int run(const std::vector<fs::path>& args, std::ostream& out, std::ostream& err, const Seams& seams) {
    try {
        const auto parsed = parse(args);
        if (parsed.help) { out << usage; return 0; }
        if (parsed.version && !parsed.command) { out << "c2-frontend-native " << version() << '\n'; return 0; }
        // Print only after the operation (and any transaction) completed.
        const auto output = execute(parsed, seams);
        out << output;
        out.flush();
        // Like CPython's failed stdout flush at exit: the operation (and any
        // commit) already happened, but the result was not delivered.
        return out.fail() ? 120 : 0;
    } catch (const ResourceExhausted& e) {
        diagnostic(err, e.what()); return 3;
    } catch (const session_runner::Interrupted& e) {
        diagnostic(err, e.what()); return 130;
    } catch (const std::exception& e) {
        diagnostic(err, e.what()); return 2;
    }
}
}
