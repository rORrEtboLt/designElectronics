#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += (char)c;
        }
    }
    out += '"';
}

void Json::write(std::string& out, int indent, int depth) const {
    auto nl = [&](int d) {
        if (indent < 0) return;
        out += '\n';
        out.append((size_t)(indent * d), ' ');
    };
    switch (t_) {
        case Null: out += "null"; break;
        case Bool: out += b_ ? "true" : "false"; break;
        case Number: {
            if (!std::isfinite(n_)) { out += "0"; break; }
            char buf[40];
            if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) snprintf(buf, sizeof buf, "%.0f", n_);
            else snprintf(buf, sizeof buf, "%.6g", n_);
            out += buf;
            break;
        }
        case String: writeString(out, s_); break;
        case Array: {
            // Short arrays of scalars stay on one line (points, coordinates).
            bool flat = true;
            for (auto& v : a_) if (v.t_ == Array || v.t_ == Object) { flat = false; break; }
            out += '[';
            for (size_t i = 0; i < a_.size(); i++) {
                if (i) out += flat ? ", " : ",";
                if (!flat) nl(depth + 1);
                a_[i].write(out, indent, depth + 1);
            }
            if (!flat && !a_.empty()) nl(depth);
            out += ']';
            break;
        }
        case Object: {
            out += '{';
            for (size_t i = 0; i < o_.size(); i++) {
                if (i) out += ',';
                nl(depth + 1);
                writeString(out, o_[i].first);
                out += indent >= 0 ? ": " : ":";
                o_[i].second.write(out, indent, depth + 1);
            }
            if (!o_.empty()) nl(depth);
            out += '}';
            break;
        }
    }
}

struct JsonParser {
    const std::string& s;
    size_t p = 0;
    std::string err;

    void ws() { while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++; }
    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at offset " + std::to_string(p); return false; }

    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }

    bool str(std::string& o) {
        if (s[p] != '"') return fail("expected string");
        p++;
        while (p < s.size() && s[p] != '"') {
            char c = s[p++];
            if (c != '\\') { o += c; continue; }
            if (p >= s.size()) return fail("bad escape");
            char e = s[p++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (p + 4 > s.size()) return fail("bad unicode escape");
                    unsigned cp = (unsigned)strtoul(s.substr(p, 4).c_str(), nullptr, 16);
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        unsigned lo = (unsigned)strtoul(s.substr(p + 2, 4).c_str(), nullptr, 16);
                        p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += e;
            }
        }
        if (p >= s.size()) return fail("unterminated string");
        p++;
        return true;
    }

    bool value(Json& v, int depth) {
        if (depth > 200) return fail("nesting too deep");
        ws();
        if (p >= s.size()) return fail("unexpected end");
        char c = s[p];
        if (c == '{') {
            p++;
            v = Json::object();
            ws();
            if (p < s.size() && s[p] == '}') { p++; return true; }
            for (;;) {
                ws();
                std::string k;
                if (p >= s.size() || !str(k)) return fail("expected key");
                ws();
                if (p >= s.size() || s[p] != ':') return fail("expected ':'");
                p++;
                Json child;
                if (!value(child, depth + 1)) return false;
                v.o_.emplace_back(std::move(k), std::move(child));
                ws();
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == '}') { p++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            p++;
            v = Json::array();
            ws();
            if (p < s.size() && s[p] == ']') { p++; return true; }
            for (;;) {
                Json child;
                if (!value(child, depth + 1)) return false;
                v.a_.push_back(std::move(child));
                ws();
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == ']') { p++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string o; if (!str(o)) return false; v = Json(std::move(o)); return true; }
        if (s.compare(p, 4, "true") == 0) { p += 4; v = Json(true); return true; }
        if (s.compare(p, 5, "false") == 0) { p += 5; v = Json(false); return true; }
        if (s.compare(p, 4, "null") == 0) { p += 4; v = Json(); return true; }
        const char* start = s.c_str() + p;
        char* end = nullptr;
        double d = strtod(start, &end);
        if (end == start) return fail("unexpected character");
        p += (size_t)(end - start);
        v = Json(d);
        return true;
    }
};

Json Json::parse(const std::string& text, std::string* err) {
    JsonParser jp{text, 0, {}};
    Json v;
    if (!jp.value(v, 0)) {
        if (err) *err = jp.err;
        return Json();
    }
    if (err) err->clear();
    return v;
}
