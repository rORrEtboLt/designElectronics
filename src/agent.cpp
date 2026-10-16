// designElectronics — the AI tab's engine: run a command-line AI agent the user
// already has (Claude Code, Codex CLI, Gemini CLI or a custom command) with the
// designElectronics MCP tools, connected live to the open design.
//
//   app  ──listens on 127.0.0.1:port──▶  `designElectronics --mcp --connect port --token t`
//    ▲                                         ▲  stdio MCP
//    └─ applies tool calls to the live doc     └── started by the agent (claude -p …)
//
// The agent runs in an empty folder and is only allowed designElectronics' tools.
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include "app.h"
#include "mcp.h"
#include "net.h"

namespace {

std::string prefDirAgent() {
    char* p = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string s = p ? p : "";
    SDL_free(p);
    return s + "agent";
}

std::string selfExe() {
    // path of this executable (inside the .app bundle on macOS)
    const char* base = SDL_GetBasePath();
    std::string b = base ? base : "";
#ifdef _WIN32
    return b + "designElectronics.exe";
#elif defined(SDL_PLATFORM_MACOS)
    if (b.find(".app/Contents/Resources") != std::string::npos) return b.substr(0, b.find("Resources")) + "MacOS/designElectronics";
    return b + "designElectronics";
#else
    return b + "designElectronics";
#endif
}

std::string jsonStr(const std::string& s) { return Json(s).dump(-1); }

// What an agent did, in words ("Adding a led", "Wiring R1.pin1 → LED1.anode")
std::string describeTool(std::string name, const Json& in) {
    const std::string pre = "mcp__designElectronics__";
    if (name.rfind(pre, 0) == 0) name = name.substr(pre.size());
    auto S = [&](const char* k) { return in[k].isString() ? in[k].str() : in[k].isNull() ? std::string() : in[k].dump(-1); };
    if (name == "add_part") return "Adding " + S("type");
    if (name == "connect") return "Wiring " + S("from_part") + "." + S("from_pin") + " → " + S("to_part") + "." + S("to_pin");
    if (name == "place_on_breadboard") return "Plugging " + S("part") + " into " + S("hole");
    if (name == "set_part_properties") return in["properties"].has("code") ? "Writing " + S("part") + "'s program" : "Updating " + S("part");
    if (name == "simulate") return "Running the simulation";
    if (name == "get_design") return "Looking at the design";
    if (name == "list_parts") return "Searching parts" + (S("query").empty() ? "" : " for “" + S("query") + "”");
    if (name == "get_part_info") return "Reading " + S("type") + " pinout";
    if (name == "autoroute") return "Routing the PCB";
    if (name == "run_drc") return "Checking design rules";
    if (name == "move_part") return "Moving " + S("part");
    if (name == "remove_part") return "Removing " + S("part");
    if (name == "load_example") return "Loading an example";
    if (name == "new_design") return "Starting a new design";
    if (name == "export") return "Exporting " + S("format");
    if (name == "save_design") return "Saving";
    return name;
}

const char* kSystemPrompt =
    "You are the AI assistant inside designElectronics, an electronics design app (breadboard, schematic, PCB, simulator). "
    "The user sees the design update live while you work. Use the designElectronics MCP tools to read and change the design "
    "that is currently open: start with get_design. Build circuits that work: pick parts with list_parts/get_part_info, "
    "add_part, place parts on a breadboard with place_on_breadboard (check the reported holes for shorts) and/or connect pins, "
    "add current-limiting resistors for LEDs, then simulate to verify. For microcontroller boards, write an Arduino sketch with "
    "set_part_properties {\"code\": …} and check its Serial output in simulate. Don't save or export files unless asked. "
    "Keep replies short: say what you built and what to look at.";

}  // namespace

