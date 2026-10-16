// designElectronics — Arduino sketch interpreter (see arduino.h).
//
// Pipeline: preprocess (#define) → tokens → single-pass recursive-descent
// compiler → bytecode → stack VM that can suspend at delay().
#include "arduino.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace ard {
namespace {

// ===========================================================================
// Tokens
// ===========================================================================
enum TokK { T_ID, T_NUM, T_STR, T_OP, T_END };
struct Tok {
    TokK k = T_END;
    std::string s;
    double n = 0;
    bool flt = false;
    int line = 0;
};

struct Err {
    int line;
    std::string msg;
};
[[noreturn]] void fail(int line, const std::string& m) { throw Err{line, m}; }

bool isIdStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool isIdChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

void lexLine(const std::string& src, int line, std::vector<Tok>& out, bool& inComment) {
    size_t i = 0, n = src.size();
    while (i < n) {
        if (inComment) {
            size_t e = src.find("*/", i);
            if (e == std::string::npos) return;
            i = e + 2;
            inComment = false;
            continue;
        }
        char c = src[i];
        if (std::isspace((unsigned char)c)) { i++; continue; }
        if (c == '/' && i + 1 < n && src[i + 1] == '/') return;
        if (c == '/' && i + 1 < n && src[i + 1] == '*') { inComment = true; i += 2; continue; }
        Tok t;
        t.line = line;
        if (isIdStart(c)) {
            size_t j = i;
            while (j < n && isIdChar(src[j])) j++;
            t.k = T_ID;
            t.s = src.substr(i, j - i);
            i = j;
        } else if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)src[i + 1]))) {
            size_t j = i;
            t.k = T_NUM;
            if (c == '0' && j + 1 < n && (src[j + 1] == 'x' || src[j + 1] == 'X')) {
                j += 2;
                while (j < n && std::isxdigit((unsigned char)src[j])) j++;
                t.n = (double)std::strtoull(src.substr(i + 2, j - i - 2).c_str(), nullptr, 16);
            } else if (c == '0' && j + 1 < n && (src[j + 1] == 'b' || src[j + 1] == 'B')) {
                j += 2;
                unsigned long long v = 0;
                while (j < n && (src[j] == '0' || src[j] == '1')) v = v * 2 + (unsigned)(src[j++] - '0');
                t.n = (double)v;
            } else {
                while (j < n && (std::isdigit((unsigned char)src[j]) || src[j] == '.')) { if (src[j] == '.') t.flt = true; j++; }
                if (j < n && (src[j] == 'e' || src[j] == 'E')) {
                    t.flt = true;
                    j++;
                    if (j < n && (src[j] == '+' || src[j] == '-')) j++;
                    while (j < n && std::isdigit((unsigned char)src[j])) j++;
                }
                t.n = std::strtod(src.substr(i, j - i).c_str(), nullptr);
            }
            while (j < n && (src[j] == 'u' || src[j] == 'U' || src[j] == 'l' || src[j] == 'L' || src[j] == 'f' || src[j] == 'F')) {
                if (src[j] == 'f' || src[j] == 'F') t.flt = true;
                j++;
            }
            i = j;
        } else if (c == '"' || c == '\'') {
            char q = c;
            size_t j = i + 1;
            std::string v;
            while (j < n && src[j] != q) {
                if (src[j] == '\\' && j + 1 < n) {
                    char e = src[j + 1];
                    v += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == '0' ? '\0' : e;
                    j += 2;
                } else v += src[j++];
            }
            if (j >= n) fail(line, "missing closing " + std::string(1, q));
            i = j + 1;
            if (q == '"') { t.k = T_STR; t.s = v; }
            else { t.k = T_NUM; t.n = v.empty() ? 0 : (double)(unsigned char)v[0]; }
        } else {
            static const char* ops3[] = {"<<=", ">>=", "..."};
            static const char* ops2[] = {"==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "->", "::"};
            t.k = T_OP;
            bool found = false;
            for (auto* o : ops3) if (src.compare(i, 3, o) == 0) { t.s = o; i += 3; found = true; break; }
            if (!found) for (auto* o : ops2) if (src.compare(i, 2, o) == 0) { t.s = o; i += 2; found = true; break; }
            if (!found) { t.s = std::string(1, c); i++; }
        }
        out.push_back(t);
    }
}

// Preprocess (#define object-like macros, ignore other directives) and tokenize.
std::vector<Tok> tokenize(const std::string& source, int lineOffset) {
    std::map<std::string, std::vector<Tok>> macros;
    std::vector<Tok> raw;
    bool inComment = false;
    int line = 0;
    size_t pos = 0;
    while (pos <= source.size()) {
        size_t e = source.find('\n', pos);
        if (e == std::string::npos) e = source.size();
        std::string text = source.substr(pos, e - pos);
        line++;
        int thisLine = line;
        while (!text.empty() && text.back() == '\\' && e < source.size()) {  // continued lines
            text.pop_back();
            pos = e + 1;
            e = source.find('\n', pos);
            if (e == std::string::npos) e = source.size();
            text += source.substr(pos, e - pos);
            line++;
        }
        pos = e + 1;
        size_t f = text.find_first_not_of(" \t");
        if (!inComment && f != std::string::npos && text[f] == '#') {
            std::string d = text.substr(f + 1);
            size_t k = d.find_first_not_of(" \t");
            d = k == std::string::npos ? "" : d.substr(k);
            if (d.rfind("define", 0) == 0) {
                std::string rest = d.substr(6);
                size_t a = rest.find_first_not_of(" \t");
                if (a == std::string::npos) continue;
                size_t b = a;
                while (b < rest.size() && isIdChar(rest[b])) b++;
                std::string name = rest.substr(a, b - a);
                if (b < rest.size() && rest[b] == '(') fail(thisLine + lineOffset, "function-like #define macros aren't supported in the simulator — use a function instead");
                std::vector<Tok> body;
                bool c2 = false;
                lexLine(rest.substr(b), thisLine + lineOffset, body, c2);
                macros[name] = body;
            } else if (d.rfind("undef", 0) == 0) {
                std::string name = d.substr(5);
                name.erase(0, name.find_first_not_of(" \t"));
                macros.erase(name);
            }
            continue;  // #include, #pragma, #if… are ignored
        }
        lexLine(text, thisLine + lineOffset, raw, inComment);
        if (e >= source.size()) break;
    }
    // expand macros
    std::vector<Tok> out;
    std::function<void(const Tok&, int)> emit = [&](const Tok& t, int depth) {
        if (t.k == T_ID && depth < 16) {
            auto it = macros.find(t.s);
            if (it != macros.end()) {
                for (Tok m : it->second) { m.line = t.line; emit(m, depth + 1); }
                return;
            }
        }
        out.push_back(t);
    };
    for (auto& t : raw) emit(t, 0);
    Tok end;
    end.line = line + lineOffset;
    out.push_back(end);
    return out;
}

// ===========================================================================
// Types, values, bytecode
// ===========================================================================
struct Ty {
    bool flt = false, str = false, obj = false, boolean = false, isVoid = false, sgn = true;
    int bits = 32;
};

struct Val {
    double n = 0;
    bool isInt = true;
    int arr = -1;                       // array reference
    std::shared_ptr<const std::string> s;  // string value
    bool isStr() const { return (bool)s; }
};

Val num(double d, bool isInt) { Val v; v.n = d; v.isInt = isInt; return v; }
Val str(std::string s) { Val v; v.s = std::make_shared<const std::string>(std::move(s)); return v; }

enum Op : uint8_t {
    PUSHN, PUSHS, LDG, STG, LDL, STL, INDEX, STX, DUP, DUP2, POP, BIN, UN, JMP, JZ, JNZ,
    CALL, BUILTIN, RET, CONV, INCV, INCX, ALLOC, START_LOOP
};
enum Bin { B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_SHL, B_SHR, B_AND, B_OR, B_XOR, B_LT, B_LE, B_GT, B_GE, B_EQ, B_NE };
enum Un { U_NEG, U_NOT, U_BNOT };

struct Ins {
    Op op;
    int a = 0, b = 0;
    double d = 0;
    int line = 0;
};

// builtins (id = index)
const char* kBuiltins[] = {
    "pinMode", "digitalWrite", "digitalRead", "analogRead", "analogWrite", "delay", "delayMicroseconds", "millis", "micros",
    "tone", "noTone", "map", "constrain", "min", "max", "abs", "sq", "sqrt", "pow", "sin", "cos", "tan", "atan2", "floor", "ceil",
    "round", "random", "randomSeed", "bitRead", "highByte", "lowByte", "bit", "Serial.begin", "Serial.print", "Serial.println",
    "Serial.write", "Serial.available", "Serial.read", "Serial.printf", "Serial.flush", "Serial.end", "Serial.readString",
    "Serial.parseInt", "String", "analogReadResolution", "analogWriteResolution", "ledcSetup", "ledcAttachPin", "ledcWrite",
    "ledcAttach", "yield", "atan", "asin", "acos", "exp", "log", "log10", "fabs", "isnan", "fmod", "str.length", "str.toInt",
    "str.toFloat", "str.charAt", "str.substring", "str.indexOf", "str.equals", "str.startsWith", "str.endsWith", "str.concat",
    "str.toUpperCase", "str.toLowerCase", "str.trim", "sizeof_elem", "Serial.peek", "radians", "degrees", "noop", "ledcWriteTone",
    "str.replace", "str.c_str",
};
int builtinId(const std::string& n) {
    for (size_t i = 0; i < sizeof(kBuiltins) / sizeof(kBuiltins[0]); i++) if (n == kBuiltins[i]) return (int)i;
    return -1;
}

struct Func {
    std::string name;
    int entry = -1;
    int nparams = 0, nlocals = 0;
    Ty ret;
    std::vector<Ty> params;
    bool defined = false;
    int firstUseLine = 0;
};

struct Var {
    int slot = 0;
    bool global = false;
    int ty = 0;          // index into types
    bool isArray = false;
    int arrLen = 0;      // global arrays: length
    bool isObject = false;
    std::string className;
};

