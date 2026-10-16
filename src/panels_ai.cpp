// designElectronics — the AI tab: chat with a command-line AI agent (Claude Code,
// Codex, Gemini or a custom command) that builds circuits in the open design.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"

void App::drawRightTabs(Rect r, float y) {
    float s = ui.s, pad = 10 * s;
    const char* names[] = {"Parts", "Layers", "AI"};
    int n = agentsSupported() ? 3 : 2;
    float x = r.x + pad - 6 * s;
    for (int i = 0; i < n; i++) {
        float w = g.textWidth(names[i], 15 * s) + 24 * s;
        if (ui.button(std::string("rtab-") + names[i], Rect(x, y - 6 * s, w, 30 * s), names[i], BTN_TAB, rightTab == i)) rightTab = i;
        x += w;
    }
}

namespace {
// Agents answer in Markdown: drop emphasis markers, turn list markers into bullets.
std::string plainMarkdown(const std::string& md) {
    std::string out;
    size_t a = 0;
    while (a <= md.size()) {
        size_t e = md.find('\n', a);
        if (e == std::string::npos) e = md.size();
        std::string line = md.substr(a, e - a);
        // tables: drop |---| rules, show rows as "a · b · c"
        std::string t = line;
        t.erase(0, t.find_first_not_of(' '));
        if (!t.empty() && t[0] == '|') {
            if (t.find_first_not_of("|-: ") == std::string::npos) { a = e + 1; continue; }
            std::string row, cell;
            for (size_t i = 1; i < t.size(); i++) {
                if (t[i] == '|') {
                    size_t b0 = cell.find_first_not_of(' '), b1 = cell.find_last_not_of(' ');
                    if (b0 != std::string::npos) row += (row.empty() ? "" : " · ") + cell.substr(b0, b1 - b0 + 1);
                    cell.clear();
                } else cell += t[i];
            }
            line = row;
        }
        size_t ind = line.find_first_not_of(' ');
        if (ind != std::string::npos && line.size() > ind + 1 && (line[ind] == '-' || line[ind] == '*') && line[ind + 1] == ' ')
            line = line.substr(0, ind) + "• " + line.substr(ind + 2);
        while (!line.empty() && line[0] == '#') line.erase(0, 1);
        if (!line.empty() && line[0] == ' ' && md[a] == '#') line.erase(0, 1);
        std::string clean;
        for (size_t i = 0; i < line.size(); i++) {
            if ((line[i] == '*' || line[i] == '_') && i + 1 < line.size() && line[i + 1] == line[i]) { i++; continue; }
            if (line[i] == '`') continue;
            clean += line[i];
        }
        out += clean;
        if (e < md.size()) out += '\n';
        a = e + 1;
    }
    return out;
}

const char* agentName(const std::string& k) {
    return k == "claude" ? "Claude Code" : k == "codex" ? "Codex" : k == "gemini" ? "Gemini" : "Custom";
}
}  // namespace