// ---------------------------------------------------------------------------
struct App::AgentState {
    std::mutex m;
    std::vector<ChatMsg> messages;
    bool running = false;
    std::string sessionId;          // Claude Code: continue the same conversation
    SDL_Process* process = nullptr;
    // detection
    bool detecting = false, detected = false;
    std::map<std::string, std::string> paths;  // agent id -> executable
    std::string loginPath;
    // bridge
    std::unique_ptr<McpSession> session;  // main thread only
    std::atomic<bool> stopBridge{false};
    int port = 0;
    std::string token;
    long long listener = -1;
    void add(ChatMsg::Role r, const std::string& t) {
        std::lock_guard<std::mutex> lk(m);
        if (r == ChatMsg::R_ASSISTANT && !messages.empty() && messages.back().role == ChatMsg::R_ASSISTANT && messages.back().streaming) {
            messages.back().text += t;
            return;
        }
        messages.push_back({r, t, false});
    }
};

bool App::agentsSupported() const {
#if defined(DE_HAVE_NET) && !defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_ANDROID)
    return true;
#else
    return false;
#endif
}

// ---- find installed agents (through the login shell, so PATH matches the terminal) ----
void App::detectAgents() {
    if (!agent) agent = std::make_shared<AgentState>();
    auto st = agent;
    if (st->detecting) return;
    st->detecting = true;
#if defined(DE_HAVE_NET)
    std::thread([st]() {
        std::vector<std::string> args;
#ifdef _WIN32
        args = {"cmd", "/c", "echo PATH=%PATH% & where claude & where codex & where gemini"};
#else
        const char* sh = SDL_getenv("SHELL");
        std::string shell = sh && *sh ? sh : "/bin/zsh";
        args = {shell, "-lc", "echo PATH=$PATH; command -v claude; command -v codex; command -v gemini; true"};
#endif
        std::vector<const char*> argv;
        for (auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        std::string out;
        if (SDL_Process* p = SDL_CreateProcess(argv.data(), true)) {
            size_t n = 0;
            int code = 0;
            if (void* data = SDL_ReadProcess(p, &n, &code)) { out.assign((const char*)data, n); SDL_free(data); }
            SDL_DestroyProcess(p);
        }
        std::map<std::string, std::string> found;
        std::string pathVar;
        size_t a = 0;
        while (a < out.size()) {
            size_t e = out.find('\n', a);
            if (e == std::string::npos) e = out.size();
            std::string line = out.substr(a, e - a);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            a = e + 1;
            if (line.rfind("PATH=", 0) == 0) { pathVar = line.substr(5); continue; }
            for (const char* id : {"claude", "codex", "gemini"}) {
                std::string base = line.substr(line.find_last_of("/\\") + 1);
                if (base == id || base == std::string(id) + ".exe" || base == std::string(id) + ".cmd") found.emplace(id, line);
            }
        }
        // common install locations (in case the shell profile didn't load)
        const char* home = SDL_getenv("HOME");
        std::string h = home ? home : "";
        for (const std::string& c : {h + "/.local/bin/claude", h + "/.claude/local/claude", std::string("/opt/homebrew/bin/claude"), std::string("/usr/local/bin/claude")}) {
            SDL_PathInfo info;
            if (!found.count("claude") && SDL_GetPathInfo(c.c_str(), &info)) found["claude"] = c;
        }
        std::lock_guard<std::mutex> lk(st->m);
        st->paths = found;
        st->loginPath = pathVar;
        st->detecting = false;
        st->detected = true;
    }).detach();
#else
    st->detecting = false;
    st->detected = true;
#endif
}

// ---- bridge: the agent's MCP process forwards tool calls here ----
void App::ensureAgentBridge() {
#if defined(DE_HAVE_NET)
    AgentState& st = *agent;
    if (st.port) return;
    st.session = std::make_unique<McpSession>(&doc);
    st.session->beforeEdit = [this]() { checkpoint(); };
    st.session->afterEdit = [this]() {
        doc.syncWireEnds();
        changed();
        pruneSelection();
        drcIssues.clear();
    };
    st.session->onReplaced = [this]() {
        for (auto& c : cam) c.valid = false;
        selParts.clear(); selWires.clear(); selNotes.clear();
        codeOpen = false;
    };
    std::random_device rd;
    char tok[33];
    snprintf(tok, sizeof tok, "%08x%08x%08x%08x", rd(), rd(), rd(), rd());
    st.token = tok;
    int port = 0;
    st.listener = net::listenLocal(port);
    if (st.listener == net::kInvalid) { st.add(ChatMsg::R_ERROR, "Couldn't open the local connection for the AI agent."); return; }
    st.port = port;
    auto stp = agent;
    App* app = this;
    std::thread([stp, app]() {
        while (!stp->stopBridge) {
            net::Sock c = net::acceptClient(stp->listener, 200);
            if (c == net::kInvalid) continue;
            std::thread([stp, app, c]() {
                std::string buf, line;
                while (!stp->stopBridge && net::readLine(c, buf, line)) {
                    Json req = Json::parse(line);
                    Json resp = Json::object();
                    if (req["token"].str() != stp->token) {
                        resp.set("isError", true);
                        resp.set("text", "Error: not authorised");
                    } else {
                        // run the tool on the main thread (the document lives there) and wait
                        struct Wait { std::mutex m; std::condition_variable cv; bool done = false; std::string text; bool err = false; };
                        auto w = std::make_shared<Wait>();
                        std::string name = req["name"].str();
                        Json args = req["arguments"];
                        app->runOnMain([stp, w, name, args]() {
                            bool err = false;
                            std::string text = stp->session ? stp->session->callTool(name, args, err) : "Error: no session";
                            std::lock_guard<std::mutex> lk(w->m);
                            w->text = text;
                            w->err = err;
                            w->done = true;
                            w->cv.notify_all();
                        });
                        std::unique_lock<std::mutex> lk(w->m);
                        w->cv.wait_for(lk, std::chrono::seconds(120), [&] { return w->done; });
                        resp.set("isError", w->done ? w->err : true);
                        resp.set("text", w->done ? w->text : "Error: the app didn't respond");
                    }
                    if (!net::sendAll(c, resp.dump(-1) + "\n")) break;
                }
                net::closeSock(c);
            }).detach();
        }
    }).detach();
#endif
}

// ---- run the agent on one message ----
void App::startAgent(const std::string& prompt) {
#if defined(DE_HAVE_NET)
    if (!agent) agent = std::make_shared<AgentState>();
    AgentState& st = *agent;
    if (st.running) return;
    ensureAgentBridge();
    if (!st.port) return;
    std::string kind = agentKind;
    std::string exe;
    {
        std::lock_guard<std::mutex> lk(st.m);
        if (st.paths.count(kind)) exe = st.paths[kind];
    }
    if (kind != "custom" && exe.empty()) { st.add(ChatMsg::R_ERROR, "That AI tool isn't installed (see the setup notes above)."); return; }
    st.add(ChatMsg::R_USER, prompt);
    // working folder + MCP config pointing back at this app
    std::string dir = prefDirAgent();
    SDL_CreateDirectory(dir.c_str());
    std::string self = selfExe();
    std::string portS = std::to_string(st.port);
    std::string cfg = dir + "/mcp.json";
    std::string server = "{\"command\":" + jsonStr(self) + ",\"args\":[\"--mcp\",\"--connect\"," + jsonStr(portS) + ",\"--token\"," + jsonStr(st.token) + "]}";
    writeTextFile(cfg, "{\"mcpServers\":{\"designElectronics\":" + server + "}}");
    std::vector<std::string> args;
    if (kind == "claude") {
        args = {exe, "-p", prompt, "--output-format", "stream-json", "--verbose", "--mcp-config", cfg, "--strict-mcp-config",
                "--allowedTools", "mcp__designElectronics",
                "--disallowedTools", "Bash", "Edit", "Write", "MultiEdit", "NotebookEdit", "Read", "Glob", "Grep", "WebFetch", "WebSearch", "Task",
                "--append-system-prompt", kSystemPrompt};
        if (!st.sessionId.empty()) { args.push_back("--resume"); args.push_back(st.sessionId); }
    } else if (kind == "codex") {
        args = {exe, "exec", "--skip-git-repo-check", "--json",
                "-c", "mcp_servers.designElectronics.command=" + jsonStr(self),
                "-c", "mcp_servers.designElectronics.args=[\"--mcp\",\"--connect\"," + jsonStr(portS) + ",\"--token\"," + jsonStr(st.token) + "]",
                std::string(kSystemPrompt) + "\n\nUser request: " + prompt};
    } else if (kind == "gemini") {
        SDL_CreateDirectory((dir + "/.gemini").c_str());
        writeTextFile(dir + "/.gemini/settings.json",
                      "{\"mcpServers\":{\"designElectronics\":" + server + "},"
                      "\"excludeTools\":[\"run_shell_command\",\"write_file\",\"replace\",\"read_file\",\"web_fetch\",\"google_web_search\"]}");
        args = {exe, "--yolo", "-p", std::string(kSystemPrompt) + "\n\nUser request: " + prompt};
    } else {  // custom command template
        std::string tpl = agentCustomCommand;
        std::string cur;
        bool q = false;
        for (size_t i = 0; i <= tpl.size(); i++) {
            char c = i < tpl.size() ? tpl[i] : ' ';
            if (c == '"') { q = !q; continue; }
            if (c == ' ' && !q) { if (!cur.empty()) args.push_back(cur); cur.clear(); continue; }
            cur += c;
        }
        for (auto& a : args) {
            size_t p;
            while ((p = a.find("{prompt}")) != std::string::npos) a.replace(p, 8, prompt);
            while ((p = a.find("{mcp_config}")) != std::string::npos) a.replace(p, 12, cfg);
        }
        if (args.empty()) { st.add(ChatMsg::R_ERROR, "Enter a command, e.g. mytool --mcp-config {mcp_config} -p {prompt}"); return; }
    }
    // environment: the login shell's PATH so node-based CLIs find node
    SDL_Environment* env = SDL_CreateEnvironment(true);
    if (!st.loginPath.empty()) SDL_SetEnvironmentVariable(env, "PATH", st.loginPath.c_str(), true);
    std::vector<const char*> argv;
    for (auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    std::string errFile = dir + "/stderr.txt";
    SDL_IOStream* errIO = SDL_IOFromFile(errFile.c_str(), "wb");
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)argv.data());
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, dir.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    if (errIO) {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_POINTER, errIO);
    }
    SDL_Process* proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (!proc) {
        if (errIO) SDL_CloseIO(errIO);
        st.add(ChatMsg::R_ERROR, std::string("Couldn't start ") + args[0] + ": " + SDL_GetError());
        return;
    }
    st.process = proc;
    st.running = true;
    auto stp = agent;
    Uint64 started = SDL_GetTicks();
    std::thread([stp, proc, errIO, errFile, kind, started]() {
        SDL_IOStream* out = SDL_GetProcessOutput(proc);
        std::string buf;
        char tmp[4096];
        bool gotResult = false;
        auto handleLine = [&](const std::string& line) {
            if (line.empty()) return;
            Json j = Json::parse(line);
            if (!j.isObject()) {  // plain-text agents (Gemini, custom): show as the reply
                if (kind == "claude" || kind == "codex") return;
                stp->add(ChatMsg::R_ASSISTANT, line + "\n");
                std::lock_guard<std::mutex> lk(stp->m);
                if (!stp->messages.empty()) stp->messages.back().streaming = true;
                return;
            }
            if (kind == "claude") {
                std::string type = j["type"].str();
                if (type == "system" && j["subtype"].str() == "init") {
                    std::lock_guard<std::mutex> lk(stp->m);
                    stp->sessionId = j["session_id"].str();
                } else if (type == "assistant") {
                    for (auto& c : j["message"]["content"].items()) {
                        if (c["type"].str() == "text" && !c["text"].str().empty()) stp->add(ChatMsg::R_ASSISTANT, c["text"].str());
                        else if (c["type"].str() == "tool_use") stp->add(ChatMsg::R_TOOL, describeTool(c["name"].str(), c["input"]));
                    }
                } else if (type == "user") {
                    for (auto& c : j["message"]["content"].items())
                        if (c["type"].str() == "tool_result" && c["is_error"].boolean()) {
                            std::string t = c["content"].isString() ? c["content"].str() : c["content"][(size_t)0]["text"].str();
                            stp->add(ChatMsg::R_TOOL_ERROR, t.substr(0, 300));
                        }
                } else if (type == "result") {
                    gotResult = true;
                    char b[160];
                    double secs = (double)(SDL_GetTicks() - started) / 1000.0;
                    if (j["total_cost_usd"].isNumber()) snprintf(b, sizeof b, "Done in %.0f s · $%.3f", secs, j["total_cost_usd"].num());
                    else snprintf(b, sizeof b, "Done in %.0f s", secs);
                    if (j["is_error"].boolean()) stp->add(ChatMsg::R_ERROR, j["result"].str().empty() ? "The agent stopped with an error." : j["result"].str());
                    stp->add(ChatMsg::R_INFO, b);
                }
            } else if (kind == "codex") {
                // newer CLI: {"type":"item.completed","item":{"type":"agent_message","text":…}}; older: {"msg":{"type":"agent_message","message":…}}
                const Json& item = j["item"];
                std::string it = item["type"].str(), mt = j["msg"]["type"].str();
                if (it == "agent_message") stp->add(ChatMsg::R_ASSISTANT, item["text"].str());
                else if (mt == "agent_message") stp->add(ChatMsg::R_ASSISTANT, j["msg"]["message"].str());
                else if (it == "mcp_tool_call" && j["type"].str() == "item.started") stp->add(ChatMsg::R_TOOL, describeTool(item["tool"].str(), item["arguments"]));
                else if (mt == "mcp_tool_call_begin") stp->add(ChatMsg::R_TOOL, describeTool(j["msg"]["invocation"]["tool"].str(), j["msg"]["invocation"]["arguments"]));
                if (j["type"].str() == "turn.completed" || mt == "task_complete") gotResult = true;
            }
        };
        for (;;) {
            size_t n = out ? SDL_ReadIO(out, tmp, sizeof tmp) : 0;
            if (n == 0) {
                if (out && SDL_GetIOStatus(out) == SDL_IO_STATUS_NOT_READY) { SDL_Delay(15); continue; }
                break;
            }
            buf.append(tmp, n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                buf.erase(0, nl + 1);
                handleLine(line);
            }
        }
        if (!buf.empty()) handleLine(buf);
        int code = 0;
        SDL_WaitProcess(proc, true, &code);
        if (errIO) SDL_CloseIO(errIO);
        std::string err = readTextFile(errFile);
        bool killed;
        {
            std::lock_guard<std::mutex> lk(stp->m);
            killed = stp->process == nullptr;
            stp->process = nullptr;
            if (!stp->messages.empty()) stp->messages.back().streaming = false;
        }
        if (killed) stp->add(ChatMsg::R_INFO, "Stopped.");
        else if (code != 0 && !gotResult) {
            std::string e = err;
            if (e.size() > 600) e = e.substr(e.size() - 600);
            stp->add(ChatMsg::R_ERROR, "The agent exited with code " + std::to_string(code) + (e.empty() ? "." : ":\n" + e));
        } else if (kind != "claude" && kind != "codex") stp->add(ChatMsg::R_INFO, "Done.");
        SDL_DestroyProcess(proc);
        std::lock_guard<std::mutex> lk(stp->m);
        stp->running = false;
    }).detach();
#else
    (void)prompt;
#endif
}

void App::stopAgent() {
    if (!agent) return;
    std::lock_guard<std::mutex> lk(agent->m);
    if (agent->process) {
        SDL_KillProcess(agent->process, false);
        agent->process = nullptr;
    }
}

void App::newAgentChat() {
    if (!agent) return;
    std::lock_guard<std::mutex> lk(agent->m);
    if (agent->running) return;
    agent->messages.clear();
    agent->sessionId.clear();
}

std::vector<App::ChatMsg> App::agentMessages(bool& running, bool& detected, std::map<std::string, std::string>& paths) {
    if (!agent) { running = false; detected = false; return {}; }
    std::lock_guard<std::mutex> lk(agent->m);
    running = agent->running;
    detected = agent->detected;
    paths = agent->paths;
    return agent->messages;
}

std::string App::standaloneMcpConfig() const {
    return "{\n  \"mcpServers\": {\n    \"designElectronics\": { \"command\": " + jsonStr(selfExe()) + ", \"args\": [\"--mcp\"] }\n  }\n}";
}