const std::set<std::string> kTypeWords = {"void", "bool", "boolean", "char", "byte", "int", "short", "long", "unsigned", "signed",
                                          "float", "double", "word", "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                                          "int8_t", "int16_t", "int32_t", "int64_t", "String", "const", "volatile", "static",
                                          "constexpr", "inline", "register", "auto"};

// ===========================================================================
// Compiler
// ===========================================================================
class Compiler {
public:
    Compiler(const BoardInfo& b, std::vector<Ins>& code, std::vector<Ty>& types, std::vector<std::string>& strs,
             std::vector<std::string>& warns)
        : board_(b), code_(code), types_(types), strs_(strs), warns_(warns) {}

    std::vector<Var> globals;
    std::vector<int> globalArrayLens;  // per global slot: array length (0 = not array)
    std::map<std::string, Var> globalNames;
    std::map<std::string, Func> funcs;
    std::vector<Ins> initCode;

    void compileUnit(const std::vector<Tok>& toks) {
        t_ = &toks;
        p_ = 0;
        while (peek().k != T_END) topLevel();
    }

    int typeIndex(const Ty& t) {
        for (size_t i = 0; i < types_.size(); i++) {
            const Ty& u = types_[i];
            if (u.flt == t.flt && u.str == t.str && u.obj == t.obj && u.boolean == t.boolean && u.isVoid == t.isVoid && u.sgn == t.sgn && u.bits == t.bits)
                return (int)i;
        }
        types_.push_back(t);
        return (int)types_.size() - 1;
    }

private:
    const BoardInfo& board_;
    std::vector<Ins>& code_;
    std::vector<Ty>& types_;
    std::vector<std::string>& strs_;
    std::vector<std::string>& warns_;
    const std::vector<Tok>* t_ = nullptr;
    size_t p_ = 0;
    std::vector<Ins>* out_ = nullptr;   // where code goes (function body or init)
    Func* fn_ = nullptr;                 // function being compiled
    std::vector<std::map<std::string, Var>> scopes_;
    int nextLocal_ = 0;
    struct Loop { std::vector<int> breaks, continues; bool isSwitch = false; };
    std::vector<Loop> loops_;
    std::set<std::string> warned_;

    // ---- token helpers ----
    const Tok& peek(int k = 0) const { size_t i = std::min(p_ + (size_t)k, t_->size() - 1); return (*t_)[i]; }
    const Tok& next() { const Tok& t = peek(); if (p_ < t_->size() - 1) p_++; return t; }
    bool isOp(const char* s, int k = 0) const { return peek(k).k == T_OP && peek(k).s == s; }
    bool isId(const char* s, int k = 0) const { return peek(k).k == T_ID && peek(k).s == s; }
    bool accept(const char* s) { if (isOp(s) || isId(s)) { next(); return true; } return false; }
    void expect(const char* s) {
        if (!accept(s)) {
            const Tok& t = peek();
            std::string got = t.k == T_END ? "end of file" : t.k == T_STR ? "\"" + t.s + "\"" : t.k == T_NUM ? "a number" : "'" + t.s + "'";
            fail(t.line, std::string("expected '") + s + "' but found " + got);
        }
    }
    int line() const { return peek().line; }
    std::string ident() {
        if (peek().k != T_ID) fail(line(), "expected a name");
        return next().s;
    }
    void warnOnce(const std::string& what, int ln) {
        if (warned_.insert(what).second) {
            char b[300];
            snprintf(b, sizeof b, "line %d: %s isn't simulated (ignored here, it still works on the real board)", ln, what.c_str());
            warns_.push_back(b);
        }
    }

    // ---- emit ----
    int emit(Op op, int a = 0, int b = 0, double d = 0, int ln = -1) {
        Ins i;
        i.op = op; i.a = a; i.b = b; i.d = d; i.line = ln < 0 ? line() : ln;
        out_->push_back(i);
        return (int)out_->size() - 1;
    }
    int here() const { return (int)out_->size(); }
    void patch(int at, int target) { (*out_)[(size_t)at].a = target; }
    int strConst(const std::string& s) {
        for (size_t i = 0; i < strs_.size(); i++) if (strs_[i] == s) return (int)i;
        strs_.push_back(s);
        return (int)strs_.size() - 1;
    }

    // ---- types ----
    bool atType(int k = 0) const {
        const Tok& t = peek(k);
        if (t.k != T_ID) return false;
        if (kTypeWords.count(t.s)) return true;
        // `ClassName varName` (library objects such as Servo, LiquidCrystal)
        return peek(k + 1).k == T_ID && !kTypeWords.count(peek(k + 1).s) && funcs.find(t.s) == funcs.end() &&
               globalNames.find(t.s) == globalNames.end() && !findLocal(t.s);
    }
    Ty parseType(std::string* className = nullptr) {
        Ty t;
        bool unsign = false, any = false;
        int longs = 0;
        std::string base;
        while (peek().k == T_ID) {
            const std::string& w = peek().s;
            if (w == "const" || w == "volatile" || w == "static" || w == "constexpr" || w == "inline" || w == "register") { next(); continue; }
            if (w == "unsigned") { unsign = true; any = true; next(); continue; }
            if (w == "signed") { any = true; next(); continue; }
            if (w == "long") { longs++; any = true; next(); continue; }
            if (w == "short") { base = "short"; any = true; next(); continue; }
            if (kTypeWords.count(w) && base.empty()) { base = w; any = true; next(); continue; }
            if (!any && base.empty()) {  // class name
                base = w;
                next();
                t.obj = true;
                if (className) *className = base;
            }
            break;
        }
        if (!any && base.empty()) fail(line(), "expected a type");
        while (isOp("*") || isOp("&")) next();  // pointers/references: treated as values
        if (t.obj) return t;
        if (base == "void") t.isVoid = true;
        else if (base == "float" || base == "double") t.flt = true;
        else if (base == "bool" || base == "boolean") { t.boolean = true; t.bits = 8; }
        else if (base == "String") t.str = true;
        else if (base == "char" || base == "int8_t") { t.bits = 8; t.sgn = !unsign && base != "char" ? true : !unsign; }
        else if (base == "byte" || base == "uint8_t") { t.bits = 8; t.sgn = false; }
        else if (base == "short" || base == "int16_t") t.bits = 16;
        else if (base == "word" || base == "uint16_t") { t.bits = 16; t.sgn = false; }
        else if (base == "int32_t") t.bits = 32;
        else if (base == "uint32_t" || base == "size_t") { t.bits = 32; t.sgn = false; }
        else if (base == "int64_t") t.bits = 64;
        else if (base == "uint64_t") { t.bits = 64; t.sgn = false; }
        else if (base == "auto") t.flt = true;
        else t.bits = longs >= 2 ? 64 : longs == 1 ? 32 : board_.intBits;  // int / long / unsigned
        if (unsign) t.sgn = false;
        if (base.empty() && longs) t.bits = longs >= 2 ? 64 : 32;
        return t;
    }