void App::drawAIPanel(Rect r) {
    float s = ui.s;
    auto& th = ui.th;
    g.fillRect(r, th.panel);
    g.fillRect({r.x, r.y, std::max(1.f, s), r.h}, th.border);
    float pad = 10 * s;
    float y = r.y + pad;
    if (compact) {
        g.text({r.x + pad + 2 * s, y + 11 * s}, "AI assistant", 16 * s, th.text, 0);
        if (ui.iconButton("closeai", Rect(r.r() - 40 * s, y - 4 * s, 32 * s, 32 * s), "close", "")) drawerAI = false;
    } else drawRightTabs(r, y);
    y += 32 * s;
    float x = r.x + pad, w = r.w - 2 * pad;

    if (!agentsSupported()) {
        ui.wrappedText(Rect(x, y, w, 0),
                       "The AI assistant drives an AI tool installed on your computer, such as Claude Code. Use the desktop app for it, "
                       "or connect your AI to designElectronics through MCP (see the AI section of the docs).",
                       th.textDim, 13.5f * s);
        return;
    }
    if (!agent) detectAgents();
    bool running = false, detected = false;
    std::map<std::string, std::string> paths;
    std::vector<ChatMsg> msgs = agentMessages(running, detected, paths);
    for (auto& m : msgs) if (m.role == ChatMsg::R_ASSISTANT) m.text = plainMarkdown(m.text);
    // the agent just finished and changed the design: show everything it built
    if (agentWasRunning && !running && agentStartRev != doc.revision) for (auto& c : cam) c.valid = false;
    if (running && !agentWasRunning) agentStartRev = doc.revision;
    agentWasRunning = running;
    if (!detected) detectAgents();

    // ---- agent picker ----
    float cx = x;
    for (const char* k : {"claude", "codex", "gemini", "custom"}) {
        bool found = std::string(k) == "custom" || paths.count(k);
        std::string label = agentName(k);
        float bw = g.textWidth(label, 12.5f * s) + 18 * s;
        if (cx + bw > x + w) break;
        if (ui.button(std::string("ai-k-") + k, Rect(cx, y, bw, 26 * s), label, BTN_CHIP, agentKind == k, !running)) agentKind = k;
        if (!found && detected) ui.tooltip(Rect(cx, y, bw, 26 * s), std::string(agentName(k)) + " isn't installed");
        cx += bw + 4 * s;
    }
    y += 34 * s;
    bool ready = agentKind == "custom" ? !agentCustomCommand.empty() : paths.count(agentKind) > 0;

    // ---- setup notes when the chosen agent is missing ----
    if (agentKind == "custom") {
        g.text({x, y + 8 * s}, "Command ({prompt} and {mcp_config} are filled in)", 12 * s, th.textDim, 0);
        ui.textField("ai-custom", Rect(x, y + 18 * s, w, 30 * s), agentCustomCommand, "mytool --mcp-config {mcp_config} -p {prompt}", true);
        y += 56 * s;
    } else if (detected && !ready) {
        std::string how = agentKind == "claude"
            ? "Claude Code isn't installed. In a terminal run:\n  npm install -g @anthropic-ai/claude-code\nthen run  claude  once to sign in, and press Check again."
            : agentKind == "codex" ? "Codex CLI isn't installed. In a terminal run:\n  npm install -g @openai/codex\nthen  codex login, and press Check again."
                                   : "Gemini CLI isn't installed. In a terminal run:\n  npm install -g @google/gemini-cli\nthen run  gemini  once to sign in, and press Check again.";
        float h = ui.wrappedText(Rect(x, y, w, 0), how, th.text, 13 * s);
        y += h + 8 * s;
        if (ui.button("ai-check", Rect(x, y, 110 * s, 30 * s), "Check again")) { agent.reset(); detectAgents(); }
        if (agentKind == "claude" && ui.button("ai-guide", Rect(x + 118 * s, y, 130 * s, 30 * s), "Install guide", BTN_FLAT))
            SDL_OpenURL("https://docs.claude.com/en/docs/claude-code/setup");
        y += 42 * s;
    } else if (!detected) {
        g.text({x, y + 8 * s}, "Looking for AI tools on this computer…", 12.5f * s, th.textDim, 0);
        y += 24 * s;
    }

    // ---- transcript ----
    float inputH = 40 * s;
    Rect list(x, y, w, r.b() - y - inputH - 2 * pad - 22 * s);
    g.fillRoundRect(list, 8 * s, th.field);
    Rect inner = list.inset(10 * s);
    std::vector<float> heights;
    float total = 0;
    float px = 13.5f * s;
    for (auto& m : msgs) {
        float bw = m.role == ChatMsg::R_USER ? inner.w * 0.85f : inner.w;
        float h;
        if (m.role == ChatMsg::R_TOOL || m.role == ChatMsg::R_INFO) h = 20 * s;
        else h = ui.wrappedText(Rect(0, 0, bw - 20 * s, 0), m.text, 0, m.role == ChatMsg::R_TOOL_ERROR ? 12 * s : px, false) + 16 * s;
        heights.push_back(h);
        total += h + 6 * s;
    }
    if (running) total += 26 * s;
    if (msgs.empty()) {
        float h = ui.wrappedText(Rect(inner.x, inner.y, inner.w, 0),
                                 std::string("Describe a circuit and ") + agentName(agentKind) +
                                     " builds it here: it picks parts, wires them on the breadboard, writes sketches for boards and runs "
                                     "the simulation to check its work. You can undo anything it does.",
                                 th.textDim, 13 * s);
        float sy = inner.y + h + 12 * s;
        const char* ideas[] = {"Blink a red LED with an ESP32", "A 555 timer that flashes an LED twice a second",
                               "Arduino + 74HC595 driving 8 LEDs as a running light", "Add a pushbutton that turns the LED on",
                               "Make a 50×40 mm PCB for this and autoroute it"};
        for (auto* idea : ideas) {
            Rect b(inner.x, sy, inner.w, 30 * s);
            if (b.b() > inner.b()) break;
            bool hot = ui.hovered(b);
            if (ui.button(std::string("ai-idea-") + idea, b, "", BTN_FLAT, false, ready && !running)) startAgent(idea);
            g.fillRoundRect(b, 6 * s, hot ? withAlpha(th.accent, 30) : th.panel2);
            std::string t = idea;
            while (t.size() > 4 && g.textWidth(t, 12.5f * s) > b.w - 20 * s) { t.resize(t.size() - 4); t += "…"; }
            g.text({b.x + 10 * s, b.center().y}, t, 12.5f * s, ready ? th.text : th.textDim, 0);
            sy += 36 * s;
        }
    } else {
        if (msgs.size() != agentSeen) { ui.scrollTo("ai-chat", 1e9f); agentSeen = msgs.size(); }  // follow new messages
        float oy = ui.beginScroll("ai-chat", inner, total);
        float yy = inner.y + oy;
        for (size_t i = 0; i < msgs.size(); i++) {
            const ChatMsg& m = msgs[i];
            float h = heights[i];
            if (yy + h >= inner.y && yy <= inner.b()) {
                switch (m.role) {
                    case ChatMsg::R_USER: {
                        float bw = inner.w * 0.85f;
                        Rect b(inner.r() - bw, yy, bw, h);
                        g.fillRoundRect(b, 10 * s, th.accent);
                        ui.wrappedText(Rect(b.x + 10 * s, b.y + 8 * s, b.w - 20 * s, 0), m.text, rgba(255, 255, 255), px);
                        break;
                    }
                    case ChatMsg::R_ASSISTANT: {
                        Rect b(inner.x, yy, inner.w, h);
                        g.fillRoundRect(b, 10 * s, th.panel2);
                        ui.wrappedText(Rect(b.x + 10 * s, b.y + 8 * s, b.w - 20 * s, 0), m.text, th.text, px);
                        break;
                    }
                    case ChatMsg::R_TOOL: {
                        ui.drawIcon("bolt", Rect(inner.x + 2 * s, yy + 3 * s, 14 * s, 14 * s), th.accent);
                        std::string t = m.text;
                        while (t.size() > 4 && g.textWidth(t, 12 * s) > inner.w - 24 * s) { t.resize(t.size() - 4); t += "…"; }
                        g.text({inner.x + 22 * s, yy + 10 * s}, t, 12 * s, th.textDim, 0);
                        break;
                    }
                    case ChatMsg::R_TOOL_ERROR:
                        ui.wrappedText(Rect(inner.x + 22 * s, yy + 4 * s, inner.w - 22 * s, 0), m.text, th.danger, 12 * s);
                        break;
                    case ChatMsg::R_ERROR: {
                        Rect b(inner.x, yy, inner.w, h);
                        g.fillRoundRect(b, 8 * s, withAlpha(th.danger, 30));
                        ui.wrappedText(Rect(b.x + 10 * s, b.y + 8 * s, b.w - 20 * s, 0), m.text, th.danger, px);
                        break;
                    }
                    case ChatMsg::R_INFO:
                        g.text({inner.center().x, yy + 10 * s}, m.text, 11.5f * s, th.textDim, 1);
                        break;
                }
            }
            yy += h + 6 * s;
        }
        if (running) {
            int dots = (int)(SDL_GetTicks() / 400) % 4;
            g.text({inner.x + 2 * s, yy + 10 * s}, std::string(agentName(agentKind)) + " is working" + std::string((size_t)dots, '.'), 12.5f * s, th.accent, 0);
        }
        ui.endScroll();
    }

    // ---- input ----
    float iy = list.b() + 8 * s;
    float bw = 70 * s;
    bool enter = in.key(SDLK_RETURN) || in.key(SDLK_KP_ENTER);
    ui.textField("ai-input", Rect(x, iy, w - bw - 6 * s, inputH), agentInput, running ? "Working…" : "Describe a circuit or a change…", true);
    if (running) {
        if (ui.button("ai-stop", Rect(x + w - bw, iy, bw, inputH), "Stop")) stopAgent();
    } else {
        bool can = ready && !agentInput.empty();
        if ((ui.button("ai-send", Rect(x + w - bw, iy, bw, inputH), "Send", BTN_PRIMARY, false, can) || (enter && can))) {
            std::string p = agentInput;
            agentInput.clear();
            startAgent(p);
        }
    }
    // footer: new chat · use another MCP client
    float fy = iy + inputH + 4 * s;
    if (!msgs.empty() && !running && ui.button("ai-new", Rect(x, fy, 80 * s, 20 * s), "New chat", BTN_FLAT)) newAgentChat();
    if (ui.button("ai-mcp", Rect(x + w - 190 * s, fy, 190 * s, 20 * s), "Copy MCP config for other apps", BTN_FLAT)) {
        SDL_SetClipboardText(standaloneMcpConfig().c_str());
        ui.toast("MCP config copied — paste it into Claude Desktop, Cursor, Conductor or any MCP client.");
    }
}
