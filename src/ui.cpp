#include "ui.h"

#include <cmath>

std::vector<std::string> wrapText(Gfx& g, const std::string& t, float px, float width) {
    std::vector<std::string> lines;
    std::string para;
    auto flushPara = [&](const std::string& p) {
        std::string line, word;
        size_t i = 0;
        auto pushWord = [&]() {
            if (word.empty()) return;
            std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && g.textWidth(cand, px) > width) { lines.push_back(line); line = word; }
            else line = cand;
            word.clear();
        };
        for (; i < p.size(); i++) {
            if (p[i] == ' ') pushWord();
            else word += p[i];
        }
        pushWord();
        lines.push_back(line);
    };
    for (char c : t) {
        if (c == '\n') { flushPara(para); para.clear(); }
        else para += c;
    }
    flushPara(para);
    return lines;
}

void UI::begin() {
    tip_.clear();
    if (focus_ && in->pressed && !clipStack_.contains(in->mouse)) {
        // handled per-field: pressing outside the focused field commits it
    }
    // Close menus when pressing outside of them.
    if (!menus_.empty() && in->pressed) {
        bool inside = false;
        for (auto& m : menus_) if (m.rect.contains(in->mouse)) inside = true;
        if (!inside && !menuJustOpened_) menus_.clear();
    }
    menuJustOpened_ = false;
    if (!in->down && !in->released) { pressedId_ = 0; scrollDrag_ = false; scrollDragId_ = 0; }
}

bool UI::overBlocking(Vec2 p) const {
    if (!dialogs_.empty() || !sheets_.empty()) return true;
    for (auto& m : menus_) if (m.rect.contains(p)) return true;
    return false;
}

bool UI::interactive(Rect r) const {
    if (blocking()) return false;
    if (clipping_ && !clipStack_.contains(in->mouse)) return false;
    return true;
}

void UI::label(Rect r, const std::string& t, uint32_t col, float px, int align, FontFace face) {
    float x = align == 0 ? r.x : align == 1 ? r.center().x : r.r();
    g->text({x, r.center().y}, t, px, col, align, face);
}

float UI::wrappedText(Rect r, const std::string& t, uint32_t col, float px, bool draw) {
    auto lines = wrapText(*g, t, px, r.w);
    float lh = px * 1.35f;
    if (draw)
        for (size_t i = 0; i < lines.size(); i++) g->text({r.x, r.y + lh * (float)i + lh / 2}, lines[i], px, col, 0);
    return lh * (float)lines.size();
}

void UI::tooltip(Rect r, const std::string& tip) {
    if (tip.empty() || in->touch || in->down) return;
    if (!hovered(r)) return;
    if (tipId_ != tip) { tipId_ = tip; tipStart_ = SDL_GetTicks(); }
    tip_ = tip;
    tipRect_ = r;
}

bool UI::button(const std::string& sid, Rect r, const std::string& lbl, ButtonStyle st, bool selected, bool enabled) {
    size_t id = hashId(sid);
    bool hot = enabled && hovered(r);
    if (hot && in->pressed) pressedId_ = id;
    bool held = pressedId_ == id && in->down && r.contains(in->mouse) && !scrollDrag_;
    bool clicked = enabled && pressedId_ == id && in->released && r.contains(in->mouse) && interactive(r) && !scrollDrag_;
    if (clicked) pressedId_ = 0;

    uint32_t bg = 0, fg = enabled ? th.text : th.textDim, border = 0;
    float rad = 6 * s;
    switch (st) {
        case BTN_NORMAL:
            bg = held ? th.buttonDown : hot ? th.buttonHover : th.button;
            border = th.border;
            if (selected) { bg = th.buttonDown; border = th.accent; }
            break;
        case BTN_PRIMARY:
            bg = held ? mixColor(th.accent, rgba(0, 0, 0), 0.2f) : hot ? mixColor(th.accent, rgba(255, 255, 255), 0.1f) : th.accent;
            fg = th.accentText;
            break;
        case BTN_FLAT:
        case BTN_TOOL:
            if (held) bg = th.buttonDown; else if (hot) bg = th.buttonHover;
            if (selected) { bg = th.buttonDown; fg = th.accent; }
            break;
        case BTN_TAB:
            rad = 7 * s;
            bg = selected ? th.panel : hot ? rgba(240, 240, 242) : 0;
            fg = selected ? th.text : th.textDim;
            break;
        case BTN_CHIP:
            rad = r.h / 2;
            bg = selected ? th.accent : held ? th.buttonDown : hot ? th.buttonHover : th.button;
            fg = selected ? th.accentText : th.text;
            border = selected ? 0 : th.border;
            break;
    }
    if (bg) g->fillRoundRect(r, rad, bg);
    if (border) g->strokeRoundRect(r, rad, std::max(1.f, s), border);
    if (st == BTN_TAB && selected) g->fillRect({r.x + 10 * s, r.b() - 3 * s, r.w - 20 * s, 3 * s}, th.accent);
    float px = (st == BTN_CHIP ? 12.5f : 14.f) * s;
    g->text(r.center(), lbl, px, fg, 1);
    return clicked;
}

