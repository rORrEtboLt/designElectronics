// designElectronics — multi-line code editor widget (UI::codeEditor): monospace
// text with line numbers, C/Arduino syntax colours, selection, undo, clipboard,
// auto-indent, and highlights for the executing line and errors.
#include <cctype>
#include <string>
#include <algorithm>
#include <cmath>
#include <set>

#include "ui.h"

namespace {

size_t nextCp(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    i++;
    while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}
size_t prevCp(const std::string& s, size_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

struct Lines {
    std::vector<size_t> start;  // byte offset of each line
    explicit Lines(const std::string& t) {
        start.push_back(0);
        for (size_t i = 0; i < t.size(); i++) if (t[i] == '\n') start.push_back(i + 1);
    }
    size_t count() const { return start.size(); }
    size_t end(const std::string& t, size_t l) const { return l + 1 < start.size() ? start[l + 1] - 1 : t.size(); }
    size_t lineOf(size_t pos) const { return (size_t)(std::upper_bound(start.begin(), start.end(), pos) - start.begin()) - 1; }
};

int colOf(const std::string& t, size_t lineStart, size_t pos) {
    int c = 0;
    for (size_t i = lineStart; i < pos; i = nextCp(t, i)) c++;
    return c;
}
size_t posAtCol(const std::string& t, size_t lineStart, size_t lineEnd, int col) {
    size_t i = lineStart;
    for (int c = 0; c < col && i < lineEnd; c++) i = nextCp(t, i);
    return std::min(i, lineEnd);
}

bool isWordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

const std::set<std::string>& keywords() {
    static const std::set<std::string> k = {"if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
                                            "return", "const", "static", "volatile", "true", "false", "HIGH", "LOW", "INPUT",
                                            "OUTPUT", "INPUT_PULLUP", "LED_BUILTIN", "struct", "class", "enum", "sizeof", "new",
                                            "delete", "nullptr", "NULL", "MSBFIRST", "LSBFIRST", "auto"};
    return k;
}
const std::set<std::string>& types() {
    static const std::set<std::string> k = {"void", "int", "long", "unsigned", "float", "double", "bool", "boolean", "char",
                                            "byte", "word", "short", "String", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                                            "int8_t", "int16_t", "int32_t", "int64_t", "size_t", "signed"};
    return k;
}
const std::set<std::string>& apiNames() {
    static const std::set<std::string> k = {"setup", "loop", "pinMode", "digitalWrite", "digitalRead", "analogRead", "analogWrite",
                                            "delay", "delayMicroseconds", "millis", "micros", "tone", "noTone", "map", "constrain",
                                            "Serial", "shiftOut", "shiftIn", "random", "randomSeed", "min", "max", "abs",
                                            "ledcWrite", "ledcAttach", "ledcSetup", "ledcAttachPin", "attachInterrupt"};
    return k;
}

}  // namespace

bool UI::codeEditor(const std::string& sid, Rect r, std::string& text, CodeEditorState& st, int execLine, int errorLine,
                    const std::string& errorMsg) {
    size_t id = hashId(sid);
    bool focused = focus_ == id;
    bool changed = false;
    float px = 13.5f * s;
    float lh = std::round(px * 1.45f);
    float cw = g->textWidth("M", px, FONT_MONO);
    if (cw <= 0) cw = px * 0.6f;
    st.cursor = std::min(st.cursor, text.size());
    st.anchor = std::min(st.anchor, text.size());

    Lines L(text);
    int digits = std::max(2, (int)std::to_string(L.count()).size());
    float gutter = (float)digits * cw + 18 * s;
    float errH = errorLine > 0 ? 26 * s : 0;
    Rect body(r.x, r.y, r.w, r.h - errH);
    Rect textArea(body.x + gutter, body.y, body.w - gutter, body.h);
    float padTop = 6 * s, padLeft = 8 * s;

    auto posFromMouse = [&](Vec2 m) {
        float yy = m.y - body.y - padTop + st.scrollY;
        size_t line = (size_t)std::clamp((long)std::floor(yy / lh), 0L, (long)L.count() - 1);
        int col = (int)std::lround((m.x - textArea.x - padLeft + st.scrollX) / cw);
        return posAtCol(text, L.start[line], L.end(text, line), std::max(0, col));
    };
    auto pushUndo = [&](bool force) {
        Uint64 now = SDL_GetTicks();
        if (force || st.undo.empty() || now - st.lastEdit > 700) {
            st.undo.push_back({text, st.cursor});
            if (st.undo.size() > 300) st.undo.erase(st.undo.begin());
        }
        st.redo.clear();
        st.lastEdit = now;
    };
    auto selRange = [&]() { return std::make_pair(std::min(st.cursor, st.anchor), std::max(st.cursor, st.anchor)); };
    auto eraseSel = [&]() {
        auto [a, b] = selRange();
        if (a == b) return false;
        text.erase(a, b - a);
        st.cursor = st.anchor = a;
        return true;
    };
    auto insert = [&](const std::string& t) {
        eraseSel();
        text.insert(st.cursor, t);
        st.cursor += t.size();
        st.anchor = st.cursor;
        changed = true;
    };
    bool keyboardMoved = false;

    // ---- pointer ----
    bool hot = hovered(body);
    if (hot && in->pressed && !scrollDrag_) {
        if (!focused) {
            if (focus_) clearFocus();
            focus_ = id;
            focused = true;
            if (window) {
                SDL_Rect area{(int)r.x, (int)r.y, (int)r.w, (int)r.h};
                SDL_SetTextInputArea(window, &area, 0);
                SDL_StartTextInput(window);
            }
        }
        size_t p = posFromMouse(in->mouse);
        if (in->doubleClick) {  // select word
            size_t a = p, b = p;
            while (a > 0 && isWordChar(text[a - 1])) a--;
            while (b < text.size() && isWordChar(text[b])) b++;
            st.anchor = a;
            st.cursor = b;
        } else {
            st.cursor = p;
            if (!in->shift()) st.anchor = p;
            st.dragging = true;
        }
        st.desiredCol = -1;
    }
    if (st.dragging) {
        if (in->down) {
            st.cursor = posFromMouse(in->mouse);
            if (in->mouse.y < body.y + 4 * s) st.scrollY -= lh * 0.5f;  // drag-scroll
            if (in->mouse.y > body.b() - 4 * s) st.scrollY += lh * 0.5f;
        } else st.dragging = false;
    }
    if (focused && in->pressed && !r.contains(in->mouse)) { clearFocus(); focused = false; }
    if (hot && in->wheelY != 0 && !blocking()) st.scrollY -= in->wheelY * (in->wheelPrecise ? 1.f : 3 * lh);
    if (hot && in->wheelX != 0 && !blocking()) st.scrollX -= in->wheelX * (in->wheelPrecise ? 1.f : 3 * cw);

    // ---- keyboard ----
    if (focused) {
        if (!in->text.empty()) {
            pushUndo(false);
            std::string t = in->text;
            size_t ls = L.start[L.lineOf(st.cursor)];
            bool onlySpace = text.find_first_not_of(" \t", ls) >= st.cursor;
            if (t == "}" && onlySpace && st.cursor - ls >= 2 && text.compare(st.cursor - 2, 2, "  ") == 0) {  // dedent a closing brace
                text.erase(st.cursor - 2, 2);
                st.cursor -= 2;
                st.anchor = st.cursor;
            }
            insert(t);
        }
        for (auto k : in->keys) {
            bool shift = in->shift(), cmd = in->cmd();
            Lines Lk(text);
            size_t line = Lk.lineOf(st.cursor);
            size_t ls = Lk.start[line], le = Lk.end(text, line);
            auto move = [&](size_t p) {
                st.cursor = p;
                if (!shift) st.anchor = p;
                keyboardMoved = true;
            };
            auto vertical = [&](long dl) {
                int col = st.desiredCol >= 0 ? st.desiredCol : colOf(text, ls, st.cursor);
                st.desiredCol = col;
                long nl = std::clamp((long)line + dl, 0L, (long)Lk.count() - 1);
                move(posAtCol(text, Lk.start[(size_t)nl], Lk.end(text, (size_t)nl), col));
            };
            if (k != SDLK_UP && k != SDLK_DOWN && k != SDLK_PAGEUP && k != SDLK_PAGEDOWN) st.desiredCol = -1;
            if (k == SDLK_LEFT) {
                auto [a, b] = selRange();
                if (!shift && a != b) move(a);
                else move(cmd ? ls : prevCp(text, st.cursor));
            } else if (k == SDLK_RIGHT) {
                auto [a, b] = selRange();
                if (!shift && a != b) move(b);
                else move(cmd ? le : nextCp(text, st.cursor));
            } else if (k == SDLK_UP) {
                if (cmd) move(0); else vertical(-1);
            } else if (k == SDLK_DOWN) {
                if (cmd) move(text.size()); else vertical(1);
            } else if (k == SDLK_PAGEUP) vertical(-(long)(body.h / lh));
            else if (k == SDLK_PAGEDOWN) vertical((long)(body.h / lh));
            else if (k == SDLK_HOME) {
                size_t first = std::min(text.find_first_not_of(" \t", ls), le);
                move(st.cursor == first ? ls : first);
            } else if (k == SDLK_END) move(le);
            else if (k == SDLK_A && cmd) { st.anchor = 0; st.cursor = text.size(); }
            else if ((k == SDLK_C || k == SDLK_X) && cmd) {
                auto [a, b] = selRange();
                std::string sel = a == b ? text.substr(ls, std::min(le + 1, text.size()) - ls) : text.substr(a, b - a);  // no selection: whole line
                SDL_SetClipboardText(sel.c_str());
                if (k == SDLK_X) {
                    pushUndo(true);
                    if (a == b) { text.erase(ls, std::min(le + 1, text.size()) - ls); st.cursor = st.anchor = std::min(ls, text.size()); changed = true; }
                    else { eraseSel(); changed = true; }
                }
            } else if (k == SDLK_V && cmd) {
                char* t = SDL_GetClipboardText();
                if (t) {
                    std::string c = t;
                    SDL_free(t);
                    std::string clean;
                    for (size_t i = 0; i < c.size(); i++) {
                        if (c[i] == '\r') continue;
                        if (c[i] == '\t') clean += "  ";
                        else clean += c[i];
                    }
                    pushUndo(true);
                    insert(clean);
                }
            } else if (k == SDLK_Z && cmd) {
                auto& from = shift ? st.redo : st.undo;
                auto& to = shift ? st.undo : st.redo;
                if (!from.empty()) {
                    to.push_back({text, st.cursor});
                    text = from.back().first;
                    st.cursor = st.anchor = std::min(from.back().second, text.size());
                    from.pop_back();
                    changed = true;
                    st.lastEdit = 0;
                }
            } else if (k == SDLK_Y && cmd) {
                if (!st.redo.empty()) {
                    st.undo.push_back({text, st.cursor});
                    text = st.redo.back().first;
                    st.cursor = st.anchor = std::min(st.redo.back().second, text.size());
                    st.redo.pop_back();
                    changed = true;
                }
            } else if (k == SDLK_SLASH && cmd) {  // toggle // comments on the selected lines
                pushUndo(true);
                auto [a, b] = selRange();
                size_t l0 = Lk.lineOf(a), l1 = Lk.lineOf(b > a && b == Lk.start[Lk.lineOf(b)] ? b - 1 : b);
                bool allCommented = true;
                for (size_t l = l0; l <= l1; l++) {
                    size_t f = text.find_first_not_of(" ", Lk.start[l]);
                    if (f < Lk.end(text, l) && text.compare(f, 2, "//") != 0) allCommented = false;
                }
                long shiftBytes = 0;
                for (size_t l = l1 + 1; l-- > l0;) {
                    size_t f = text.find_first_not_of(" ", Lk.start[l]);
                    if (f >= Lk.end(text, l)) continue;
                    if (allCommented) {
                        size_t n = text.compare(f, 3, "// ") == 0 ? 3 : 2;
                        text.erase(f, n);
                        shiftBytes -= (long)n;
                    } else {
                        text.insert(f, "// ");
                        shiftBytes += 3;
                    }
                }
                st.cursor = (size_t)std::clamp((long)st.cursor + shiftBytes, 0L, (long)text.size());
                st.anchor = st.cursor;
                changed = true;
            } else if (k == SDLK_BACKSPACE) {
                pushUndo(false);
                if (!eraseSel() && st.cursor > 0) {
                    size_t from = prevCp(text, st.cursor);
                    // in leading spaces, delete back to the previous indent stop
                    if (st.cursor - ls >= 2 && text.find_first_not_of(' ', ls) >= st.cursor && (st.cursor - ls) % 2 == 0) from = st.cursor - 2;
                    text.erase(from, st.cursor - from);
                    st.cursor = st.anchor = from;
                }
                changed = true;
                keyboardMoved = true;
            } else if (k == SDLK_DELETE) {
                pushUndo(false);
                if (!eraseSel() && st.cursor < text.size()) text.erase(st.cursor, nextCp(text, st.cursor) - st.cursor);
                changed = true;
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                pushUndo(true);
                size_t ind = ls;
                while (ind < le && text[ind] == ' ') ind++;
                std::string indent(ind - ls, ' ');
                char before = st.cursor > ls ? text[st.cursor - 1] : 0;
                char after = st.cursor < text.size() ? text[st.cursor] : 0;
                if (before == '{') {
                    insert("\n" + indent + "  ");
                    if (after == '}') {  // { | }  →  block
                        size_t keep = st.cursor;
                        insert("\n" + indent);
                        st.cursor = st.anchor = keep;
                    }
                } else insert("\n" + indent);
                keyboardMoved = true;
            } else if (k == SDLK_TAB) {
                pushUndo(true);
                auto [a, b] = selRange();
                if (a == b && !shift) insert("  ");
                else {  // indent / dedent lines
                    size_t l0 = Lk.lineOf(a), l1 = Lk.lineOf(b);
                    for (size_t l = l1 + 1; l-- > l0;) {
                        size_t st0 = Lk.start[l];
                        if (shift) { int n = 0; while (n < 2 && st0 + (size_t)n < text.size() && text[st0 + (size_t)n] == ' ') n++; text.erase(st0, (size_t)n); }
                        else text.insert(st0, "  ");
                    }
                    Lines L2(text);
                    st.anchor = L2.start[std::min(l0, L2.count() - 1)];
                    st.cursor = L2.end(text, std::min(l1, L2.count() - 1));
                    changed = true;
                }
            } else if (k == SDLK_ESCAPE) { clearFocus(); focused = false; break; }
        }
    }

    // ---- layout after edits ----
    Lines L2(text);
    float contentH = (float)L2.count() * lh + padTop * 2;
    size_t maxCols = 0;
    for (size_t l = 0; l < L2.count(); l++) maxCols = std::max(maxCols, L2.end(text, l) - L2.start[l]);
    float contentW = (float)maxCols * cw + padLeft * 2 + 40 * s;
    if (keyboardMoved || changed) {  // keep the cursor in view
        size_t cl = L2.lineOf(st.cursor);
        float cy = (float)cl * lh + padTop;
        if (cy < st.scrollY) st.scrollY = cy - padTop;
        if (cy + lh > st.scrollY + body.h) st.scrollY = cy + lh - body.h + padTop;
        float cx = (float)colOf(text, L2.start[cl], st.cursor) * cw + padLeft;
        if (cx < st.scrollX) st.scrollX = std::max(0.f, cx - 40 * s);
        if (cx > st.scrollX + textArea.w - 20 * s) st.scrollX = cx - textArea.w + 60 * s;
    }
    if (execLine > 0 && !focused && st.followExec) {  // follow the running line
        float cy = (float)(execLine - 1) * lh + padTop;
        if (cy < st.scrollY || cy + lh > st.scrollY + body.h) st.scrollY = cy - body.h * 0.4f;
    }
    st.scrollY = std::clamp(st.scrollY, 0.f, std::max(0.f, contentH - body.h));
    st.scrollX = std::clamp(st.scrollX, 0.f, std::max(0.f, contentW - textArea.w));

    // ---- draw ----
    g->fillRect(r, rgba(255, 255, 255));
    g->fillRect(Rect(body.x, body.y, gutter, body.h), rgba(244, 245, 248));
    bool hadClip = clipping_;
    Rect prevClip = clipStack_;
    g->setClip(&body);
    size_t first = (size_t)std::max(0.f, std::floor((st.scrollY - padTop) / lh));
    size_t last = std::min(L2.count(), first + (size_t)(body.h / lh) + 2);
    auto [sa, sb] = selRange();
    // block-comment state at the first visible line
    bool inBlock = false;
    {
        size_t limit = L2.start[std::min(first, L2.count() - 1)];
        for (size_t i = 0; i + 1 < limit; i++) {
            if (!inBlock && text[i] == '/' && text[i + 1] == '/') { while (i < limit && text[i] != '\n') i++; continue; }
            if (!inBlock && text[i] == '"') { i++; while (i < limit && text[i] != '"' && text[i] != '\n') { if (text[i] == '\\') i++; i++; } continue; }
            if (!inBlock && text[i] == '/' && text[i + 1] == '*') { inBlock = true; i++; continue; }
            if (inBlock && text[i] == '*' && text[i + 1] == '/') { inBlock = false; i++; }
        }
    }
    const uint32_t cText = rgba(36, 41, 47), cKw = rgba(124, 58, 237), cType = rgba(15, 111, 198), cApi = rgba(0, 131, 143),
                   cNum = rgba(194, 65, 12), cStr = rgba(21, 128, 61), cCom = rgba(140, 149, 159), cPre = rgba(180, 35, 122);
    for (size_t l = first; l < last; l++) {
        float y = body.y + padTop + (float)l * lh - st.scrollY;
        size_t ls = L2.start[l], le = L2.end(text, l);
        int lineNo = (int)l + 1;
        if (lineNo == errorLine) g->fillRect(Rect(body.x, y, body.w, lh), rgba(253, 226, 226));
        else if (lineNo == execLine) g->fillRect(Rect(body.x, y, body.w, lh), rgba(255, 244, 194));
        else if (focused && L2.lineOf(st.cursor) == l && sa == sb) g->fillRect(Rect(textArea.x, y, textArea.w, lh), rgba(246, 248, 252));
        // selection
        if (sa != sb && sb >= ls && sa <= le) {
            size_t a = std::max(sa, ls), b = std::min(sb, le);
            float x0 = textArea.x + padLeft - st.scrollX + (float)colOf(text, ls, a) * cw;
            float x1 = textArea.x + padLeft - st.scrollX + (float)colOf(text, ls, b) * cw + (sb > le ? cw * 0.5f : 0);
            g->fillRect(Rect(std::max(x0, textArea.x), y, std::max(0.f, x1 - std::max(x0, textArea.x)), lh), rgba(207, 225, 255));
        }
        // tokens
        std::string line = text.substr(ls, le - ls);
        float x = textArea.x + padLeft - st.scrollX;
        size_t i = 0;
        size_t firstNonSpace = line.find_first_not_of(' ');
        bool pre = firstNonSpace != std::string::npos && line[firstNonSpace] == '#';
        auto drawTok = [&](size_t a, size_t b, uint32_t col) {
            if (b <= a) return;
            std::string tok = line.substr(a, b - a);
            float tx = x + (float)colOf(line, 0, a) * cw;
            if (tx < textArea.r() && tx + (float)tok.size() * cw > textArea.x)
                g->text({tx, y + lh / 2}, tok, px, col, 0, FONT_MONO);
        };
        while (i < line.size()) {
            if (inBlock) {
                size_t e = line.find("*/", i);
                size_t stop = e == std::string::npos ? line.size() : e + 2;
                drawTok(i, stop, cCom);
                if (e != std::string::npos) inBlock = false;
                i = stop;
                continue;
            }
            char c = line[i];
            if (c == ' ') { i++; continue; }
            if (pre) { drawTok(i, line.size(), cPre); break; }
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') { drawTok(i, line.size(), cCom); break; }
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') { inBlock = true; continue; }
            if (c == '"' || c == '\'') {
                size_t j = i + 1;
                while (j < line.size() && line[j] != c) { if (line[j] == '\\') j++; j++; }
                j = std::min(j + 1, line.size());
                drawTok(i, j, cStr);
                i = j;
                continue;
            }
            if (std::isdigit((unsigned char)c)) {
                size_t j = i;
                while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '.')) j++;
                drawTok(i, j, cNum);
                i = j;
                continue;
            }
            if (isWordChar(c)) {
                size_t j = i;
                while (j < line.size() && isWordChar(line[j])) j++;
                std::string w = line.substr(i, j - i);
                uint32_t col = keywords().count(w) ? cKw : types().count(w) ? cType : apiNames().count(w) ? cApi : cText;
                drawTok(i, j, col);
                i = j;
                continue;
            }
            size_t j = nextCp(line, i);
            drawTok(i, j, cText);
            i = j;
        }
    }
    // caret
    if (focused && sa == sb && (SDL_GetTicks() / 530) % 2 == 0) {
        size_t cl = L2.lineOf(st.cursor);
        float cx = textArea.x + padLeft - st.scrollX + (float)colOf(text, L2.start[cl], st.cursor) * cw;
        float cy = body.y + padTop + (float)cl * lh - st.scrollY;
        if (cx >= textArea.x) g->fillRect(Rect(cx, cy + 2 * s, std::max(1.5f, 1.5f * s), lh - 4 * s), rgba(31, 111, 229));
    }
    // gutter numbers (drawn last so text scrolled under them stays hidden)
    g->fillRect(Rect(body.x, body.y, gutter, body.h), rgba(244, 245, 248));
    for (size_t l = first; l < last; l++) {
        float y = body.y + padTop + (float)l * lh - st.scrollY;
        int lineNo = (int)l + 1;
        uint32_t col = lineNo == errorLine ? rgba(200, 40, 40) : lineNo == execLine ? rgba(150, 110, 0) : rgba(150, 156, 166);
        if (lineNo == errorLine) g->fillCircle({body.x + 7 * s, y + lh / 2}, 3.5f * s, rgba(220, 50, 50));
        if (lineNo == execLine) g->fillCircle({body.x + 7 * s, y + lh / 2}, 3.5f * s, rgba(230, 170, 0));
        g->text({body.x + gutter - 8 * s, y + lh / 2}, std::to_string(lineNo), px * 0.9f, col, 2, FONT_MONO);
    }
    g->setClip(hadClip ? &prevClip : nullptr);
    g->fillRect(Rect(body.x + gutter - 1, body.y, std::max(1.f, s), body.h), rgba(226, 229, 234));
    // error strip
    if (errorLine > 0) {
        Rect er(r.x, body.b(), r.w, errH);
        g->fillRect(er, rgba(200, 40, 40));
        std::string msg = "Line " + std::to_string(errorLine) + ": " + errorMsg;
        while (msg.size() > 4 && g->textWidth(msg, 12.5f * s) > er.w - 20 * s) { msg.resize(msg.size() - 4); msg += "…"; }
        g->text({er.x + 10 * s, er.center().y}, msg, 12.5f * s, rgba(255, 255, 255), 0);
    }
    g->strokeRect(r, std::max(1.f, s), focused ? th.accent : th.border);
    return changed;
}
