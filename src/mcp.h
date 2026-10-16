// designElectronics — MCP tools as a library: used by `designElectronics --mcp`
// (stdio server, its own design) and by the in-app AI tab (the live design).
#pragma once
#include <functional>
#include <memory>
#include <string>

#include "doc.h"

class McpSession {
public:
    // live = nullptr: the session owns its own design (standalone server).
    explicit McpSession(Document* live = nullptr);
    ~McpSession();
    // Run one tool; returns its text result and whether it failed.
    std::string callTool(const std::string& name, const Json& args, bool& isError);
    // Hooks for the app: before/after a tool that changes the design (undo, redraw),
    // and when the whole design was replaced (fit the view).
    std::function<void()> beforeEdit, afterEdit, onReplaced;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// stdio MCP server. With connectPort > 0 it forwards tool calls to the running
// app (the AI tab), which applies them to the open design.
int runMcpServer(int connectPort = 0, const std::string& token = "");