    // ---- scopes ----
    const Var* findLocal(const std::string& n) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }
    const Var* findVar(const std::string& n) const {
        if (const Var* v = findLocal(n)) return v;
        auto g = globalNames.find(n);
        return g == globalNames.end() ? nullptr : &g->second;
    }
    int newLocal() {
        int s = nextLocal_++;
        if (fn_) fn_->nlocals = std::max(fn_->nlocals, nextLocal_);
        return s;
    }

    // ---- top level ----
    void topLevel() {
        if (accept(";")) return;
        if (isId("using") || isId("namespace") || isId("typedef") || isId("struct") || isId("class") || isId("enum")) {
            int ln = line();
            if (isId("enum")) { enumDecl(); return; }
            fail(ln, "'" + peek().s + "' isn't supported in the simulator yet");
        }
        int ln = line();
        std::string cls;
        Ty ty = parseType(&cls);
        std::string name = ident();
        if (isOp("(") && !ty.obj) { function(ty, name, ln); return; }
        if (isOp("(") && ty.obj && looksLikeFunction()) { function(ty, name, ln); return; }
        // global variable(s)
        out_ = &initCode;
        for (;;) {
            declareVar(ty, cls, name, true);
            if (!accept(",")) break;
            name = ident();
        }
        expect(";");
        out_ = nullptr;
    }

    bool looksLikeFunction() const {
        // `Type name(Type a, …) {` vs `Class obj(1, 2);`
        size_t depth = 0, k = 0;
        for (;; k++) {
            const Tok& t = peek((int)k);
            if (t.k == T_END) return false;
            if (t.k == T_OP && t.s == "(") depth++;
            else if (t.k == T_OP && t.s == ")") { if (--depth == 0) break; }
        }
        return peek((int)k + 1).k == T_OP && (peek((int)k + 1).s == "{" || peek((int)k + 1).s == ";") &&
               (peek(1).k == T_ID || (peek(1).k == T_OP && peek(1).s == ")"));
    }

    void enumDecl() {
        next();  // enum
        if (peek().k == T_ID) next();
        expect("{");
        double v = 0;
        while (!isOp("}")) {
            std::string n = ident();
            if (accept("=")) {
                bool neg = accept("-");
                if (peek().k != T_NUM) fail(line(), "enum values must be numbers");
                v = next().n * (neg ? -1 : 1);
            }
            Ty t;
            out_ = &initCode;
            Var var;
            var.global = true;
            var.slot = (int)globals.size();
            var.ty = typeIndex(t);
            globals.push_back(var);
            globalArrayLens.push_back(0);
            globalNames[n] = var;
            emit(PUSHN, 0, 0, v);
            emit(STG, var.slot, var.ty);
            emit(POP);
            out_ = nullptr;
            v++;
            if (!accept(",")) break;
        }
        expect("}");
        while (peek().k == T_ID) next();
        expect(";");
    }

    void declareVar(const Ty& ty, const std::string& cls, const std::string& name, bool global) {
        Var v;
        v.global = global;
        v.ty = typeIndex(ty);
        v.isObject = ty.obj;
        v.className = cls;
        v.slot = global ? (int)globals.size() : newLocal();
        bool isArr = false;
        int arrLen = -1;
        if (accept("[")) {
            isArr = true;
            if (!isOp("]")) {
                if (peek().k != T_NUM) {
                    // constant variable as array size: evaluate simple constants only
                    const Var* cv = peek().k == T_ID ? findVar(peek().s) : nullptr;
                    if (!cv) fail(line(), "array size must be a number");
                    fail(line(), "array size must be a number (use #define SIZE 8)");
                }
                arrLen = (int)next().n;
            }
            expect("]");
            if (isOp("[")) fail(line(), "2-D arrays aren't supported in the simulator yet");
        }
        v.isArray = isArr;
        if (global) {
            globals.push_back(v);
            globalArrayLens.push_back(0);
            globalNames[name] = v;
        } else {
            scopes_.back()[name] = v;
        }
        int ln = line();
        if (ty.obj) {  // library object: `Servo s;` or `LiquidCrystal lcd(12, 11, …);`
            if (accept("(")) {
                int depth = 1;
                while (depth && peek().k != T_END) { if (isOp("(")) depth++; if (isOp(")")) depth--; next(); }
            } else if (accept("=")) {
                // `Adafruit_NeoPixel strip = Adafruit_NeoPixel(…)`: skip the initializer
                int depth = 0;
                while (peek().k != T_END && !(depth == 0 && (isOp(",") || isOp(";")))) {
                    if (isOp("(")) depth++;
                    if (isOp(")")) depth--;
                    next();
                }
            }
            return;
        }
        if (isArr) {
            std::vector<int> elems;  // we emit element initializers after allocation
            if (accept("=")) {
                if (peek().k == T_STR && (ty.bits == 8 || ty.str)) {  // char msg[] = "hi" → string
                    std::string s = next().s;
                    v.isArray = false;
                    Ty st;
                    st.str = true;
                    v.ty = typeIndex(st);
                    if (global) { globals.back() = v; globalNames[name] = v; }
                    else scopes_.back()[name] = v;
                    emit(PUSHS, strConst(s));
                    emit(global ? STG : STL, v.slot, v.ty);
                    emit(POP);
                    return;
                }
                expect("{");
                // allocate, then store each element
                std::vector<std::pair<int, int>> dummy;
                int count = 0;
                int allocAt = emit(ALLOC, v.slot, global ? 1 : 0, 0, ln);
                while (!isOp("}")) {
                    emit(global ? LDG : LDL, v.slot);
                    emit(PUSHN, 0, 0, count);
                    expr();
                    emit(STX, 0, v.ty);
                    emit(POP);
                    count++;
                    if (!accept(",")) break;
                }
                expect("}");
                if (arrLen < 0) arrLen = count;
                if (count > arrLen) fail(ln, "too many initializers for array '" + name + "'");
                (*out_)[(size_t)allocAt].d = arrLen;
            } else {
                if (arrLen < 0) fail(ln, "array '" + name + "' needs a size");
                emit(ALLOC, v.slot, global ? 1 : 0, arrLen, ln);
            }
            if (global) { globalArrayLens.back() = arrLen; globals.back().arrLen = arrLen; globalNames[name].arrLen = arrLen; }
            else scopes_.back()[name].arrLen = arrLen;
            return;
        }
        if (accept("=")) expr();
        else if (ty.str) emit(PUSHS, strConst(""));
        else emit(PUSHN, 0, 0, 0);
        emit(global ? STG : STL, v.slot, v.ty, 0, ln);
        emit(POP);
    }

    void function(const Ty& ret, const std::string& name, int ln) {
        Func& f = funcs[name];
        f.name = name;
        f.ret = ret;
        if (f.firstUseLine == 0) f.firstUseLine = ln;
        expect("(");
        std::vector<std::pair<std::string, Ty>> params;
        std::vector<bool> arrayParam;
        if (!(isId("void") && isOp(")", 1))) {
            while (!isOp(")")) {
                std::string cls;
                Ty pt = parseType(&cls);
                std::string pn = peek().k == T_ID ? ident() : "";
                bool arr = false;
                if (accept("[")) { while (!isOp("]")) next(); expect("]"); arr = true; }
                if (accept("=")) { // default argument value: skip
                    while (!isOp(",") && !isOp(")")) next();
                }
                params.push_back({pn, pt});
                arrayParam.push_back(arr);
                if (!accept(",")) break;
            }
        } else next();
        expect(")");
        f.nparams = (int)params.size();
        f.params.clear();
        for (auto& p : params) f.params.push_back(p.second);
        if (accept(";")) return;  // prototype
        if (f.defined) fail(ln, "function '" + name + "' is defined twice");
        f.defined = true;
        // body
        std::vector<Ins> body;
        out_ = &body;
        fn_ = &f;
        scopes_.clear();
        scopes_.emplace_back();
        nextLocal_ = 0;
        f.nlocals = 0;
        for (size_t i = 0; i < params.size(); i++) {
            Var v;
            v.slot = newLocal();
            v.ty = typeIndex(params[i].second);
            v.isArray = arrayParam[i];
            v.isObject = params[i].second.obj;
            if (!params[i].first.empty()) scopes_.back()[params[i].first] = v;
        }
        block();
        emit(PUSHN, 0, 0, 0);  // falling off the end returns 0 / nothing
        emit(RET);
        // append body to the program
        int base = (int)code_.size();
        for (auto ins : body) {
            if (ins.op == JMP || ins.op == JZ || ins.op == JNZ) ins.a += base;
            code_.push_back(ins);
        }
        f.entry = base;
        // functions called before this one was defined
        out_ = nullptr;
        fn_ = nullptr;
        scopes_.clear();
    }

    // ---- statements ----
    void block() {
        expect("{");
        scopes_.emplace_back();
        while (!isOp("}")) {
            if (peek().k == T_END) fail(line(), "missing '}'");
            statement();
        }
        expect("}");
        scopes_.pop_back();
    }

    void statement() {
        int ln = line();
        if (isOp("{")) { block(); return; }
        if (accept(";")) return;
        if (isId("if")) {
            next();
            expect("(");
            expr();
            expect(")");
            int jz = emit(JZ, 0, 0, 0, ln);
            statement();
            if (isId("else")) {
                next();
                int jend = emit(JMP);
                patch(jz, here());
                statement();
                patch(jend, here());
            } else patch(jz, here());
            return;
        }
        if (isId("while")) {
            next();
            int top = here();
            expect("(");
            expr();
            expect(")");
            int jz = emit(JZ, 0, 0, 0, ln);
            loops_.emplace_back();
            statement();
            emit(JMP, top, 0, 0, ln);
            patch(jz, here());
            finishLoop(here(), top);
            return;
        }
        if (isId("do")) {
            next();
            int top = here();
            loops_.emplace_back();
            statement();
            if (!isId("while")) fail(line(), "expected 'while' after do { … }");
            next();
            int cont = here();
            expect("(");
            expr();
            expect(")");
            expect(";");
            emit(JNZ, top, 0, 0, ln);
            finishLoop(here(), cont);
            return;
        }
        if (isId("for")) {
            next();
            expect("(");
            scopes_.emplace_back();
            if (!accept(";")) {
                if (atType()) localDecl();
                else { expr(); emit(POP); expect(";"); }
            }
            int top = here();
            int jz = -1;
            if (!isOp(";")) { expr(); jz = emit(JZ, 0, 0, 0, ln); }
            expect(";");
            // increment is compiled after the body: capture its tokens
            size_t incStart = p_;
            int depth = 0;
            while (!(depth == 0 && isOp(")"))) {
                if (peek().k == T_END) fail(ln, "missing ')' in for");
                if (isOp("(")) depth++;
                if (isOp(")")) depth--;
                next();
            }
            size_t incEnd = p_;
            expect(")");
            loops_.emplace_back();
            statement();
            int cont = here();
            size_t save = p_;
            if (incEnd > incStart) {
                p_ = incStart;
                expr();
                emit(POP);
                p_ = save;
            }
            emit(JMP, top, 0, 0, ln);
            int end = here();
            if (jz >= 0) patch(jz, end);
            finishLoop(end, cont);
            scopes_.pop_back();
            return;
        }
        if (isId("switch")) {
            next();
            expect("(");
            expr();
            expect(")");
            int tmp = newLocal();
            Ty it;
            emit(STL, tmp, typeIndex(it));
            emit(POP);
            expect("{");
            // collect case bodies: first emit a jump table placeholder chain
            int jumpTable = emit(JMP);
            loops_.emplace_back();
            loops_.back().isSwitch = true;
            std::vector<std::pair<double, int>> cases;
            int defaultAt = -1;
            scopes_.emplace_back();
            while (!isOp("}")) {
                if (isId("case")) {
                    next();
                    bool neg = accept("-");
                    double v;
                    if (peek().k == T_NUM) v = next().n;
                    else if (peek().k == T_ID) {
                        std::string cn = next().s;
                        long c;
                        if (!constantValue(cn, c)) fail(line(), "case labels must be constants");
                        v = (double)c;
                    } else fail(line(), "case labels must be constants");
                    if (neg) v = -v;
                    expect(":");
                    cases.push_back({v, here()});
                } else if (isId("default")) {
                    next();
                    expect(":");
                    defaultAt = here();
                } else statement();
            }
            expect("}");
            scopes_.pop_back();
            int jumpOverTable = emit(JMP);
            patch(jumpTable, here());
            for (auto& c : cases) {
                emit(LDL, tmp);
                emit(PUSHN, 0, 0, c.first);
                emit(BIN, B_EQ);
                emit(JNZ, c.second);
            }
            int afterTable = emit(JMP, defaultAt >= 0 ? defaultAt : 0);
            int end = here();
            if (defaultAt < 0) patch(afterTable, end);
            patch(jumpOverTable, end);
            Loop l = loops_.back();
            loops_.pop_back();
            for (int b : l.breaks) patch(b, end);
            if (!l.continues.empty()) {  // continue inside switch belongs to the enclosing loop
                if (loops_.empty()) fail(ln, "'continue' outside a loop");
                for (int c : l.continues) loops_.back().continues.push_back(c);
            }
            return;
        }
        if (isId("break")) {
            next();
            expect(";");
            if (loops_.empty()) fail(ln, "'break' outside a loop or switch");
            loops_.back().breaks.push_back(emit(JMP, 0, 0, 0, ln));
            return;
        }
        if (isId("continue")) {
            next();
            expect(";");
            if (loops_.empty()) fail(ln, "'continue' outside a loop");
            loops_.back().continues.push_back(emit(JMP, 0, 0, 0, ln));
            return;
        }
        if (isId("return")) {
            next();
            if (!isOp(";")) {
                expr();
                if (fn_ && !fn_->ret.isVoid) emit(CONV, typeIndex(fn_->ret));
            } else emit(PUSHN, 0, 0, 0);
            expect(";");
            emit(RET, 0, 0, 0, ln);
            return;
        }
        if (atType()) { localDecl(); return; }
        expr();
        emit(POP);
        expect(";");
    }

    void finishLoop(int breakTarget, int continueTarget) {
        Loop l = loops_.back();
        loops_.pop_back();
        for (int b : l.breaks) patch(b, breakTarget);
        for (int c : l.continues) patch(c, continueTarget);
    }

    void localDecl() {
        std::string cls;
        Ty ty = parseType(&cls);
        for (;;) {
            std::string name = ident();
            declareVar(ty, cls, name, false);
            if (!accept(",")) break;
        }
        expect(";");
    }

    bool constantValue(const std::string& n, long& out) const {
        static const std::map<std::string, long> base = {
            {"HIGH", 1}, {"LOW", 0}, {"INPUT", 0}, {"OUTPUT", 1}, {"INPUT_PULLUP", 2}, {"INPUT_PULLDOWN", 3}, {"true", 1},
            {"false", 0}, {"LSBFIRST", 0}, {"MSBFIRST", 1}, {"DEC", 10}, {"HEX", 16}, {"OCT", 8}, {"BIN", 2},
            {"CHANGE", 1}, {"FALLING", 2}, {"RISING", 3}, {"NULL", 0}, {"nullptr", 0}};
        auto it = base.find(n);
        if (it != base.end()) { out = it->second; return true; }
        if (n == "LED_BUILTIN") { out = board_.ledBuiltin; return true; }
        auto c = board_.constants.find(n);
        if (c != board_.constants.end()) { out = c->second; return true; }
        return false;
    }

    // ---- expressions ----
    // An lvalue whose final load/store hasn't been emitted yet.
    struct LV {
        enum Kind { NONE, VAR, ELEM } kind = NONE;
        Var var;
    };

    void rvalue(LV& lv) {
        if (lv.kind == LV::VAR) emit(lv.var.global ? LDG : LDL, lv.var.slot);
        else if (lv.kind == LV::ELEM) emit(INDEX);
        lv.kind = LV::NONE;
    }

    void expr() { assignment(); }

    void assignment() {
        LV lv;
        ternary(&lv);
        static const char* aops[] = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
        for (auto* o : aops) {
            if (!isOp(o)) continue;
            int ln = line();
            if (lv.kind == LV::NONE) fail(ln, "can't assign to this expression");
            next();
            std::string op = o;
            if (op == "=") {
                assignment();
            } else {
                // load current value
                if (lv.kind == LV::VAR) emit(lv.var.global ? LDG : LDL, lv.var.slot);
                else { emit(DUP2); emit(INDEX); }
                assignment();
                static const std::map<std::string, int> bin = {{"+=", B_ADD}, {"-=", B_SUB}, {"*=", B_MUL}, {"/=", B_DIV}, {"%=", B_MOD},
                                                               {"&=", B_AND}, {"|=", B_OR}, {"^=", B_XOR}, {"<<=", B_SHL}, {">>=", B_SHR}};
                emit(BIN, bin.at(op), 0, 0, ln);
            }
            store(lv, ln);
            return;
        }
        rvalue(lv);
    }

    void store(LV& lv, int ln) {
        if (lv.kind == LV::VAR) emit(lv.var.global ? STG : STL, lv.var.slot, lv.var.ty, 0, ln);
        else emit(STX, 0, lv.var.ty, 0, ln);
        lv.kind = LV::NONE;
    }

    void ternary(LV* lv) {
        logicalOr(lv);
        if (isOp("?")) {
            rvalue(*lv);
            next();
            int jz = emit(JZ);
            expr();
            int jend = emit(JMP);
            expect(":");
            patch(jz, here());
            LV o;
            ternary(&o);
            rvalue(o);
            patch(jend, here());
        }
    }

    void logicalOr(LV* lv) {
        logicalAnd(lv);
        while (isOp("||") || isId("or")) {
            rvalue(*lv);
            next();
            int jt = emit(JNZ);
            LV r;
            logicalAnd(&r);
            rvalue(r);
            int jt2 = emit(JNZ);
            emit(PUSHN, 0, 0, 0);
            int jend = emit(JMP);
            patch(jt, here());
            patch(jt2, here());
            emit(PUSHN, 0, 0, 1);
            patch(jend, here());
        }
    }

    void logicalAnd(LV* lv) {
        binaryLevel(lv, 0);
        while (isOp("&&") || isId("and")) {
            rvalue(*lv);
            next();
            int jf = emit(JZ);
            LV r;
            binaryLevel(&r, 0);
            rvalue(r);
            int jf2 = emit(JZ);
            emit(PUSHN, 0, 0, 1);
            int jend = emit(JMP);
            patch(jf, here());
            patch(jf2, here());
            emit(PUSHN, 0, 0, 0);
            patch(jend, here());
        }
    }

    // precedence levels for binary operators: | ^ & (== !=) (< <= > >=) (<< >>) (+ -) (* / %)
    void binaryLevel(LV* lv, int level) {
        static const std::vector<std::vector<std::pair<const char*, int>>> levels = {
            {{"|", B_OR}}, {{"^", B_XOR}}, {{"&", B_AND}}, {{"==", B_EQ}, {"!=", B_NE}},
            {{"<", B_LT}, {"<=", B_LE}, {">", B_GT}, {">=", B_GE}}, {{"<<", B_SHL}, {">>", B_SHR}},
            {{"+", B_ADD}, {"-", B_SUB}}, {{"*", B_MUL}, {"/", B_DIV}, {"%", B_MOD}}};
        if (level >= (int)levels.size()) { unary(lv); return; }
        binaryLevel(lv, level + 1);
        for (;;) {
            int op = -1;
            for (auto& o : levels[(size_t)level]) if (isOp(o.first)) { op = o.second; break; }
            if (op < 0) break;
            int ln = line();
            rvalue(*lv);
            next();
            LV r;
            binaryLevel(&r, level + 1);
            rvalue(r);
            emit(BIN, op, 0, 0, ln);
        }
    }

    void unary(LV* lv) {
        int ln = line();
        if (accept("-")) { LV o; unary(&o); rvalue(o); emit(UN, U_NEG, 0, 0, ln); return; }
        if (accept("+")) { LV o; unary(&o); rvalue(o); return; }
        if (accept("!") || accept("not")) { LV o; unary(&o); rvalue(o); emit(UN, U_NOT, 0, 0, ln); return; }
        if (accept("~")) { LV o; unary(&o); rvalue(o); emit(UN, U_BNOT, 0, 0, ln); return; }
        if (isOp("++") || isOp("--")) {
            int d = next().s == "++" ? 1 : -1;
            LV o;
            unary(&o);
            incdec(o, d, false, ln);
            return;
        }
        // casts: (int)x, (float)x
        if (isOp("(") && peek(1).k == T_ID && kTypeWords.count(peek(1).s)) {
            size_t save = p_;
            next();
            Ty t = parseType();
            if (accept(")")) {
                LV o;
                unary(&o);
                rvalue(o);
                emit(CONV, typeIndex(t), 0, 0, ln);
                return;
            }
            p_ = save;
        }
        postfix(lv);
    }

    void incdec(LV& lv, int delta, bool post, int ln) {
        if (lv.kind == LV::VAR) emit(INCV, lv.var.slot, lv.var.ty, delta, ln), (*out_).back().b |= (lv.var.global ? 1 << 16 : 0) | (post ? 1 << 17 : 0);
        else if (lv.kind == LV::ELEM) emit(INCX, post ? 1 : 0, lv.var.ty, delta, ln);
        else fail(ln, "++/-- needs a variable");
        lv.kind = LV::NONE;
    }

    void postfix(LV* lv) {
        primary(lv);
        for (;;) {
            int ln = line();
            if (isOp("[")) {
                if (lv->kind != LV::VAR || !lv->var.isArray) fail(ln, "only arrays can be indexed");
                emit(lv->var.global ? LDG : LDL, lv->var.slot);
                next();
                expr();
                expect("]");
                lv->kind = LV::ELEM;
                continue;
            }
            if (isOp("++") || isOp("--")) {
                int d = next().s == "++" ? 1 : -1;
                incdec(*lv, d, true, ln);
                continue;
            }
            if (isOp(".") && lv->kind == LV::VAR) {
                // method on a variable: String methods, or a library object
                next();
                std::string m = ident();
                Var var = lv->var;
                if (var.isObject) {
                    lv->kind = LV::NONE;
                    skipArgs();
                    warnOnce(var.className + "." + m + "()", ln);
                    emit(PUSHN, 0, 0, 0);
                    continue;
                }
                const Ty& vt = types_[(size_t)var.ty];
                if (!vt.str) fail(ln, "'.' only works on String variables and library objects here");
                rvalue(*lv);
                int id = builtinId("str." + m);
                if (id < 0) fail(ln, "String." + m + "() isn't supported in the simulator");
                int n = 1 + args();
                emit(BUILTIN, id, n, 0, ln);
                continue;
            }
            break;
        }
    }

    int args() {
        expect("(");
        int n = 0;
        while (!isOp(")")) {
            expr();
            n++;
            if (!accept(",")) break;
        }
        expect(")");
        return n;
    }
    void skipArgs() {
        if (!isOp("(")) return;
        int depth = 0;
        do {
            if (isOp("(")) depth++;
            if (isOp(")")) depth--;
            next();
        } while (depth > 0 && peek().k != T_END);
    }

    void primary(LV* lv) {
        const Tok& t = peek();
        int ln = t.line;
        if (t.k == T_NUM) { next(); emit(PUSHN, t.flt ? 1 : 0, 0, t.n, ln); return; }
        if (t.k == T_STR) {
            std::string s = next().s;
            while (peek().k == T_STR) s += next().s;  // "a" "b"
            emit(PUSHS, strConst(s), 0, 0, ln);
            return;
        }
        if (accept("(")) { LV o; expr(); expect(")"); return; }
        if (t.k != T_ID) fail(ln, "unexpected '" + t.s + "'");
        std::string name = next().s;
        if (name == "F" && isOp("(")) {  // F("text") just returns the text
            next();
            expr();
            expect(")");
            return;
        }
        if (name == "sizeof") {
            expect("(");
            std::string vn = ident();
            const Var* v = findVar(vn);
            bool elem = false;
            if (accept("[")) { while (!isOp("]")) next(); expect("]"); elem = true; }
            expect(")");
            if (!v) fail(ln, "sizeof needs a variable");
            const Ty& ty = types_[(size_t)v->ty];
            int bytes = ty.flt ? 4 : std::max(1, ty.bits / 8);
            int n = (v->isArray && !elem) ? std::max(0, v->arrLen) * bytes : bytes;
            if (v->isArray && !elem && v->arrLen <= 0) fail(ln, "sizeof of an array parameter isn't supported");
            emit(PUSHN, 0, 0, n, ln);
            return;
        }
        // Serial.xxx(), WiFi.xxx(), obj.xxx()
        if (isOp(".") && !findVar(name)) {
            next();
            std::string m = ident();
            std::string full = name + "." + m;
            if (name == "Serial1" || name == "Serial2" || name == "SerialUSB") full = "Serial." + m;
            int id = builtinId(full);
            if (id >= 0 && isOp("(")) { int n = args(); emit(BUILTIN, id, n, 0, ln); return; }
            warnOnce(full + (isOp("(") ? "()" : ""), ln);
            skipArgs();
            emit(PUSHN, 0, 0, 0, ln);
            return;
        }
        if (isOp("(")) {
            // casts written as calls: int(x), float(x), byte(x)
            if (kTypeWords.count(name) && name != "String") {
                Ty ct;
                // reuse parseType on a synthetic token sequence: simplest is a small switch
                if (name == "float" || name == "double") ct.flt = true;
                else if (name == "byte" || name == "uint8_t") { ct.bits = 8; ct.sgn = false; }
                else if (name == "char") ct.bits = 8;
                else if (name == "bool" || name == "boolean") { ct.boolean = true; ct.bits = 8; }
                else if (name == "long") ct.bits = 32;
                else if (name == "word") { ct.bits = 16; ct.sgn = false; }
                else ct.bits = board_.intBits;
                expect("(");
                expr();
                expect(")");
                emit(CONV, typeIndex(ct), 0, 0, ln);
                return;
            }
            // bitWrite/bitSet/bitClear modify their first argument
            if (name == "bitSet" || name == "bitClear" || name == "bitWrite") {
                expect("(");
                LV target;
                postfix(&target);
                if (target.kind == LV::NONE) fail(ln, name + " needs a variable");
                if (target.kind == LV::VAR) emit(target.var.global ? LDG : LDL, target.var.slot);
                else { emit(DUP2); emit(INDEX); }
                expect(",");
                expr();  // bit number
                if (name == "bitWrite") { expect(","); expr(); }
                expect(")");
                emit(BUILTIN, builtinId("noop"), 0, 0, ln);  // placeholder replaced below
                out_->pop_back();
                // value bit [b]: compute with BIN ops
                if (name == "bitSet") { emitBitOp(ln, 1); }
                else if (name == "bitClear") { emitBitOp(ln, 0); }
                else emitBitOp(ln, 2);
                store(target, ln);
                return;
            }
            if (name == "attachInterrupt" || name == "detachInterrupt" || name == "interrupts" || name == "noInterrupts" ||
                name == "digitalPinToInterrupt" || name == "pulseIn" || name == "touchRead" || name == "esp_deep_sleep_start") {
                warnOnce(name + "()", ln);
                skipArgs();
                emit(PUSHN, 0, 0, 0, ln);
                return;
            }
            auto fit = funcs.find(name);
            int bid = builtinId(name);
            if (fit == funcs.end() && bid >= 0) {
                int n = args();
                emit(BUILTIN, bid, n, 0, ln);
                return;
            }
            Func& f = funcs[name];
            f.name = name;
            if (f.firstUseLine == 0) f.firstUseLine = ln;
            int n = args();
            // the callee is linked by name once every function is known (d = -(name string + 1))
            emit(CALL, 0, n, -(double)(strConst(name) + 1), ln);
            return;
        }
        if (const Var* v = findVar(name)) {
            lv->kind = LV::VAR;
            lv->var = *v;
            return;
        }
        long c;
        if (constantValue(name, c)) { emit(PUSHN, 0, 0, (double)c, ln); return; }
        if (name == "PI" || name == "M_PI") { emit(PUSHN, 1, 0, M_PI, ln); return; }
        if (name == "HALF_PI") { emit(PUSHN, 1, 0, M_PI / 2, ln); return; }
        if (name == "TWO_PI") { emit(PUSHN, 1, 0, M_PI * 2, ln); return; }
        if (name == "DEG_TO_RAD") { emit(PUSHN, 1, 0, M_PI / 180, ln); return; }
        if (name == "RAD_TO_DEG") { emit(PUSHN, 1, 0, 180 / M_PI, ln); return; }
        if (name == "Serial") { emit(PUSHN, 0, 0, 1, ln); return; }  // while (!Serial)
        fail(ln, "'" + name + "' was not declared");
    }

    // stack: value bit [b] → new value. mode 1 set, 0 clear, 2 write
    void emitBitOp(int ln, int mode) {
        if (mode == 2) {
            // v n b  →  b ? v | (1<<n) : v & ~(1<<n)
            int tmpB = newLocal(), tmpN = newLocal(), tmpV = newLocal();
            Ty it;
            it.bits = 64;
            int ti = typeIndex(it);
            emit(STL, tmpB, ti); emit(POP);
            emit(STL, tmpN, ti); emit(POP);
            emit(STL, tmpV, ti); emit(POP);
            emit(LDL, tmpB);
            int jz = emit(JZ);
            emit(LDL, tmpV); emit(PUSHN, 0, 0, 1); emit(LDL, tmpN); emit(BIN, B_SHL); emit(BIN, B_OR);
            int je = emit(JMP);
            patch(jz, here());
            emit(LDL, tmpV); emit(PUSHN, 0, 0, 1); emit(LDL, tmpN); emit(BIN, B_SHL); emit(UN, U_BNOT); emit(BIN, B_AND);
            patch(je, here());
            (void)ln;
            return;
        }
        // v n → v | (1<<n)   or   v & ~(1<<n)
        int tmpN = newLocal();
        Ty it;
        it.bits = 64;
        emit(STL, tmpN, typeIndex(it)); emit(POP);
        emit(PUSHN, 0, 0, 1); emit(LDL, tmpN); emit(BIN, B_SHL);
        if (mode == 1) emit(BIN, B_OR);
        else { emit(UN, U_BNOT); emit(BIN, B_AND); }
    }