bool UI::iconButton(const std::string& sid, Rect r, const std::string& icon, const std::string& lbl, bool selected, bool enabled, const std::string& tip) {
    bool c = button(sid, r, "", BTN_TOOL, selected, enabled);
    uint32_t col = !enabled ? withAlpha(th.textDim, 120) : selected ? th.accent : th.text;
    if (lbl.empty()) {
        float sz = std::min(r.w, r.h) * 0.62f;
        drawIcon(icon, Rect(r.center().x - sz / 2, r.center().y - sz / 2, sz, sz), col);
    } else {
        float sz = std::min(r.w * 0.5f, r.h * 0.48f);
        drawIcon(icon, Rect(r.center().x - sz / 2, r.y + r.h * 0.12f, sz, sz), col);
        g->text({r.center().x, r.b() - r.h * 0.2f}, lbl, 11.5f * s, col, 1);
    }
    tooltip(r, tip.empty() ? lbl : tip);
    return c;
}

void UI::clearFocus() {
    if (focus_) {
        focus_ = 0;
        if (window) SDL_StopTextInput(window);
    }
}

static size_t utf8Prev(const std::string& s, size_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}
static size_t utf8Next(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    i++;
    while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

bool UI::textField(const std::string& sid, Rect r, std::string& value, const std::string& placeholder, bool live) {
    size_t id = hashId(sid);
    bool focused = focus_ == id;
    bool hot = hovered(r);
    bool committed = false;
    float px = 14 * s;

    if (!focused && hot && in->released && !scrollDrag_ && in->pressPos.x >= r.x && r.contains(in->pressPos)) {
        if (focus_) clearFocus();
        focus_ = id;
        focused = true;
        editBuf_ = value;
        caret_ = editBuf_.size();
        if (window) {
            SDL_Rect area{(int)r.x, (int)r.y, (int)r.w, (int)r.h};
            SDL_SetTextInputArea(window, &area, 0);
            SDL_StartTextInput(window);
        }
    }
    if (focused) {
        if (in->pressed && !r.contains(in->mouse)) {
            value = editBuf_;
            committed = true;
            clearFocus();
            focused = false;
        } else {
            if (!in->text.empty()) { editBuf_.insert(caret_, in->text); caret_ += in->text.size(); }
            for (auto k : in->keys) {
                if (k == SDLK_BACKSPACE && caret_ > 0) { size_t p = utf8Prev(editBuf_, caret_); editBuf_.erase(p, caret_ - p); caret_ = p; }
                else if (k == SDLK_DELETE && caret_ < editBuf_.size()) { size_t n = utf8Next(editBuf_, caret_); editBuf_.erase(caret_, n - caret_); }
                else if (k == SDLK_LEFT) caret_ = utf8Prev(editBuf_, caret_);
                else if (k == SDLK_RIGHT) caret_ = utf8Next(editBuf_, caret_);
                else if (k == SDLK_HOME) caret_ = 0;
                else if (k == SDLK_END) caret_ = editBuf_.size();
                else if (k == SDLK_V && in->cmd()) {
                    char* t = SDL_GetClipboardText();
                    if (t) { std::string c = t; SDL_free(t); for (auto& ch : c) if (ch == '\n' || ch == '\r') ch = ' '; editBuf_.insert(caret_, c); caret_ += c.size(); }
                } else if (k == SDLK_A && in->cmd()) { caret_ = editBuf_.size(); }
                else if (k == SDLK_C && in->cmd()) SDL_SetClipboardText(editBuf_.c_str());
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { value = editBuf_; committed = true; clearFocus(); focused = false; break; }
                else if (k == SDLK_ESCAPE) { clearFocus(); focused = false; break; }
            }
            if (focused && live && value != editBuf_) { value = editBuf_; committed = true; }
        }
    }
    g->fillRoundRect(r, 5 * s, th.field);
    g->strokeRoundRect(r, 5 * s, focused ? 2 * s : std::max(1.f, s), focused ? th.accent : (hot ? mixColor(th.border, th.accent, 0.4f) : th.border));
    Rect inner = r.inset(8 * s);
    const std::string& shown = focused ? editBuf_ : value;
    Rect clip = Rect(r.x + 2 * s, r.y, r.w - 4 * s, r.h);
    bool hadClip = clipping_;
    Rect prevClip = clipStack_;
    Rect c2 = hadClip ? Rect(std::max(clip.x, prevClip.x), std::max(clip.y, prevClip.y),
                             std::min(clip.r(), prevClip.r()) - std::max(clip.x, prevClip.x),
                             std::min(clip.b(), prevClip.b()) - std::max(clip.y, prevClip.y)) : clip;
    g->setClip(&c2);
    float scrollX = 0;
    if (focused) {
        float cw = g->textWidth(editBuf_.substr(0, caret_), px);
        if (cw > inner.w) scrollX = cw - inner.w;
    }
    if (shown.empty() && !focused) g->text({inner.x, r.center().y}, placeholder, px, th.textDim, 0);
    else g->text({inner.x - scrollX, r.center().y}, shown, px, th.text, 0);
    if (focused && (SDL_GetTicks() / 530) % 2 == 0) {
        float cx = inner.x - scrollX + g->textWidth(editBuf_.substr(0, caret_), px);
        g->fillRect({cx, r.y + 6 * s, std::max(1.5f, 1.5f * s), r.h - 12 * s}, th.accent);
    }
    if (hadClip) g->setClip(&prevClip); else g->setClip(nullptr);
    return committed;
}

bool UI::textArea(const std::string& sid, Rect r, std::string& value, const std::string& placeholder, float padRight) {
    size_t id = hashId(sid);
    bool focused = focus_ == id;
    bool hot = hovered(r);
    bool submit = false;
    float px = 14 * s, lineH = 19 * s;
    Rect inner(r.x + 10 * s, r.y + 9 * s, r.w - 20 * s - padRight, r.h - 18 * s);
    if (!focused && hot && in->released && !scrollDrag_ && r.contains(in->pressPos)) {
        if (focus_) clearFocus();
        focus_ = id;
        focused = true;
        editBuf_ = value;
        caret_ = editBuf_.size();
        if (window) {
            SDL_Rect area{(int)r.x, (int)r.y, (int)r.w, (int)r.h};
            SDL_SetTextInputArea(window, &area, 0);
            SDL_StartTextInput(window);
        }
    }
    if (focused && value != editBuf_ && in->text.empty() && in->keys.empty()) editBuf_ = value, caret_ = std::min(caret_, editBuf_.size());
    // word wrap: [start, end) byte ranges per visual line
    auto wrap = [&](const std::string& t) {
        std::vector<std::pair<size_t, size_t>> lines;
        size_t start = 0;
        while (true) {
            size_t nl = t.find('\n', start);
            size_t para = nl == std::string::npos ? t.size() : nl;
            size_t ls = start;
            while (true) {
                size_t lastSpace = std::string::npos, i = ls, fit = para;
                while (i < para) {
                    size_t n = utf8Next(t, i);
                    if (g->textWidth(t.substr(ls, n - ls), px) > inner.w) { fit = i; break; }
                    if (t[i] == ' ') lastSpace = i;
                    i = n;
                }
                if (fit >= para) { lines.push_back({ls, para}); break; }
                size_t brk = (lastSpace != std::string::npos && lastSpace > ls) ? lastSpace + 1 : std::max(fit, utf8Next(t, ls));
                lines.push_back({ls, brk});
                ls = brk;
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return lines;
    };
    if (focused) {
        if (in->pressed && !r.contains(in->mouse)) {
            value = editBuf_;
            clearFocus();
            focused = false;
        } else {
            if (!in->text.empty()) { editBuf_.insert(caret_, in->text); caret_ += in->text.size(); }
            for (auto k : in->keys) {
                if (k == SDLK_BACKSPACE && caret_ > 0) { size_t p = utf8Prev(editBuf_, caret_); editBuf_.erase(p, caret_ - p); caret_ = p; }
                else if (k == SDLK_DELETE && caret_ < editBuf_.size()) { size_t n = utf8Next(editBuf_, caret_); editBuf_.erase(caret_, n - caret_); }
                else if (k == SDLK_LEFT) caret_ = utf8Prev(editBuf_, caret_);
                else if (k == SDLK_RIGHT) caret_ = utf8Next(editBuf_, caret_);
                else if (k == SDLK_UP || k == SDLK_DOWN) {
                    auto ls = wrap(editBuf_);
                    size_t li = 0;
                    for (size_t i = 0; i < ls.size(); i++) if (caret_ >= ls[i].first && caret_ <= ls[i].second) li = i;
                    float cx = g->textWidth(editBuf_.substr(ls[li].first, caret_ - ls[li].first), px);
                    size_t to = k == SDLK_UP ? (li ? li - 1 : 0) : std::min(ls.size() - 1, li + 1);
                    size_t p = ls[to].first;
                    while (p < ls[to].second && g->textWidth(editBuf_.substr(ls[to].first, utf8Next(editBuf_, p) - ls[to].first), px) <= cx + 2) p = utf8Next(editBuf_, p);
                    caret_ = p;
                }
                else if (k == SDLK_HOME) caret_ = 0;
                else if (k == SDLK_END) caret_ = editBuf_.size();
                else if (k == SDLK_V && in->cmd()) {
                    char* t = SDL_GetClipboardText();
                    if (t) { std::string c = t; SDL_free(t); c.erase(std::remove(c.begin(), c.end(), '\r'), c.end()); editBuf_.insert(caret_, c); caret_ += c.size(); }
                } else if (k == SDLK_A && in->cmd()) caret_ = editBuf_.size();
                else if (k == SDLK_C && in->cmd()) SDL_SetClipboardText(editBuf_.c_str());
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    if (in->shift()) { editBuf_.insert(caret_, "\n"); caret_++; }
                    else submit = true;
                } else if (k == SDLK_ESCAPE) { clearFocus(); focused = false; break; }
            }
            value = editBuf_;
        }
    }
    g->fillRoundRect(r, 8 * s, th.field);
    g->strokeRoundRect(r, 8 * s, focused ? 2 * s : std::max(1.f, s), focused ? th.accent : (hot ? mixColor(th.border, th.accent, 0.4f) : th.border));
    const std::string& shown = focused ? editBuf_ : value;
    auto lines = wrap(shown);
    size_t caretLine = 0;
    for (size_t i = 0; i < lines.size(); i++) if (caret_ >= lines[i].first && caret_ <= lines[i].second) caretLine = i;
    int visible = std::max(1, (int)(inner.h / lineH));
    int first = focused ? std::max(0, (int)caretLine - visible + 1) : 0;
    bool hadClip = clipping_;
    Rect prevClip = clipStack_;
    Rect clip(r.x + 2 * s, r.y + 2 * s, r.w - 4 * s, r.h - 4 * s);
    Rect c2 = hadClip ? Rect(std::max(clip.x, prevClip.x), std::max(clip.y, prevClip.y),
                             std::min(clip.r(), prevClip.r()) - std::max(clip.x, prevClip.x),
                             std::min(clip.b(), prevClip.b()) - std::max(clip.y, prevClip.y)) : clip;
    g->setClip(&c2);
    if (shown.empty() && !focused) {
        auto pl = wrap(placeholder);
        for (size_t i = 0; i < pl.size() && (int)i < visible; i++)
            g->text({inner.x, inner.y + lineH * (float)i + lineH / 2}, placeholder.substr(pl[i].first, pl[i].second - pl[i].first), px, th.textDim, 0);
    } else {
        for (int i = first; i < (int)lines.size() && i < first + visible; i++)
            g->text({inner.x, inner.y + lineH * (float)(i - first) + lineH / 2}, shown.substr(lines[(size_t)i].first, lines[(size_t)i].second - lines[(size_t)i].first), px, th.text, 0);
    }
    if (focused && (SDL_GetTicks() / 530) % 2 == 0 && !lines.empty()) {
        auto& ln = lines[caretLine];
        float cx = inner.x + g->textWidth(shown.substr(ln.first, std::min(caret_, ln.second) - ln.first), px);
        float cy = inner.y + lineH * (float)((int)caretLine - first);
        g->fillRect({cx, cy + 2 * s, std::max(1.5f, 1.5f * s), lineH - 4 * s}, th.accent);
    }
    if (hadClip) g->setClip(&prevClip); else g->setClip(nullptr);
    return submit;
}

bool UI::comboField(const std::string& sid, Rect r, std::string& value, const std::vector<std::string>& options, bool editable) {
    float bw = options.empty() ? 0 : 30 * s;
    bool changed = false;
    if (editable) {
        changed = textField(sid, Rect(r.x, r.y, r.w - bw - (bw > 0 ? 4 * s : 0), r.h), value);
        if (bw > 0) {
            Rect br(r.r() - bw, r.y, bw, r.h);
            if (button(sid + "#dd", br, "")) {
                std::vector<MenuItem> items;
                for (auto& o : options) {
                    MenuItem m;
                    m.label = o;
                    m.checked = o == value;
                    std::string* target = &pendingCombo_;
                    std::string opt = o;
                    size_t hid = hashId(sid);
                    m.action = [this, opt, hid, target]() { *target = opt; pendingComboId_ = hid; };
                    items.push_back(m);
                }
                openMenu(Rect(r.x, r.y, r.w, r.h), items);
            }
            drawIcon("chevron", Rect(br.center().x - 7 * s, br.center().y - 7 * s, 14 * s, 14 * s), th.textDim);
        }
    } else {
        if (button(sid, r, "", BTN_NORMAL)) {
            std::vector<MenuItem> items;
            for (auto& o : options) {
                MenuItem m;
                m.label = o;
                m.checked = o == value;
                std::string opt = o;
                size_t hid = hashId(sid);
                m.action = [this, opt, hid]() { pendingCombo_ = opt; pendingComboId_ = hid; };
                items.push_back(m);
            }
            openMenu(r, items);
        }
        g->text({r.x + 10 * s, r.center().y}, value, 14 * s, th.text, 0);
        drawIcon("chevron", Rect(r.r() - 22 * s, r.center().y - 7 * s, 14 * s, 14 * s), th.textDim);
    }
    if (pendingComboId_ == hashId(sid)) {
        value = pendingCombo_;
        pendingComboId_ = 0;
        changed = true;
    }
    return changed;
}

bool UI::checkbox(const std::string& sid, Rect r, const std::string& lbl, bool& v) {
    bool c = button(sid, r, "", BTN_FLAT);
    if (c) v = !v;
    Rect box(r.x + 4 * s, r.center().y - 9 * s, 18 * s, 18 * s);
    g->fillRoundRect(box, 4 * s, v ? th.accent : th.field);
    g->strokeRoundRect(box, 4 * s, std::max(1.f, s), v ? th.accent : th.border);
    if (v) g->polyline({{box.x + 4 * s, box.center().y}, {box.x + 8 * s, box.b() - 5 * s}, {box.r() - 4 * s, box.y + 5 * s}}, 2.2f * s, th.accentText);
    g->text({box.r() + 8 * s, r.center().y}, lbl, 14 * s, th.text, 0);
    return c;
}

float UI::beginScroll(const std::string& sid, Rect r, float contentH) {
    size_t id = hashId(sid);
    float& y = scrollY_[id];
    float maxY = std::max(0.f, contentH - r.h);
    if (!blocking() && r.contains(in->mouse)) {
        y -= in->wheelY * 40 * s;
        if (in->pressed) { scrollDragId_ = id; scrollDragStart_ = in->mouse.y + y; }
    }
    if (scrollDragId_ == id && in->down) {
        float ny = scrollDragStart_ - in->mouse.y;
        if (!scrollDrag_ && std::fabs(in->mouse.y - in->pressPos.y) > 8 * s && in->touch) { scrollDrag_ = true; pressedId_ = 0; }
        if (scrollDrag_) y = ny;
    }
    y = std::clamp(y, 0.f, maxY);
    scrollStack_.push_back({id, r, &y, contentH});
    clipStack_ = r;
    clipping_ = true;
    g->setClip(&r);
    return -y;
}

void UI::endScroll() {
    if (scrollStack_.empty()) return;
    Scroll sc = scrollStack_.back();
    scrollStack_.pop_back();
    g->setClip(nullptr);
    clipping_ = false;
    if (sc.contentH > sc.r.h) {
        float h = sc.r.h * sc.r.h / sc.contentH;
        float t = *sc.y / (sc.contentH - sc.r.h);
        g->fillRoundRect({sc.r.r() - 5 * s, sc.r.y + t * (sc.r.h - h), 3 * s, h}, 1.5f * s, rgba(0, 0, 0, 60));
    }
}

void UI::openMenu(Rect anchor, std::vector<MenuItem> items, bool above) {
    menus_.clear();
    Menu m;
    m.items = std::move(items);
    m.rect = menuRect(anchor, m.items, above, false);
    menus_.push_back(m);
    menuJustOpened_ = true;
}

Rect UI::menuRect(Rect anchor, const std::vector<MenuItem>& items, bool above, bool side) const {
    float w = 200 * s;
    for (auto& it : items) w = std::max(w, g->textWidth(it.label, 14 * s) + g->textWidth(it.shortcut, 12.5f * s) + 80 * s);
    float h = 8 * s;
    for (auto& it : items) h += it.separator ? 9 * s : 34 * s;
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    float x = side ? anchor.r() - 4 * s : anchor.x;
    float y = side ? anchor.y - 4 * s : (above ? anchor.y - h - 4 * s : anchor.b() + 4 * s);
    if (x + w > ww - 4 * s) x = side ? anchor.x - w + 4 * s : (float)ww - w - 4 * s;
    if (y + h > wh - 4 * s) y = std::max(4 * s, (float)wh - h - 4 * s);
    if (y < 4 * s) y = 4 * s;
    h = std::min(h, (float)wh - 8 * s);
    return {std::max(4 * s, x), y, w, h};
}

void UI::dialog(const std::string& title, const std::vector<std::string>& lines, std::vector<DialogButton> buttons) {
    if (buttons.empty()) buttons.push_back({"OK", nullptr, true});
    dialogs_.push_back({title, lines, std::move(buttons)});
    menus_.clear();
    clearFocus();
}

void UI::toast(const std::string& msg, float seconds) {
    toastMsg_ = msg;
    toastUntil_ = SDL_GetTicks() + (Uint64)(seconds * 1000);
}

void UI::drawMenus() {
    std::function<void()> fire;
    for (size_t level = 0; level < menus_.size(); level++) {
        Menu& m = menus_[level];
        g->fillRoundRect(Rect(m.rect.x + 2 * s, m.rect.y + 4 * s, m.rect.w, m.rect.h), 8 * s, rgba(0, 0, 0, 40));
        g->fillRoundRect(m.rect, 8 * s, th.panel);
        g->strokeRoundRect(m.rect, 8 * s, std::max(1.f, s), th.border);
        float y = m.rect.y + 4 * s;
        for (size_t i = 0; i < m.items.size(); i++) {
            MenuItem& it = m.items[i];
            if (it.separator) {
                g->fillRect({m.rect.x + 10 * s, y + 4 * s, m.rect.w - 20 * s, std::max(1.f, s)}, th.border);
                y += 9 * s;
                continue;
            }
            Rect ir(m.rect.x + 4 * s, y, m.rect.w - 8 * s, 34 * s);
            bool hot = ir.contains(in->mouse) && it.enabled;
            bool deeper = level + 1 < menus_.size() && m.open == (int)i;
            if (hot || deeper) g->fillRoundRect(ir, 5 * s, hot ? th.accent : th.buttonDown);
            uint32_t fg = !it.enabled ? th.textDim : hot ? th.accentText : th.text;
            if (it.checked) g->polyline({{ir.x + 9 * s, ir.center().y}, {ir.x + 13 * s, ir.center().y + 5 * s}, {ir.x + 20 * s, ir.center().y - 5 * s}}, 2 * s, fg);
            g->text({ir.x + 28 * s, ir.center().y}, it.label, 14 * s, fg, 0);
            if (!it.shortcut.empty()) g->text({ir.r() - 10 * s, ir.center().y}, it.shortcut, 12.5f * s, hot ? fg : th.textDim, 2);
            if (!it.children.empty()) drawIcon("chevright", Rect(ir.r() - 22 * s, ir.center().y - 7 * s, 14 * s, 14 * s), fg);
            bool openSub = !it.children.empty() && it.enabled && ((hot && !in->touch) || (hot && in->released));
            if (openSub && m.open != (int)i) {
                menus_.resize(level + 1);
                Menu sub;
                sub.items = it.children;
                sub.rect = menuRect(ir, sub.items, false, true);
                m.open = (int)i;
                menus_.push_back(sub);
                return;  // menus_ changed; redraw next frame
            }
            if (hot && in->released && it.children.empty() && m.rect.contains(in->pressPos)) {
                fire = it.action;
            } else if (hot && in->released && it.children.empty() && menus_.size() > 1) {
                // released over a submenu item after opening it by press (desktop)
                bool pressInAny = false;
                for (auto& mm : menus_) if (mm.rect.contains(in->pressPos)) pressInAny = true;
                if (pressInAny) fire = it.action;
            }
            y += 34 * s;
        }
    }
    if (fire) {
        menus_.clear();
        fire();
    }
}

void UI::drawDialogs() {
    if (dialogs_.empty()) return;
    Dialog d = dialogs_.front();
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    g->fillRect({0, 0, (float)ww, (float)wh}, rgba(0, 0, 0, 90));
    float w = std::min(520 * s, (float)ww - 32 * s);
    float pad = 20 * s;
    float bodyH = 0;
    std::string body;
    for (auto& l : d.lines) { body += l; body += '\n'; }
    if (!body.empty()) body.pop_back();
    bodyH = wrappedText(Rect(0, 0, w - 2 * pad, 0), body, 0, 14 * s, false);
    float maxBody = (float)wh * 0.6f;
    float shownBody = std::min(bodyH, maxBody);
    float h = pad + 26 * s + 10 * s + shownBody + 20 * s + 40 * s + pad;
    Rect r((float)ww / 2 - w / 2, (float)wh / 2 - h / 2, w, h);
    g->fillRoundRect(Rect(r.x, r.y + 6 * s, r.w, r.h), 12 * s, rgba(0, 0, 0, 50));
    g->fillRoundRect(r, 12 * s, th.panel);
    g->text({r.x + pad, r.y + pad + 13 * s}, d.title, 18 * s, th.text, 0);
    Rect br(r.x + pad, r.y + pad + 36 * s, w - 2 * pad, shownBody);
    // dialog body is scrollable when long
    bool wasBlocking = true;
    (void)wasBlocking;
    auto saved = dialogs_;
    dialogs_.clear();  // let the scroll region take input
    float oy = beginScroll("dialog-body", br, bodyH);
    wrappedText(Rect(br.x, br.y + oy, br.w, bodyH), body, th.text, 14 * s);
    endScroll();
    dialogs_ = saved;
    float bx = r.r() - pad;
    int clicked = -1;
    bool enter = in->key(SDLK_RETURN) || in->key(SDLK_KP_ENTER), esc = in->key(SDLK_ESCAPE);
    for (int i = (int)d.buttons.size() - 1; i >= 0; i--) {
        auto& b = d.buttons[(size_t)i];
        float bw = std::max(90 * s, g->textWidth(b.label, 14 * s) + 32 * s);
        Rect bb(bx - bw, r.b() - pad - 38 * s, bw, 38 * s);
        bx -= bw + 10 * s;
        auto savedD = dialogs_;
        dialogs_.clear();
        if (button("dlgbtn" + std::to_string(i) + b.label, bb, b.label, b.primary ? BTN_PRIMARY : BTN_NORMAL)) clicked = i;
        dialogs_ = savedD;
        if (enter && b.primary) clicked = i;
    }
    if (esc && clicked < 0) clicked = (int)d.buttons.size() - 1;
    if (esc && d.buttons.size() > 1) {
        for (size_t i = 0; i < d.buttons.size(); i++) if (!d.buttons[i].primary) { clicked = (int)i; break; }
    }
    if (clicked >= 0) {
        auto action = d.buttons[(size_t)clicked].action;
        dialogs_.erase(dialogs_.begin());
        if (action) action();
    }
}

void UI::openSheet(const std::string& id, const std::string& title, float w, float h, std::function<void(Rect)> draw) {
    sheets_.clear();
    sheets_.push_back({id, title, w, h, std::move(draw)});
    clearFocus();
}

void UI::drawSheet() {
    if (sheets_.empty()) return;
    Sheet sh = sheets_.back();
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    g->fillRect({0, 0, (float)ww, (float)wh}, rgba(0, 0, 0, 90));
    float w = std::min(sh.w * s, (float)ww - 24 * s), h = std::min(sh.h * s, (float)wh - 24 * s);
    Rect r((float)ww / 2 - w / 2, (float)wh / 2 - h / 2, w, h);
    g->fillRoundRect(Rect(r.x, r.y + 6 * s, r.w, r.h), 12 * s, rgba(0, 0, 0, 50));
    g->fillRoundRect(r, 12 * s, th.panel);
    float pad = 18 * s;
    g->text({r.x + pad, r.y + pad + 11 * s}, sh.title, 18 * s, th.text, 0);
    inSheet_ = true;
    bool closeNow = false;
    if (!blocking()) {
        Rect cb(r.r() - pad - 30 * s, r.y + pad - 4 * s, 30 * s, 30 * s);
        if (iconButton("sheet-close-" + sh.id, cb, "close", "", false, true, "Close")) closeNow = true;
        if (in->key(SDLK_ESCAPE) && !editingText()) closeNow = true;
    }
    Rect body(r.x + pad, r.y + pad + 36 * s, r.w - 2 * pad, r.h - 2 * pad - 36 * s);
    if (sh.draw) sh.draw(body);
    inSheet_ = false;
    if (closeNow && !sheets_.empty() && sheets_.back().id == sh.id) sheets_.pop_back();
}

void UI::end() {
    drawSheet();
    drawMenus();
    drawDialogs();
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    if (!toastMsg_.empty() && SDL_GetTicks() < toastUntil_) {
        float px = 14 * s;
        float w = std::min(g->textWidth(toastMsg_, px) + 40 * s, (float)ww - 32 * s);
        auto lines = wrapText(*g, toastMsg_, px, w - 40 * s);
        float h = 20 * s + (float)lines.size() * px * 1.35f;
        Rect r((float)ww / 2 - w / 2, (float)wh - h - 80 * s, w, h);
        g->fillRoundRect(r, 10 * s, rgba(40, 40, 46, 235));
        for (size_t i = 0; i < lines.size(); i++)
            g->text({r.center().x, r.y + 10 * s + px * 1.35f * ((float)i + 0.5f)}, lines[i], px, rgba(255, 255, 255), 1);
    }
    if (!tip_.empty() && SDL_GetTicks() - tipStart_ > 600 && menus_.empty() && dialogs_.empty()) {
        float px = 13 * s;
        float w = g->textWidth(tip_, px) + 16 * s;
        float x = std::min(in->mouse.x + 12 * s, (float)ww - w - 4 * s);
        float y = in->mouse.y + 22 * s;
        if (y + 28 * s > wh) y = in->mouse.y - 34 * s;
        Rect r(x, y, w, 26 * s);
        g->fillRoundRect(r, 5 * s, rgba(40, 40, 46, 240));
        g->text({r.x + 8 * s, r.center().y}, tip_, px, rgba(255, 255, 255), 0);
    }
    if (tip_.empty()) tipId_.clear();
    if (in->released) pressedId_ = 0;
}

// ---------------------------------------------------------------------------
// Vector icons (24x24 design grid scaled into r)
// ---------------------------------------------------------------------------
void UI::drawIcon(const std::string& n, Rect r, uint32_t c) {
    float u = r.w / 24.f;
    auto P = [&](float x, float y) { return Vec2(r.x + x * u, r.y + y * u); };
    float w = std::max(1.2f, 2.0f * u);
    auto L = [&](float x1, float y1, float x2, float y2) { g->line(P(x1, y1), P(x2, y2), w, c); };
    auto PL = [&](std::vector<Vec2> pts, bool closed = false) { for (auto& p : pts) p = P(p.x, p.y); g->polyline(pts, w, c, closed); };
    auto A = [&](float cx, float cy, float rr, float a0, float a1) { g->arc(P(cx, cy), rr * u, a0, a1, w, c); };
    auto ARROW = [&](float x, float y, float dx, float dy) {
        Vec2 d = normalize(Vec2(dx, dy)), nn = perp(d);
        Vec2 tip = P(x, y);
        g->fillConvex({tip + d * (3.5f * u), tip - d * (2.5f * u) + nn * (4 * u), tip - d * (2.5f * u) - nn * (4 * u)}, c);
    };
    if (n == "menu") { L(4, 6, 20, 6); L(4, 12, 20, 12); L(4, 18, 20, 18); }
    else if (n == "undo") { A(13, 13, 7, 180, 360 + 60); ARROW(5.5f, 12, 0, 1); }
    else if (n == "redo") { A(11, 13, 7, 120, 360); ARROW(18.5f, 12, 0, 1); }
    else if (n == "rotate") { A(12, 12, 7.5f, -60, 220); ARROW(16, 5.5f, 1, 0.6f); }
    else if (n == "flip") { L(12, 3, 12, 21); PL({{9, 6}, {3, 18}, {9, 18}}, true); PL({{15, 6}, {21, 18}, {15, 18}}, true); }
    else if (n == "delete") { L(4, 6, 20, 6); L(9, 6, 9, 3.5f); L(9, 3.5f, 15, 3.5f); L(15, 3.5f, 15, 6); PL({{6, 6}, {7, 21}, {17, 21}, {18, 6}}); L(10, 10, 10, 17); L(14, 10, 14, 17); }
    else if (n == "note") { PL({{4, 3}, {20, 3}, {20, 15}, {14, 21}, {4, 21}}, true); PL({{14, 21}, {14, 15}, {20, 15}}); L(7, 8, 16, 8); L(7, 12, 13, 12); }
    else if (n == "zoomin") { g->strokeCircle(P(10, 10), 6.5f * u, w, c); L(15, 15, 21, 21); L(7, 10, 13, 10); L(10, 7, 10, 13); }
    else if (n == "zoomout") { g->strokeCircle(P(10, 10), 6.5f * u, w, c); L(15, 15, 21, 21); L(7, 10, 13, 10); }
    else if (n == "fit") { PL({{3, 8}, {3, 3}, {8, 3}}); PL({{16, 3}, {21, 3}, {21, 8}}); PL({{21, 16}, {21, 21}, {16, 21}}); PL({{8, 21}, {3, 21}, {3, 16}}); g->strokeRoundRect(Rect(P(8, 8).x, P(8, 8).y, 8 * u, 8 * u), 1.5f * u, w, c); }
    else if (n == "play") { g->fillConvex({P(7, 4), P(20, 12), P(7, 20)}, c); }
    else if (n == "stop") { g->fillRoundRect(Rect(P(6, 6).x, P(6, 6).y, 12 * u, 12 * u), 2 * u, c); }
    else if (n == "route") { PL({{4, 20}, {4, 13}, {12, 13}, {12, 5}, {20, 5}}); g->fillCircle(P(4, 20), 2.6f * u, c); g->fillCircle(P(20, 5), 2.6f * u, c); }
    else if (n == "drc") { PL({{12, 2.5f}, {20, 6}, {20, 12}, {12, 21.5f}, {4, 12}, {4, 6}}, true); PL({{8, 12}, {11, 15}, {16, 9}}); }
    else if (n == "parts") { g->strokeRoundRect(Rect(P(3, 3).x, P(3, 3).y, 7.5f * u, 7.5f * u), 1.5f * u, w, c); g->strokeRoundRect(Rect(P(13.5f, 3).x, P(13.5f, 3).y, 7.5f * u, 7.5f * u), 1.5f * u, w, c); g->strokeRoundRect(Rect(P(3, 13.5f).x, P(3, 13.5f).y, 7.5f * u, 7.5f * u), 1.5f * u, w, c); L(17.25f, 13.5f, 17.25f, 21); L(13.5f, 17.25f, 21, 17.25f); }
    else if (n == "inspector") { g->strokeCircle(P(12, 12), 9 * u, w, c); L(12, 11, 12, 17); g->fillCircle(P(12, 7.5f), 1.4f * u, c); }
    else if (n == "export" || n == "share") { PL({{8, 9}, {4, 9}, {4, 21}, {20, 21}, {20, 9}, {16, 9}}); L(12, 3, 12, 15); PL({{8, 7}, {12, 3}, {16, 7}}); }
    else if (n == "save") { PL({{4, 3}, {17, 3}, {21, 7}, {21, 21}, {4, 21}}, true); PL({{8, 3}, {8, 9}, {16, 9}, {16, 3}}); g->strokeRoundRect(Rect(P(7, 14).x, P(7, 14).y, 10 * u, 7 * u), 1, w, c); }
    else if (n == "open") { PL({{3, 19}, {3, 5}, {9, 5}, {11, 7}, {19, 7}, {19, 10}}); PL({{3, 19}, {6, 10}, {22, 10}, {19, 19}}, true); }
    else if (n == "new") { PL({{5, 2.5f}, {14, 2.5f}, {19, 7.5f}, {19, 21.5f}, {5, 21.5f}}, true); L(12, 10, 12, 18); L(8, 14, 16, 14); }
    else if (n == "layer") { PL({{12, 4}, {21, 9}, {12, 14}, {3, 9}}, true); PL({{3, 14}, {12, 19}, {21, 14}}); }
    else if (n == "grid") { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) g->fillCircle(P(6 + 6.f * i, 6 + 6.f * j), 1.6f * u, c); }
    else if (n == "close") { L(6, 6, 18, 18); L(18, 6, 6, 18); }
    else if (n == "eye" || n == "eyeoff") {
        A(12, 21, 12.5f, 225, 315);  // upper lid
        A(12, 3, 12.5f, 45, 135);    // lower lid
        g->fillCircle(P(12, 12), 3.2f * u, c);
        if (n == "eyeoff") L(4, 20, 20, 4);
    }
    else if (n == "lock" || n == "unlock") {
        g->fillRoundRect(Rect(r.x + 5 * u, r.y + 11 * u, 14 * u, 10 * u), 2 * u, c);
        if (n == "lock") A(12, 11, 4.5f, 180, 360);
        else { A(12, 11, 4.5f, 180, 290); }
        L(7.5f, 11, 7.5f, 9.5f);
        if (n == "lock") L(16.5f, 11, 16.5f, 9.5f);
    }
    else if (n == "search") { g->strokeCircle(P(10.5f, 10.5f), 6.5f * u, w, c); L(15.5f, 15.5f, 20.5f, 20.5f); }
    else if (n == "chevron") { PL({{6, 9}, {12, 15}, {18, 9}}); }
    else if (n == "chevright") { PL({{9, 6}, {15, 12}, {9, 18}}); }
    else if (n == "wire") { g->fillCircle(P(5, 19), 2.5f * u, c); g->fillCircle(P(19, 5), 2.5f * u, c); PL({{5, 19}, {5, 12}, {19, 12}, {19, 5}}); }
    else if (n == "copy") { g->strokeRoundRect(Rect(P(8, 8).x, P(8, 8).y, 12 * u, 12 * u), 2 * u, w, c); PL({{16, 4}, {4, 4}, {4, 16}}); }
    else if (n == "help") { g->strokeCircle(P(12, 12), 9 * u, w, c); A(12, 9.5f, 3, 180, 400); L(12, 13, 12, 14.5f); g->fillCircle(P(12, 17.5f), 1.3f * u, c); }
    else if (n == "bolt") { g->fillConvex({P(13, 2), P(5, 14), P(11, 14)}, c); g->fillConvex({P(11, 22), P(19, 10), P(13, 10)}, c); g->fillConvex({P(13, 2), P(11, 14), P(13, 10)}, c); g->fillConvex({P(11, 14), P(13, 10), P(11, 22)}, c); }
    else { g->strokeRoundRect(Rect(P(4, 4).x, P(4, 4).y, 16 * u, 16 * u), 3 * u, w, c); }
}
