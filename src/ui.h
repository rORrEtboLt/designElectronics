// designElectronics — small immediate-mode UI toolkit (buttons, fields, menus,
// dialogs, scrolling) drawn with Gfx. Works with mouse, touch and keyboard.
#pragma once
#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx.h"

struct Input {
    Vec2 mouse;              // pixels
    bool down = false;       // primary button / finger held
    bool pressed = false;    // went down this frame
    bool released = false;   // went up this frame
    bool rightPressed = false;
    bool middleDown = false, rightDown = false;
    bool doubleClick = false;
    float wheelX = 0, wheelY = 0;
    bool touch = false;      // last pointer event came from a touchscreen
    bool wheelPrecise = false;  // trackpad-style smooth scrolling
    std::string text;        // UTF-8 typed this frame
    std::vector<SDL_Keycode> keys;
    SDL_Keymod mods = 0;
    Vec2 pressPos;
    Uint64 pressTime = 0;
    // pinch gesture (two fingers)
    bool pinching = false;
    float pinchScale = 1;    // multiplicative zoom since last frame
    Vec2 pinchCenter, pinchPan;

    bool key(SDL_Keycode k) const { for (auto x : keys) if (x == k) return true; return false; }
    bool cmd() const { return (mods & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }
    bool shift() const { return (mods & SDL_KMOD_SHIFT) != 0; }
    void endFrame() {
        pressed = released = rightPressed = doubleClick = false;
        wheelX = wheelY = 0;
        wheelPrecise = false;
        text.clear();
        keys.clear();
        pinchScale = 1;
        pinchPan = {0, 0};
    }
};

struct Theme {
    uint32_t bg = rgba(226, 226, 228);
    uint32_t panel = rgba(246, 246, 247);
    uint32_t panel2 = rgba(236, 236, 238);
    uint32_t border = rgba(204, 204, 208);
    uint32_t text = rgba(38, 38, 42);
    uint32_t textDim = rgba(112, 112, 120);
    uint32_t accent = rgba(28, 116, 222);
    uint32_t accentText = rgba(255, 255, 255);
    uint32_t button = rgba(255, 255, 255);
    uint32_t buttonHover = rgba(234, 240, 250);
    uint32_t buttonDown = rgba(214, 226, 246);
    uint32_t field = rgba(255, 255, 255);
    uint32_t danger = rgba(208, 52, 52);
    uint32_t ok = rgba(40, 160, 80);
    uint32_t brand = rgba(196, 40, 52);
};

struct MenuItem {
    std::string label, shortcut;
    std::function<void()> action;
    std::vector<MenuItem> children;
    bool enabled = true, checked = false, separator = false;
    static MenuItem sep() { MenuItem m; m.separator = true; return m; }
};

struct DialogButton {
    std::string label;
    std::function<void()> action;
    bool primary = false;
};

enum ButtonStyle { BTN_NORMAL, BTN_FLAT, BTN_TAB, BTN_PRIMARY, BTN_TOOL, BTN_CHIP };

class UI {
public:
    Gfx* g = nullptr;
    Input* in = nullptr;
    SDL_Window* window = nullptr;
    Theme th;
    float s = 1;           // ui scale (pixels per point)

    void begin();
    void end();            // draws popups, dialogs, toasts, tooltip

    // Is the pointer free for this widget (not blocked by a popup/dialog/capture)?
    bool interactive(Rect r) const;
    bool hovered(Rect r) const { return interactive(r) && r.contains(in->mouse); }
    bool blocking() const { return !menus_.empty() || !dialogs_.empty(); }
    bool overBlocking(Vec2 p) const;
    bool editingText() const { return focus_ != 0; }

    // widgets
    bool button(const std::string& id, Rect r, const std::string& label, ButtonStyle st = BTN_NORMAL, bool selected = false, bool enabled = true);
    bool iconButton(const std::string& id, Rect r, const std::string& icon, const std::string& label, bool selected = false, bool enabled = true, const std::string& tip = "");
    bool textField(const std::string& id, Rect r, std::string& value, const std::string& placeholder = "", bool live = false);
    bool comboField(const std::string& id, Rect r, std::string& value, const std::vector<std::string>& options, bool editable);
    bool checkbox(const std::string& id, Rect r, const std::string& label, bool& v);
    void label(Rect r, const std::string& t, uint32_t col, float px, int align = 0, FontFace face = FONT_UI);
    float wrappedText(Rect r, const std::string& t, uint32_t col, float px, bool draw = true);
    void tooltip(Rect r, const std::string& tip);
    void drawIcon(const std::string& name, Rect r, uint32_t col);

    // scroll region: content drawn between begin/end is clipped; returns content origin offset
    float beginScroll(const std::string& id, Rect r, float contentH);
    void endScroll();
    bool scrollDragging() const { return scrollDrag_; }

    // overlays
    void openMenu(Rect anchor, std::vector<MenuItem> items, bool above = false);
    void closeMenus() { menus_.clear(); }
    void dialog(const std::string& title, const std::vector<std::string>& lines, std::vector<DialogButton> buttons);
    void toast(const std::string& msg, float seconds = 3.0f);
    void clearFocus();

    bool hasMenu() const { return !menus_.empty(); }

private:
    struct Menu { Rect rect; std::vector<MenuItem> items; int open = -1; };
    struct Dialog { std::string title; std::vector<std::string> lines; std::vector<DialogButton> buttons; };
    std::vector<Menu> menus_;
    std::vector<Dialog> dialogs_;
    std::string toastMsg_;
    Uint64 toastUntil_ = 0;
    std::string tip_;
    Rect tipRect_;
    Uint64 tipStart_ = 0;
    std::string tipId_;

    size_t focus_ = 0;
    std::string editBuf_;
    size_t caret_ = 0;
    size_t pressedId_ = 0;
    bool menuJustOpened_ = false;
    std::string pendingCombo_;
    size_t pendingComboId_ = 0;

    struct Scroll { size_t id; Rect r; float* y; float contentH; };
    std::vector<Scroll> scrollStack_;
    std::unordered_map<size_t, float> scrollY_;
    size_t scrollDragId_ = 0;
    bool scrollDrag_ = false;
    float scrollDragStart_ = 0;
    Rect clipStack_;
    bool clipping_ = false;

    static size_t hashId(const std::string& s) { return std::hash<std::string>{}(s) | 1; }
    void drawMenus();
    void drawDialogs();
    Rect menuRect(Rect anchor, const std::vector<MenuItem>& items, bool above, bool side) const;
};

std::vector<std::string> wrapText(Gfx& g, const std::string& t, float px, float width);