public:
    int initLocals() const { return std::max(8, nextLocal_ + 4); }
};

}  // namespace

// ===========================================================================
// VM
// ===========================================================================
struct Sketch::Impl {
    BoardInfo board;
    std::vector<Ins> code;
    std::vector<Ty> types;
    std::vector<std::string> strs;
    std::vector<std::string> warns;
    std::vector<int> funcEntry, funcLocals;
    std::vector<std::vector<int>> funcParams;  // parameter type indexes
    int globalCount = 0;

    // state
    std::vector<Val> stack, globals, locals;
    std::vector<std::vector<Val>> arrays;
    struct Frame { int retPc, localBase, arrMark; };
    std::vector<Frame> frames;
    int pc = 0;
    bool ok = false, stopped = false;
    std::string error;
    int errorLine = 0, curLine = 0;
    double now = 0, wakeAt = 0, microAcc = 0;
    std::map<int, PinState> pins;
    std::map<int, double> pinChangedAt;
    std::map<int, double> toneUntil;
    std::map<int, int> ledcChannelPin, ledcBits;
    std::string serialOut, serialIn;
    uint64_t rng = 0x2545F4914F6CDD1DULL;
    int analogBits = 10, pwmBits = 8;
    std::set<std::string> runtimeWarned;
    Host* host = nullptr;

