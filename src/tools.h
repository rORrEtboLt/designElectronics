// designElectronics — autorouter, design-rule check, simulator, exporters, examples.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "doc.h"

// ---- PCB autorouter (grid A*, two layers with vias) ----
struct RouteResult {
    int routed = 0, failed = 0;
    std::string message;
};
RouteResult autoroute(Document& doc, const Connectivity& conn, bool ripUpAutorouted);

// ---- design rule check ----
struct DrcIssue {
    Vec2 pos;
    float radius = 30;
    std::string message;
};
std::vector<DrcIssue> runDrc(const Document& doc, const Connectivity& conn);

// ---- transient circuit simulator (see sim.cpp) ----
struct SimResult {
    bool ran = false;
    std::string message;
    double time = 0;                          // simulated seconds
    std::vector<double> netVoltage;           // per global net
    std::map<int, float> brightness;          // LED part id -> 0..1 (above 1 = overdriven)
    std::map<int, uint32_t> glow;             // RGB LED part id -> mixed colour
    std::map<int, std::vector<float>> segments;  // 7-segment / RGB part id -> per-LED brightness
    std::map<int, std::string> meterText;     // multimeter display
    std::map<int, float> activity;            // motor/buzzer/relay part id -> 0..1
    std::set<int> damaged;                    // parts beyond their ratings (smoke!)
    std::vector<std::string> warnings;
    // boards running a program (part id ->)
    struct Code {
        bool running = false;
        int line = 0;              // line being executed
        int errorLine = 0;         // compile or runtime error (0 = none)
        std::string error;
    };
    std::map<int, Code> code;
    std::map<int, std::string> serial;       // new Serial output during this step
};

class Simulator {
public:
    Simulator();
    ~Simulator();
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    void reset();
    void serialInput(int partId, const std::string& text);  // type into a board's Serial port
    // Advance the circuit by `seconds` of simulated time (1 ms steps, capped per call).
    SimResult step(const Document& doc, const Connectivity& conn, const std::set<int>& pressedButtons, double seconds);
    double time = 0;

private:
    struct Impl;
    Impl* impl_;
};

// Compile a board's program without running it (live syntax check). Returns false with the error line/message.
bool checkSketch(const PartInst& part, int& errorLine, std::string& error);

// Convenience: run a fresh simulation for `seconds` and return the final state.
SimResult simulate(const Document& doc, const Connectivity& conn, const std::set<int>& pressedButtons, double seconds = 0.5);

// ---- exporters ----
bool exportGerberZip(const Document& doc, const Connectivity& conn, const std::string& zipPath, std::string* msg);
std::string exportSvg(const Document& doc, const Connectivity& conn, int view);
std::string exportBomCsv(const Document& doc);
std::string exportNetlistXml(const Document& doc, const Connectivity& conn);
std::string exportSpice(const Document& doc, const Connectivity& conn);

// Store-only ZIP writer (no compression library needed).
std::string makeZip(const std::vector<std::pair<std::string, std::string>>& files);

// MCP server: see mcp.h

// ---- example projects ----
struct Example {
    std::string title;
    std::function<void(Document&)> build;
};
const std::vector<Example>& examples();
