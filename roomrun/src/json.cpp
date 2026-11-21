#include "roomrun/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rr {

static const Json kNull;

const Json* Json::find(const std::string& key) const {
    if (type != Object) return nullptr;
    for (auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}
const Json& Json::operator[](const std::string& key) const {
    const Json* j = find(key);
    return j ? *j : kNull;
}
double Json::numOr(const std::string& key, double def) const {
    const Json* j = find(key);
    return (j && j->type == Number) ? j->num : def;
}
std::string Json::strOr(const std::string& key, const std::string& def) const {
    const Json* j = find(key);
    return (j && j->type == String) ? j->str : def;
}
bool Json::boolOr(const std::string& key, bool def) const {
    const Json* j = find(key);
    return (j && j->type == Bool) ? j->b : def;
}
Json& Json::set(const std::string& key, Json v) {
    type = Object;
    for (auto& kv : obj)
        if (kv.first == key) { kv.second = std::move(v); return kv.second; }
    obj.emplace_back(key, std::move(v));
    return obj.back().second;
}

static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += c;
        }
    }
    out += '"';
}

static void numTo(std::string& out, double v, int prec) {
    if (!std::isfinite(v)) { out += "null"; return; }
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char b[32]; std::snprintf(b, sizeof b, "%lld", (long long)v); out += b; return;
    }
    char b[64]; std::snprintf(b, sizeof b, "%.*f", prec, v);
    // trim trailing zeros for compactness (deterministic)
    std::string s(b);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    out += s;
}

void Json::dumpTo(std::string& out, int prec, bool pretty, int ind) const {
    auto nl = [&](int i) { if (pretty) { out += '\n'; out.append(i * 2, ' '); } };
    switch (type) {
        case Null: out += "null"; break;
        case Bool: out += b ? "true" : "false"; break;
        case Number: numTo(out, num, prec); break;
        case String: escapeTo(out, str); break;
        case Array: {
            out += '[';
            // keep numeric arrays on one line even when pretty
            bool flat = true;
            for (auto& e : arr) if (e.type == Array || e.type == Object) { flat = false; break; }
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i) out += ',';
                if (!flat) nl(ind + 1);
                arr[i].dumpTo(out, prec, pretty, ind + 1);
            }
            if (!flat && !arr.empty()) nl(ind);
            out += ']';
            break;
        }
        case Object: {
            out += '{';
            for (size_t i = 0; i < obj.size(); ++i) {
                if (i) out += ',';
                nl(ind + 1);
                escapeTo(out, obj[i].first);
                out += pretty ? ": " : ":";
                obj[i].second.dumpTo(out, prec, pretty, ind + 1);
            }
            if (!obj.empty()) nl(ind);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int precision, bool pretty) const {
    std::string out;
    dumpTo(out, precision, pretty, 0);
    return out;
}

namespace {
struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    explicit Parser(const std::string& t) : s(t) {}
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at offset " + std::to_string(i); return false; }
    bool value(Json& out) {
        ws();
        if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (c == '"') { out.type = Json::String; return string(out.str); }
        if (s.compare(i, 4, "true") == 0) { out = Json(true); i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { out = Json(false); i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { out = Json(); i += 4; return true; }
        char* end = nullptr;
        double v = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return fail("bad value");
        i = end - s.c_str();
        out = Json(v);
        return true;
    }
    bool string(std::string& out) {
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[++i];
                if (e == 'n') out += '\n';
                else if (e == 't') out += '\t';
                else if (e == 'u' && i + 4 < s.size()) { out += '?'; i += 4; }
                else out += e;
            } else out += s[i];
            ++i;
        }
        if (i >= s.size()) return fail("unterminated string");
        ++i;
        return true;
    }
    bool array(Json& out) {
        out = Json::array();
        ++i; ws();
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        while (true) {
            Json v;
            if (!value(v)) return false;
            out.arr.push_back(std::move(v));
            ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            return fail("expected , or ]");
        }
    }
    bool object(Json& out) {
        out = Json::object();
        ++i; ws();
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        while (true) {
            ws();
            if (i >= s.size() || s[i] != '"') return fail("expected key");
            std::string k;
            if (!string(k)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return fail("expected :");
            ++i;
            Json v;
            if (!value(v)) return false;
            out.obj.emplace_back(std::move(k), std::move(v));
            ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            return fail("expected , or }");
        }
    }
};
}  // namespace

Json Json::parse(const std::string& text, std::string* err) {
    Parser p(text);
    Json out;
    if (!p.value(out)) {
        if (err) *err = p.err;
        return Json();
    }
    if (err) err->clear();
    return out;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << data;
    return bool(f);
}

}  // namespace rr