    [[noreturn]] void runtimeFail(const std::string& m) { throw Err{curLine, m}; }

    Val pop() {
        if (stack.empty()) runtimeFail("internal error: stack underflow");
        Val v = std::move(stack.back());
        stack.pop_back();
        return v;
    }

    static std::string fmtNum(const Val& v, int base = 10, int decimals = 2) {
        char b[96];
        if (!v.isInt) {
            snprintf(b, sizeof b, "%.*f", std::clamp(decimals, 0, 10), v.n);
            return b;
        }
        long long n = (long long)v.n;
        if (base == 10) { snprintf(b, sizeof b, "%lld", n); return b; }
        unsigned long long u = (unsigned long long)n;
        if (base == 16) { snprintf(b, sizeof b, "%llX", u); return b; }
        if (base == 8) { snprintf(b, sizeof b, "%llo", u); return b; }
        std::string s;
        if (u == 0) return "0";
        while (u) { s.insert(s.begin(), (char)('0' + (u & 1))); u >>= 1; }
        return s;
    }
    static std::string toStr(const Val& v) { return v.isStr() ? *v.s : fmtNum(v); }

    Val convert(const Val& v, int tyIdx) {
        const Ty& t = types[(size_t)tyIdx];
        if (t.str) return v.isStr() ? v : str(fmtNum(v));
        if (t.obj || t.isVoid) return v;
        Val out;
        out.arr = v.arr;
        double n = v.isStr() ? std::atof(v.s->c_str()) : v.n;
        if (t.flt) { out.n = n; out.isInt = false; return out; }
        if (t.boolean) { out.n = n != 0; return out; }
        if (v.arr >= 0) return v;
        // integer: truncate toward zero, then wrap to the declared width
        double tr = std::trunc(n);
        if (!std::isfinite(tr)) tr = 0;
        if (t.bits >= 64) { out.n = tr; return out; }
        uint64_t mask = (1ULL << t.bits) - 1;
        uint64_t u = (uint64_t)(int64_t)std::fmod(tr, 18446744073709551616.0) & mask;
        if (t.sgn && (u >> (t.bits - 1)) & 1) out.n = (double)(int64_t)(u | ~mask);
        else out.n = (double)u;
        return out;
    }

