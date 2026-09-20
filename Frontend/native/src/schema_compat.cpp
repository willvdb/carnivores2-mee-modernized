#include "schema_compat.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace c2::frontend::schema {
using compat::Kind;
using compat::Value;
namespace {
struct Range {
    char32_t first, last;
};
#include "schema_unicode.inc"
template <std::size_t N> bool in(char32_t c, const Range (&ranges)[N]) {
    const auto p = std::lower_bound(ranges, ranges + N, c,
                                    [](const Range &r, char32_t v) { return r.last < v; });
    return p != ranges + N && p->first <= c;
}
template <std::size_t N> void mapped(std::u32string &out, char32_t c, const CaseEntry (&table)[N]) {
    const auto p = std::lower_bound(table, table + N, c,
                                    [](const CaseEntry &e, char32_t v) { return e.code < v; });
    if (p != table + N && p->code == c)
        out += p->text;
    else
        out += c;
}
std::string float_integer(double x) {
    // Exact conversion of integral binary64 to decimal, without a rounded
    // integer->double comparison (which loses information beyond 2**53).
    if (x == 0)
        return "0";
    const bool negative = std::signbit(x);
    x = std::fabs(x);
    int exponent = 0;
    const double f = std::frexp(x, &exponent);
    auto mantissa = static_cast<std::uint64_t>(std::ldexp(f, 53));
    exponent -= 53;
    if (exponent < 0) {
        mantissa >>= -exponent;
        exponent = 0;
    }
    std::string s = std::to_string(mantissa);
    while (exponent-- > 0) {
        int carry = 0;
        for (auto i = s.rbegin(); i != s.rend(); ++i) {
            int v = (*i - '0') * 2 + carry;
            *i = static_cast<char>('0' + v % 10);
            carry = v / 10;
        }
        if (carry)
            s.insert(s.begin(), '1');
    }
    return negative ? "-" + s : s;
}
bool number(const Value &v) {
    return v.kind == Kind::boolean || v.kind == Kind::integer || v.kind == Kind::floating;
}
std::string integral(const Value &v) {
    return v.kind == Kind::boolean ? (v.boolean ? "1" : "0") : v.integer;
}
bool numeric_equal(const Value &a, const Value &b) {
    if (a.kind == Kind::floating && b.kind == Kind::floating)
        return a.floating == b.floating;
    if (a.kind != Kind::floating && b.kind != Kind::floating)
        return integral(a) == integral(b);
    const Value &f = a.kind == Kind::floating ? a : b;
    const Value &i = a.kind == Kind::floating ? b : a;
    return std::isfinite(f.floating) && std::trunc(f.floating) == f.floating &&
           float_integer(f.floating) == integral(i);
}
} // namespace
bool equal(const Value &a, const Value &b) {
    std::vector<std::pair<const Value *, const Value *>> work{{&a, &b}};
    bool root = true;
    while (!work.empty()) {
        auto [ap, bp] = work.back();
        work.pop_back();
        // Python containers use identity-or-equality for their elements; the
        // parser shares no scalar objects except its NaN singleton semantics.
        const bool nested = !root;
        root = false;
        if (nested && (ap == bp || (ap->kind == Kind::floating && bp->kind == Kind::floating &&
                                    std::isnan(ap->floating) && std::isnan(bp->floating))))
            continue;
        const auto &x = *ap;
        const auto &y = *bp;
        if (number(x) && number(y)) {
            if (!numeric_equal(x, y))
                return false;
            continue;
        }
        if (x.kind != y.kind)
            return false;
        switch (x.kind) {
        case Kind::null:
            break;
        case Kind::string:
            if (x.string != y.string)
                return false;
            break;
        case Kind::array:
            if (x.array.size() != y.array.size())
                return false;
            for (std::size_t i = 0; i < x.array.size(); ++i)
                work.emplace_back(&x.array[i], &y.array[i]);
            break;
        case Kind::object:
            if (x.object.size() != y.object.size())
                return false;
            for (const auto &p : x.object) {
                if (!y.contains(p.first))
                    return false;
                work.emplace_back(&p.second, &y.at(p.first));
            }
            break;
        default:
            return false;
        }
    }
    return true;
}
bool truth(const Value &v) {
    switch (v.kind) {
    case Kind::null:
        return false;
    case Kind::boolean:
        return v.boolean;
    case Kind::integer:
        return v.integer != "0";
    case Kind::floating:
        return v.floating != 0;
    case Kind::string:
        return !v.string.empty();
    case Kind::array:
        return !v.array.empty();
    case Kind::object:
        return !v.object.empty();
    }
    return false;
}
std::u32string casefold(std::u32string_view s) {
    std::u32string out;
    for (auto c : s)
        mapped(out, c, fold_table);
    return out;
}
std::u32string lower(std::u32string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != 0x3a3) {
            mapped(out, s[i], lower_table);
            continue;
        }
        bool before = false, after = false;
        for (auto j = i; j > 0;) {
            auto c = s[--j];
            if (in(c, ignorable_ranges))
                continue;
            before = in(c, cased_ranges);
            break;
        }
        for (auto j = i + 1; j < s.size(); ++j) {
            auto c = s[j];
            if (in(c, ignorable_ranges))
                continue;
            after = in(c, cased_ranges);
            break;
        }
        out += before && !after ? 0x3c2 : 0x3c3;
    }
    return out;
}
bool blank(std::u32string_view s) {
    return std::all_of(s.begin(), s.end(), [](char32_t c) { return in(c, space_ranges); });
}
PurePath path(std::u32string s, bool nt) {
    PurePath p;
    p.nt = nt;
    char32_t sep = nt ? U'\\' : U'/';
    std::size_t offset = 0;
    if (nt) {
        std::replace(s.begin(), s.end(), U'/', U'\\');
        if (!s.empty() && s[0] == sep) {
            if (s.size() > 1 && s[1] == sep) {
                auto prefix = s.substr(0, 8);
                for (auto &c : prefix)
                    if (c >= U'a' && c <= U'z')
                        c -= 32;
                auto start = prefix == U"\\\\?\\UNC\\" ? 8u : 2u;
                auto i = s.find(sep, start), j = i == s.npos ? s.npos : s.find(sep, i + 1);
                if (j == s.npos) {
                    p.drive = s;
                    offset = s.size();
                } else {
                    p.drive = s.substr(0, j);
                    p.root = U"\\";
                    offset = j + 1;
                }
                if (p.root.empty() && !p.drive.empty() && p.drive.back() != sep) {
                    std::vector<std::u32string> chunks;
                    std::size_t pos = 0;
                    while (true) {
                        auto end = p.drive.find(sep, pos);
                        chunks.push_back(p.drive.substr(pos, end == s.npos ? end : end - pos));
                        if (end == s.npos)
                            break;
                        pos = end + 1;
                    }
                    if ((chunks.size() == 4 && chunks[2] != U"?" && chunks[2] != U"." &&
                         !chunks[2].empty()) ||
                        chunks.size() == 6)
                        p.root = U"\\";
                }
            } else {
                p.root = U"\\";
                offset = 1;
            }
        } else if (s.size() > 1 && s[1] == U':') {
            p.drive = s.substr(0, 2);
            offset = 2;
            if (s.size() > 2 && s[2] == sep) {
                p.root = U"\\";
                offset = 3;
            }
        }
    } else if (!s.empty() && s[0] == sep) {
        p.root = (s.size() > 1 && s[1] == sep && (s.size() == 2 || s[2] != sep)) ? U"//" : U"/";
        offset = p.root.size();
    }
    while (offset < s.size()) {
        auto end = s.find(sep, offset);
        auto part = s.substr(offset, end == s.npos ? end : end - offset);
        if (!part.empty() && part != U".")
            p.parts.push_back(part);
        if (end == s.npos)
            break;
        offset = end + 1;
    }
    return p;
}
bool PurePath::parent() const {
    return std::find(parts.begin(), parts.end(), U"..") != parts.end();
}
bool PurePath::below(const PurePath &other) const {
    if (nt != other.nt || parts.size() <= other.parts.size())
        return false;
    auto norm = [&](const std::u32string &s) { return nt ? lower(s) : s; };
    if (norm(drive) != norm(other.drive) || root != other.root)
        return false;
    for (std::size_t i = 0; i < other.parts.size(); ++i)
        if (norm(parts[i]) != norm(other.parts[i]))
            return false;
    return true;
}
} // namespace c2::frontend::schema
