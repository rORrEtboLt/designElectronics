// designElectronics — tiny self-contained JSON value, parser and writer.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : t_(Bool), b_(b) {}
    Json(int v) : t_(Number), n_(v) {}
    Json(int64_t v) : t_(Number), n_((double)v) {}
    Json(double v) : t_(Number), n_(v) {}
    Json(float v) : t_(Number), n_(v) {}
    Json(const char* s) : t_(String), s_(s) {}
    Json(std::string s) : t_(String), s_(std::move(s)) {}

    static Json array() { Json j; j.t_ = Array; return j; }
    static Json object() { Json j; j.t_ = Object; return j; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == Null; }
    bool isArray() const { return t_ == Array; }
    bool isObject() const { return t_ == Object; }
    bool isString() const { return t_ == String; }
    bool isNumber() const { return t_ == Number; }

    double num(double def = 0) const { return t_ == Number ? n_ : (t_ == Bool ? (b_ ? 1 : 0) : def); }
    float f(float def = 0) const { return (float)num(def); }
    int i(int def = 0) const { return t_ == Number ? (int)n_ : def; }
    bool boolean(bool def = false) const { return t_ == Bool ? b_ : (t_ == Number ? n_ != 0 : def); }
    const std::string& str() const { static const std::string e; return t_ == String ? s_ : e; }
    std::string str(const std::string& def) const { return t_ == String ? s_ : def; }

    // arrays
    size_t size() const { return t_ == Array ? a_.size() : (t_ == Object ? o_.size() : 0); }
    const Json& operator[](size_t idx) const { static const Json n; return (t_ == Array && idx < a_.size()) ? a_[idx] : n; }
    Json& push(Json v) { if (t_ != Array) { t_ = Array; a_.clear(); } a_.push_back(std::move(v)); return a_.back(); }
    const std::vector<Json>& items() const { return a_; }

    // objects
    const Json& operator[](const char* key) const { return get(key); }
    const Json& operator[](const std::string& key) const { return get(key.c_str()); }
    const Json& get(const char* key) const {
        static const Json n;
        if (t_ != Object) return n;
        for (auto& kv : o_) if (kv.first == key) return kv.second;
        return n;
    }
    bool has(const char* key) const { if (t_ != Object) return false; for (auto& kv : o_) if (kv.first == key) return true; return false; }
    Json& set(const std::string& key, Json v) {
        if (t_ != Object) { t_ = Object; o_.clear(); }
        for (auto& kv : o_) if (kv.first == key) { kv.second = std::move(v); return kv.second; }
        o_.emplace_back(key, std::move(v));
        return o_.back().second;
    }
    const std::vector<std::pair<std::string, Json>>& members() const { return o_; }

    std::string dump(int indent = -1) const { std::string out; write(out, indent, 0); return out; }
    static Json parse(const std::string& text, std::string* err = nullptr);

private:
    Type t_ = Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Json> a_;
    std::vector<std::pair<std::string, Json>> o_;

    void write(std::string& out, int indent, int depth) const;
    friend struct JsonParser;
};