    Val binop(int op, const Val& a, const Val& b) {
        if (op == B_ADD && (a.isStr() || b.isStr())) return str(toStr(a) + toStr(b));
        if ((op == B_EQ || op == B_NE) && a.isStr() && b.isStr()) return num((*a.s == *b.s) == (op == B_EQ), true);
        bool ints = a.isInt && b.isInt;
        double x = a.isStr() ? std::atof(a.s->c_str()) : a.n, y = b.isStr() ? std::atof(b.s->c_str()) : b.n;
        auto I = [](double d) { return (int64_t)d; };
        switch (op) {
            case B_ADD: return num(x + y, ints);
            case B_SUB: return num(x - y, ints);
            case B_MUL: return num(x * y, ints);
            case B_DIV:
                if (ints) { if (I(y) == 0) runtimeFail("division by zero"); return num((double)(I(x) / I(y)), true); }
                return num(x / y, false);
            case B_MOD:
                if (ints) { if (I(y) == 0) runtimeFail("division by zero (%)"); return num((double)(I(x) % I(y)), true); }
                return num(std::fmod(x, y), false);
            case B_SHL: return num((double)(I(x) << (I(y) & 63)), true);
            case B_SHR: return num((double)(I(x) >> (I(y) & 63)), true);
            case B_AND: return num((double)(I(x) & I(y)), true);
            case B_OR: return num((double)(I(x) | I(y)), true);
            case B_XOR: return num((double)(I(x) ^ I(y)), true);
            case B_LT: return num(x < y, true);
            case B_LE: return num(x <= y, true);
            case B_GT: return num(x > y, true);
            case B_GE: return num(x >= y, true);
            case B_EQ: return num(x == y, true);
            case B_NE: return num(x != y, true);
        }
        return num(0, true);
    }

    int resolve(long pin) {
        int pp = board.resolvePin ? board.resolvePin(pin) : -1;
        if (pp < 0) {
            std::string k = "pin" + std::to_string(pin);
            if (runtimeWarned.insert(k).second) {
                char b[160];
                snprintf(b, sizeof b, "line %d: pin %ld isn't on this board's header (writes to it are not shown)", curLine, pin);
                warns.push_back(b);
            }
        }
        return pp;
    }

    // Returns false when the instruction must wait for the next millisecond (re-toggling a pin).
    bool setOutput(long pin, double level) {
        int pp = resolve(pin);
        if (pp < 0) return true;
        PinState& ps = pins[pp];
        if (ps.mode != PM_OUTPUT) {
            if (board.family == "avr" && (ps.mode == PM_INPUT || ps.mode == PM_UNSET) && level < 1) {}
            else if (ps.mode == PM_UNSET || ps.mode == PM_INPUT) {
                if (runtimeWarned.insert("nomode" + std::to_string(pin)).second) {
                    char b[200];
                    snprintf(b, sizeof b, "line %d: digitalWrite(%ld, …) without pinMode(%ld, OUTPUT) first", curLine, pin, pin);
                    warns.push_back(b);
                }
                if (board.family == "avr") {  // real AVR: HIGH on an input turns on the pull-up (LEDs glow dimly)
                    ps.mode = level >= 1 ? PM_INPUT_PULLUP : PM_INPUT;
                    return true;
                }
            }
        }
        if (ps.mode == PM_OUTPUT && ps.level != level) {
            auto it = pinChangedAt.find(pp);
            if (it != pinChangedAt.end() && it->second == now) return false;  // let the circuit see the previous level first
            pinChangedAt[pp] = now;
        }
        if (ps.mode == PM_OUTPUT || ps.mode == PM_UNSET) { ps.mode = PM_OUTPUT; ps.level = level; }
        return true;
    }

    double readVolts(int pp) { return host && host->readVoltage ? host->readVoltage(pp) : NAN; }

