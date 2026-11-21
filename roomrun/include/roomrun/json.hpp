// Minimal JSON value + parser + writer (no external deps, deterministic output).
#pragma once
#include <map>
#include <string>
#include <vector>

namespace rr {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;  // insertion-ordered

    Json() = default;
    Json(double v) : type(Number), num(v) {}
    Json(int v) : type(Number), num(v) {}
    Json(bool v) : type(Bool), b(v) {}
    Json(const char* s) : type(String), str(s) {}
    Json(std::string s) : type(String), str(std::move(s)) {}
    static Json array() { Json j; j.type = Array; return j; }
    static Json object() { Json j; j.type = Object; return j; }

    bool isNull() const { return type == Null; }
    bool isObject() const { return type == Object; }
    bool isArray() const { return type == Array; }
    bool isNumber() const { return type == Number; }
    bool isString() const { return type == String; }

    const Json* find(const std::string& key) const;
    const Json& operator[](const std::string& key) const;  // Null if missing
    const Json& operator[](size_t i) const { return arr.at(i); }
    size_t size() const { return type == Array ? arr.size() : obj.size(); }

    double numOr(const std::string& key, double def) const;
    std::string strOr(const std::string& key, const std::string& def) const;
    bool boolOr(const std::string& key, bool def) const;

    Json& set(const std::string& key, Json v);  // object insert/replace
    Json& push(Json v) { type = Array; arr.push_back(std::move(v)); return arr.back(); }

    // Compact deterministic serialisation; numbers printed with `precision` decimals.
    std::string dump(int precision = 4, bool pretty = false) const;
    static Json parse(const std::string& text, std::string* err = nullptr);

private:
    void dumpTo(std::string& out, int prec, bool pretty, int indent) const;
};

std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& data);

}  // namespace rr