    // Execute a builtin. Returns false to retry it next millisecond.
    bool builtin(int id, std::vector<Val>& a) {
        auto N = [&](size_t i) { return i < a.size() ? (a[i].isStr() ? std::atof(a[i].s->c_str()) : a[i].n) : 0.0; };
        auto push = [&](Val v) { stack.push_back(std::move(v)); };
        const std::string name = kBuiltins[id];
        auto need = [&](size_t n) { if (a.size() < n) runtimeFail(name + "() needs " + std::to_string(n) + " argument(s)"); };
        if (name == "pinMode") {
            need(2);
            int pp = resolve((long)N(0));
            if (pp >= 0) {
                int m = (int)N(1);
                pins[pp].mode = m == 1 ? PM_OUTPUT : m == 2 ? PM_INPUT_PULLUP : m == 3 ? PM_INPUT_PULLDOWN : PM_INPUT;
                if (pins[pp].mode != PM_OUTPUT) pins[pp].level = 0;
            }
            push(num(0, true));
        } else if (name == "digitalWrite") {
            need(2);
            if (!setOutput((long)N(0), N(1) != 0 ? 1.0 : 0.0)) return false;
            push(num(0, true));
        } else if (name == "digitalRead") {
            need(1);
            int pp = resolve((long)N(0));
            int r = 0;
            if (pp >= 0) {
                PinState& ps = pins[pp];
                double v = readVolts(pp);
                if (ps.mode == PM_OUTPUT) r = ps.level >= 0.5;
                else if (std::isnan(v)) r = ps.mode == PM_INPUT_PULLUP ? 1 : 0;
                else r = v > board.vref * 0.5 ? 1 : 0;
            }
            push(num(r, true));
        } else if (name == "analogRead") {
            need(1);
            long pin = (long)N(0);
            if (board.family == "avr" && pin >= 0 && pin < 8) pin += board.constants.count("A0") ? board.constants.at("A0") : 14;  // analogRead(0) = A0
            int pp = resolve(pin);
            double v = pp >= 0 ? readVolts(pp) : NAN;
            if (std::isnan(v)) v = 0;
            double full = std::pow(2.0, analogBits) - 1;
            push(num(std::round(std::clamp(v / board.vref, 0.0, 1.0) * full), true));
        } else if (name == "analogWrite") {
            need(2);
            double full = std::pow(2.0, pwmBits) - 1;
            long pin = (long)N(0);
            int pp = resolve(pin);
            if (pp >= 0) { pins[pp].mode = PM_OUTPUT; pins[pp].level = std::clamp(N(1) / full, 0.0, 1.0); }
            push(num(0, true));
        } else if (name == "delay") {
            need(1);
            double ms = std::max(0.0, N(0));
            if (ms > 0) wakeAt = now + ms;
            push(num(0, true));
        } else if (name == "delayMicroseconds") {
            need(1);
            microAcc += std::max(0.0, N(0));
            if (microAcc >= 1000) { double ms = std::floor(microAcc / 1000); microAcc -= ms * 1000; wakeAt = now + ms; }
            push(num(0, true));
        } else if (name == "millis") push(num(std::floor(now + microAcc / 1000), true));
        else if (name == "micros") push(num(std::floor(now * 1000 + microAcc), true));
        else if (name == "tone" || name == "ledcWriteTone") {
            need(2);
            long pin = (long)N(0);
            if (name == "ledcWriteTone") { auto it = ledcChannelPin.find((int)pin); if (it != ledcChannelPin.end()) pin = it->second; }
            int pp = resolve(pin);
            if (pp >= 0) {
                pins[pp].mode = PM_OUTPUT;
                pins[pp].level = N(1) > 0 ? 0.5 : 0;
                pins[pp].toneHz = N(1);
                if (a.size() > 2) toneUntil[pp] = now + N(2);
                else toneUntil.erase(pp);
            }
            push(num(0, true));
        } else if (name == "noTone") {
            int pp = resolve((long)N(0));
            if (pp >= 0) { pins[pp].level = 0; pins[pp].toneHz = 0; toneUntil.erase(pp); }
            push(num(0, true));
        } else if (name == "map") {
            need(5);
            double x = N(0), il = N(1), ih = N(2), ol = N(3), oh = N(4);
            bool ints = a[0].isInt && a[1].isInt && a[2].isInt && a[3].isInt && a[4].isInt;
            if (ih == il) runtimeFail("map(): the input range is empty");
            double r = (x - il) * (oh - ol) / (ih - il) + ol;
            push(num(ints ? std::trunc(r) : r, ints));
        } else if (name == "constrain") {
            need(3);
            push(num(std::clamp(N(0), std::min(N(1), N(2)), std::max(N(1), N(2))), a[0].isInt && a[1].isInt && a[2].isInt));
        } else if (name == "min" || name == "max") {
            need(2);
            double r = name == "min" ? std::min(N(0), N(1)) : std::max(N(0), N(1));
            push(num(r, a[0].isInt && a[1].isInt));
        } else if (name == "abs" || name == "fabs") push(num(std::fabs(N(0)), name == "abs" && !a.empty() && a[0].isInt));
        else if (name == "sq") push(num(N(0) * N(0), !a.empty() && a[0].isInt));
        else if (name == "sqrt") push(num(std::sqrt(N(0)), false));
        else if (name == "pow") push(num(std::pow(N(0), N(1)), false));
        else if (name == "sin") push(num(std::sin(N(0)), false));
        else if (name == "cos") push(num(std::cos(N(0)), false));
        else if (name == "tan") push(num(std::tan(N(0)), false));
        else if (name == "atan") push(num(std::atan(N(0)), false));
        else if (name == "asin") push(num(std::asin(N(0)), false));
        else if (name == "acos") push(num(std::acos(N(0)), false));
        else if (name == "atan2") push(num(std::atan2(N(0), N(1)), false));
        else if (name == "exp") push(num(std::exp(N(0)), false));
        else if (name == "log") push(num(std::log(N(0)), false));
        else if (name == "log10") push(num(std::log10(N(0)), false));
        else if (name == "fmod") push(num(std::fmod(N(0), N(1)), false));
        else if (name == "isnan") push(num(std::isnan(N(0)), true));
        else if (name == "floor") push(num(std::floor(N(0)), false));
        else if (name == "ceil") push(num(std::ceil(N(0)), false));
        else if (name == "round") push(num(std::round(N(0)), true));
        else if (name == "radians") push(num(N(0) * M_PI / 180, false));
        else if (name == "degrees") push(num(N(0) * 180 / M_PI, false));
        else if (name == "random") {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
            double lo = a.size() >= 2 ? N(0) : 0, hi = a.size() >= 2 ? N(1) : N(0);
            push(num(hi > lo ? lo + (double)(rng % (uint64_t)std::max(1.0, hi - lo)) : lo, true));
        } else if (name == "randomSeed") { rng = (uint64_t)N(0) * 2654435761ULL + 1; push(num(0, true)); }
        else if (name == "bitRead") push(num((double)(((int64_t)N(0) >> (int)N(1)) & 1), true));
        else if (name == "highByte") push(num((double)(((int64_t)N(0) >> 8) & 0xFF), true));
        else if (name == "lowByte") push(num((double)((int64_t)N(0) & 0xFF), true));
        else if (name == "bit") push(num((double)(1LL << (int)N(0)), true));
        else if (name == "Serial.begin" || name == "Serial.flush" || name == "Serial.end" || name == "yield" || name == "noop") push(num(0, true));
        else if (name == "Serial.print" || name == "Serial.println") {
            std::string s;
            if (!a.empty()) {
                if (a[0].isStr()) s = *a[0].s;
                else if (a.size() > 1) s = a[0].isInt ? fmtNum(a[0], (int)N(1)) : fmtNum(a[0], 10, (int)N(1));
                else s = fmtNum(a[0]);
            }
            if (name == "Serial.println") s += "\n";
            serialOut += s;
            push(num((double)s.size(), true));
        } else if (name == "Serial.write") {
            if (!a.empty()) serialOut += a[0].isStr() ? *a[0].s : std::string(1, (char)(int)N(0));
            push(num(1, true));
        } else if (name == "Serial.printf") {
            std::string fmt = a.empty() || !a[0].isStr() ? "" : *a[0].s, outS;
            size_t ai = 1;
            for (size_t i = 0; i < fmt.size(); i++) {
                if (fmt[i] != '%' || i + 1 >= fmt.size()) { outS += fmt[i]; continue; }
                size_t j = i + 1;
                std::string spec = "%";
                while (j < fmt.size() && std::strchr("-+ #0123456789.l", fmt[j])) { if (fmt[j] != 'l') spec += fmt[j]; j++; }
                if (j >= fmt.size()) break;
                char conv = fmt[j];
                char b[128] = "";
                if (conv == '%') outS += '%';
                else if (ai < a.size()) {
                    const Val& v = a[ai++];
                    if (conv == 's') snprintf(b, sizeof b, (spec + "s").c_str(), toStr(v).c_str());
                    else if (conv == 'f' || conv == 'g' || conv == 'e') snprintf(b, sizeof b, (spec + conv).c_str(), v.n);
                    else if (conv == 'c') snprintf(b, sizeof b, "%c", (char)(int)v.n);
                    else snprintf(b, sizeof b, (spec + "ll" + conv).c_str(), (long long)v.n);
                    outS += b;
                }
                i = j;
            }
            serialOut += outS;
            push(num((double)outS.size(), true));
        } else if (name == "Serial.available") push(num((double)serialIn.size(), true));
        else if (name == "Serial.read" || name == "Serial.peek") {
            if (serialIn.empty()) push(num(-1, true));
            else { int c = (unsigned char)serialIn[0]; if (name == "Serial.read") serialIn.erase(0, 1); push(num(c, true)); }
        } else if (name == "Serial.readString") { push(str(serialIn)); serialIn.clear(); }
        else if (name == "Serial.parseInt") {
            size_t i = 0;
            while (i < serialIn.size() && !std::isdigit((unsigned char)serialIn[i]) && serialIn[i] != '-') i++;
            size_t j = i + (i < serialIn.size() && serialIn[i] == '-' ? 1 : 0);
            while (j < serialIn.size() && std::isdigit((unsigned char)serialIn[j])) j++;
            long v = std::atol(serialIn.substr(i, j - i).c_str());
            serialIn.erase(0, j);
            push(num((double)v, true));
        } else if (name == "String") {
            if (a.empty()) push(str(""));
            else if (a[0].isStr()) push(a[0]);
            else if (a.size() > 1) push(str(a[0].isInt ? fmtNum(a[0], (int)N(1)) : fmtNum(a[0], 10, (int)N(1))));
            else push(str(fmtNum(a[0])));
        } else if (name == "analogReadResolution") { analogBits = std::clamp((int)N(0), 1, 16); push(num(0, true)); }
        else if (name == "analogWriteResolution") { pwmBits = std::clamp((int)N(0), 1, 16); push(num(0, true)); }
        else if (name == "ledcSetup") { ledcBits[(int)N(0)] = (int)N(2); push(num(1, true)); }
        else if (name == "ledcAttachPin") { ledcChannelPin[(int)N(1)] = (int)N(0); push(num(0, true)); }
        else if (name == "ledcAttach") { ledcChannelPin[(int)N(0)] = (int)N(0); ledcBits[(int)N(0)] = (int)N(2); push(num(1, true)); }
        else if (name == "ledcWrite") {
            int ch = (int)N(0);
            auto it = ledcChannelPin.find(ch);
            long pin = it == ledcChannelPin.end() ? ch : it->second;  // core 3.x: ledcWrite(pin, duty)
            int bits = ledcBits.count(ch) ? ledcBits[ch] : 8;
            int pp = resolve(pin);
            if (pp >= 0) { pins[pp].mode = PM_OUTPUT; pins[pp].level = std::clamp(N(1) / (std::pow(2.0, bits) - 1), 0.0, 1.0); }
            push(num(0, true));
        } else if (name.rfind("str.", 0) == 0) {
            std::string s = a.empty() ? "" : toStr(a[0]);
            std::string m = name.substr(4);
            auto S = [&](size_t i) { return i < a.size() ? toStr(a[i]) : std::string(); };
            if (m == "length") push(num((double)s.size(), true));
            else if (m == "toInt") push(num((double)std::atol(s.c_str()), true));
            else if (m == "toFloat") push(num(std::atof(s.c_str()), false));
            else if (m == "charAt") { size_t i = (size_t)N(1); push(num(i < s.size() ? (unsigned char)s[i] : 0, true)); }
            else if (m == "substring") {
                size_t from = (size_t)std::max(0.0, N(1)), to = a.size() > 2 ? (size_t)std::max(0.0, N(2)) : s.size();
                from = std::min(from, s.size());
                to = std::min(std::max(to, from), s.size());
                push(str(s.substr(from, to - from)));
            } else if (m == "indexOf") { size_t f = s.find(S(1)); push(num(f == std::string::npos ? -1 : (double)f, true)); }
            else if (m == "equals") push(num(s == S(1), true));
            else if (m == "startsWith") push(num(s.rfind(S(1), 0) == 0, true));
            else if (m == "endsWith") { std::string t = S(1); push(num(s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0, true)); }
            else if (m == "concat") push(str(s + S(1)));
            else if (m == "toUpperCase") { for (auto& c : s) c = (char)std::toupper((unsigned char)c); push(str(s)); }
            else if (m == "toLowerCase") { for (auto& c : s) c = (char)std::tolower((unsigned char)c); push(str(s)); }
            else if (m == "trim") { size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n"); push(str(b == std::string::npos ? "" : s.substr(b, e - b + 1))); }
            else if (m == "replace") { std::string f = S(1), r = S(2); size_t p = 0; while (!f.empty() && (p = s.find(f, p)) != std::string::npos) { s.replace(p, f.size(), r); p += r.size(); } push(str(s)); }
            else if (m == "c_str") push(str(s));
            else push(num(0, true));
        } else push(num(0, true));
        return true;
    }

    void step(int budget) {
        while (budget-- > 0 && !stopped) {
            if (pc < 0 || pc >= (int)code.size()) runtimeFail("internal error: bad jump");
            const Ins& in = code[(size_t)pc];
            if (in.line > 0) curLine = in.line;
            pc++;
            switch (in.op) {
                case PUSHN: stack.push_back(num(in.d, in.a == 0 && std::trunc(in.d) == in.d)); break;
                case PUSHS: stack.push_back(str(strs[(size_t)in.a])); break;
                case LDG: stack.push_back(globals[(size_t)in.a]); break;
                case LDL: stack.push_back(locals[(size_t)(frames.back().localBase + in.a)]); break;
                case STG: {
                    Val v = convert(stack.back(), in.b);
                    globals[(size_t)in.a] = v;
                    stack.back() = v;
                    break;
                }
                case STL: {
                    Val v = convert(stack.back(), in.b);
                    locals[(size_t)(frames.back().localBase + in.a)] = v;
                    stack.back() = v;
                    break;
                }
                case INDEX: {
                    Val i = pop(), arr = pop();
                    if (arr.arr < 0) runtimeFail("not an array");
                    auto& A = arrays[(size_t)arr.arr];
                    long k = (long)i.n;
                    if (k < 0 || k >= (long)A.size()) runtimeFail("array index " + std::to_string(k) + " is out of range (size " + std::to_string(A.size()) + ")");
                    stack.push_back(A[(size_t)k]);
                    break;
                }
                case STX: {
                    Val v = pop(), i = pop(), arr = pop();
                    if (arr.arr < 0) runtimeFail("not an array");
                    auto& A = arrays[(size_t)arr.arr];
                    long k = (long)i.n;
                    if (k < 0 || k >= (long)A.size()) runtimeFail("array index " + std::to_string(k) + " is out of range (size " + std::to_string(A.size()) + ")");
                    A[(size_t)k] = convert(v, in.b);
                    stack.push_back(A[(size_t)k]);
                    break;
                }
                case DUP: stack.push_back(stack.back()); break;
                case DUP2: { Val b = stack[stack.size() - 1], a = stack[stack.size() - 2]; stack.push_back(a); stack.push_back(b); break; }
                case POP: pop(); break;
                case BIN: { Val b = pop(), a = pop(); stack.push_back(binop(in.a, a, b)); break; }
                case UN: {
                    Val a = pop();
                    double x = a.isStr() ? std::atof(a.s->c_str()) : a.n;
                    if (in.a == U_NEG) stack.push_back(num(-x, a.isInt));
                    else if (in.a == U_NOT) stack.push_back(num(x == 0, true));
                    else stack.push_back(num((double)~(int64_t)x, true));
                    break;
                }
                case JMP: pc = in.a; break;
                case JZ: { Val v = pop(); if ((v.isStr() ? !v.s->empty() : v.n != 0) == false) pc = in.a; break; }
                case JNZ: { Val v = pop(); if (v.isStr() ? !v.s->empty() : v.n != 0) pc = in.a; break; }
                case CALL: {
                    if (frames.size() > 400) runtimeFail("too much recursion (stack overflow)");
                    int f = in.a;
                    int base = (int)locals.size();
                    locals.resize((size_t)(base + std::max(funcLocals[(size_t)f], in.b)));
                    for (int k = in.b - 1; k >= 0; k--) {
                        Val v = pop();
                        const auto& pt = funcParams[(size_t)f];
                        locals[(size_t)(base + k)] = (k < (int)pt.size() && v.arr < 0) ? convert(v, pt[(size_t)k]) : v;
                    }
                    frames.push_back({pc, base, (int)arrays.size()});
                    pc = funcEntry[(size_t)f];
                    break;
                }
                case RET: {
                    Val r = pop();
                    Frame fr = frames.back();
                    frames.pop_back();
                    locals.resize((size_t)fr.localBase);
                    size_t keep = (size_t)std::max(fr.arrMark, globalArrayCount);  // global arrays live forever
                    if (arrays.size() > keep) arrays.resize(keep);
                    pc = fr.retPc;
                    stack.push_back(r);
                    break;
                }
                case BUILTIN: {
                    std::vector<Val> args((size_t)in.b);
                    for (int k = in.b - 1; k >= 0; k--) args[(size_t)k] = pop();
                    if (!builtin(in.a, args)) {
                        for (auto& v : args) stack.push_back(v);  // retry next millisecond
                        pc--;
                        return;
                    }
                    if (wakeAt > now) return;
                    break;
                }
                case CONV: stack.back() = convert(stack.back(), in.a); break;
                case INCV: {
                    bool global = (in.b >> 16) & 1, post = (in.b >> 17) & 1;
                    int ty = in.b & 0xFFFF;
                    Val& slot = global ? globals[(size_t)in.a] : locals[(size_t)(frames.back().localBase + in.a)];
                    Val old = slot;
                    slot = convert(num(slot.n + in.d, slot.isInt), ty);
                    stack.push_back(post ? old : slot);
                    break;
                }
                case INCX: {
                    Val i = pop(), arr = pop();
                    if (arr.arr < 0) runtimeFail("not an array");
                    auto& A = arrays[(size_t)arr.arr];
                    long k = (long)i.n;
                    if (k < 0 || k >= (long)A.size()) runtimeFail("array index " + std::to_string(k) + " is out of range");
                    Val old = A[(size_t)k];
                    A[(size_t)k] = convert(num(old.n + in.d, old.isInt), in.b);
                    stack.push_back(in.a ? old : A[(size_t)k]);
                    break;
                }
                case ALLOC: {
                    int len = std::max(0, (int)in.d);
                    if (arrays.size() > 100000) runtimeFail("too many arrays (an array declared inside an endless loop?)");
                    arrays.emplace_back((size_t)len);
                    Val ref;
                    ref.arr = (int)arrays.size() - 1;
                    if (in.b) { globals[(size_t)in.a] = ref; globalArrayCount = (int)arrays.size(); }
                    else locals[(size_t)(frames.back().localBase + in.a)] = ref;
                    break;
                }
                case START_LOOP: break;
            }
            if (stack.size() > 20000) runtimeFail("expression stack overflow");
        }
    }
    int globalArrayCount = 0;
};

Sketch::Sketch() : impl_(new Impl) {}
Sketch::~Sketch() = default;

static const char* kPrelude =
    "void shiftOut(int dataPin, int clockPin, int bitOrder, int val) {\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    if (bitOrder == LSBFIRST) digitalWrite(dataPin, (val >> i) & 1); else digitalWrite(dataPin, (val >> (7 - i)) & 1);\n"
    "    digitalWrite(clockPin, HIGH); digitalWrite(clockPin, LOW);\n"
    "  }\n"
    "}\n"
    "int shiftIn(int dataPin, int clockPin, int bitOrder) {\n"
    "  int v = 0;\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    digitalWrite(clockPin, HIGH);\n"
    "    if (bitOrder == LSBFIRST) v = v | (digitalRead(dataPin) << i); else v = v | (digitalRead(dataPin) << (7 - i));\n"
    "    digitalWrite(clockPin, LOW);\n"
    "  }\n"
    "  return v;\n"
    "}\n";

bool Sketch::compile(const std::string& source, const BoardInfo& board, CompileError& err) {
    Impl& m = *impl_;
    m = Impl();
    m.board = board;
    m.analogBits = board.adcBits;
    std::vector<Ins> code;
    Compiler c(board, code, m.types, m.strs, m.warns);
    Ty intTy;
    c.typeIndex(intTy);
    try {
        c.compileUnit(tokenize(kPrelude, -100000));  // prelude lines are negative: never shown
        c.compileUnit(tokenize(source, 0));
        if (!c.funcs.count("setup") || !c.funcs["setup"].defined) throw Err{1, "the sketch needs a setup() function"};
        if (!c.funcs.count("loop") || !c.funcs["loop"].defined) throw Err{1, "the sketch needs a loop() function"};
        // function table
        std::map<std::string, int> fidx;
        for (auto& kv : c.funcs) {
            if (!kv.second.defined) throw Err{std::max(1, kv.second.firstUseLine), "'" + kv.first + "' was not declared"};
            fidx[kv.first] = (int)m.funcEntry.size();
            m.funcEntry.push_back(kv.second.entry);
            m.funcLocals.push_back(kv.second.nlocals);
            std::vector<int> pt;
            for (auto& t : kv.second.params) pt.push_back(c.typeIndex(t));
            m.funcParams.push_back(pt);
        }
        // init code (global initializers) as a function, then the startup sequence
        int initEntry = (int)code.size();
        for (auto ins : c.initCode) {
            if (ins.op == JMP || ins.op == JZ || ins.op == JNZ) ins.a += initEntry;
            code.push_back(ins);
        }
        Ins ret;
        ret.op = PUSHN;
        code.push_back(ret);
        ret.op = RET;
        code.push_back(ret);
        int initIdx = (int)m.funcEntry.size();
        m.funcEntry.push_back(initEntry);
        m.funcLocals.push_back(c.initLocals());
        m.funcParams.emplace_back();
        int start = (int)code.size();
        auto add = [&](Op op, int a = 0, int b = 0) { Ins i; i.op = op; i.a = a; i.b = b; code.push_back(i); };
        add(CALL, initIdx, 0);
        add(POP);
        add(START_LOOP, 1);
        add(CALL, fidx["setup"], 0);
        add(POP);
        int top = (int)code.size();
        add(CALL, fidx["loop"], 0);
        add(POP);
        add(JMP, top);
        m.code = std::move(code);
        m.pc = start;
        m.globals.resize(c.globals.size());
        for (auto& ins : m.code) {  // link calls by name
            if (ins.op == CALL && ins.d < 0) {
                const std::string& callee = m.strs[(size_t)(-ins.d - 1)];
                int nparams = c.funcs[callee].nparams;
                if (ins.b != nparams) throw Err{ins.line, "'" + callee + "' takes " + std::to_string(nparams) + " argument(s), not " + std::to_string(ins.b)};
                ins.a = fidx[callee];
            }
        }
        m.frames.push_back({-1, 0, 0});
        m.locals.resize(4);
        m.ok = true;
        m.stopped = false;
    } catch (const Err& e) {
        err.line = std::max(0, e.line);
        err.message = e.msg;
        m.ok = false;
        return false;
    }
    return true;
}

void Sketch::run(double nowMs, Host& host) {
    Impl& m = *impl_;
    if (!m.ok || m.stopped) return;
    m.now = nowMs;
    m.host = &host;
    // finished tones
    for (auto it = m.toneUntil.begin(); it != m.toneUntil.end();) {
        if (nowMs >= it->second) { m.pins[it->first].level = 0; m.pins[it->first].toneHz = 0; it = m.toneUntil.erase(it); }
        else ++it;
    }
    if (m.wakeAt > nowMs + 1e-9) return;
    try {
        m.step(20000);  // ~an AVR's worth of work per millisecond
    } catch (const Err& e) {
        m.stopped = true;
        m.errorLine = e.line;
        m.error = e.msg;
    }
    m.host = nullptr;
}

bool Sketch::running() const { return impl_->ok && !impl_->stopped; }
const std::string& Sketch::runtimeError() const { return impl_->error; }
int Sketch::runtimeLine() const { return impl_->errorLine; }
std::string Sketch::takeSerial() { std::string s; s.swap(impl_->serialOut); return s; }
void Sketch::serialInput(const std::string& text) { impl_->serialIn += text; }
const std::map<int, PinState>& Sketch::pins() const { return impl_->pins; }
const std::vector<std::string>& Sketch::warnings() const { return impl_->warns; }
int Sketch::currentLine() const { return impl_->curLine; }

}  // namespace ard
