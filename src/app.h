// designElectronics — application state, interaction and UI composition.
#pragma once
#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "doc.h"
#include "gfx.h"
#include "history.h"
#include "tools.h"
#include "ui.h"

// Per-view drawing layers (toggled in the Layers panel; visual only, not saved).
enum CanvasLayer { LY_PARTS, LY_WIRES, LY_LABELS, LY_NOTES, LY_RATSNEST, LY_BOARD, LY_COPPER_BOTTOM, LY_COPPER_TOP, LY_PADS, LY_SILK, LY_COUNT };

class App;
// web.cpp — browser glue (no-ops on native builds)
void webRegisterApp(App* app);
void webSaveAndDownload(const std::string& name, const std::function<void(const std::string&)>& write);
void webPickFile(const char* accept);

struct Camera {
    Vec2 pan;          // screen position of world origin (pixels)
    float zoom = 0.15f;  // pixels per mil
    bool valid = false;
};

enum DragMode {
    DRAG_NONE,
    DRAG_PENDING,      // pressed, waiting to see if this becomes a drag or a click
    DRAG_PAN,
    DRAG_MOVE,
    DRAG_WIRE_NEW,
    DRAG_WIRE_END,
    DRAG_WIRE_BEND,
    DRAG_RUBBER,
    DRAG_NOTE_RESIZE,
    DRAG_BIN,
};

// What is under the pointer in the canvas.
struct Hit {
    enum Kind { NONE, PIN, PART, WIRE_END, WIRE_BEND, WIRE_SEG, NOTE, NOTE_CORNER, RATSNEST } kind = NONE;
    int part = -1, pin = -1, wire = -1, index = -1, note = -1;
    RatLine rat;
};

class App {
public:
    bool init(int argc, char** argv);
    SDL_AppResult event(const SDL_Event& e);
    SDL_AppResult iterate();
    void shutdown();

    // ---- state ----
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    Gfx g;
    UI ui;
    Input in;
    Document doc;
    Connectivity conn;
    int connRev = -1;

    int view = BB;
    Camera cam[NUM_VIEWS];
    std::set<int> selParts, selWires, selNotes;

    // interaction
    DragMode drag = DRAG_NONE;
    Hit pressHit;
    Vec2 pressWorld;
    bool canvasOwnsPointer = false;
    std::map<int, Vec2> moveStart;                 // part id -> start pos
    std::map<int, std::vector<Vec2>> moveWireStart;  // wire id -> start points
    std::map<int, Vec2> moveNoteStart;
    int anchorPart = -1;
    int dragWire = -1, dragWireEnd = -1, dragBend = -1;
    Vec2 rubberA, rubberB;
    Hit hover;
    bool longPressFired = false;
    int binDrag = -1;                // library index being dragged from the bin
    std::map<SDL_FingerID, Vec2> fingers;
    bool spaceDown = false;
    Vec2 lastMouse;
    bool prevMiddle = false;
    int wireTargetPart = -1, wireTargetPin = -1;

    // undo
    std::vector<std::string> undoStack, redoStack;
    std::string editSnapshot;
    bool editOpen = false;

    // file
    std::string filePath;
    int savedRevision = 0;
    Uint64 lastAutosave = 0;
    int autosaveRevision = -1;

    // tools
    bool simOn = false;
    SimResult sim;
    Simulator simulator;
    Uint64 lastSimTicks = 0;
    int simRev = -1;
    std::set<int> simPressed;
    std::set<int> simPressedLast;
    std::vector<DrcIssue> drcIssues;
    int drcRev = -1;
    int activeLayer = TOP;
    bool showGrid = true;
    bool snapToGrid = true;
    bool showRatsnest = true;
    bool showPartLabels = true;
    bool layerOff[NUM_VIEWS][LY_COUNT] = {};
    bool layerShown(int v, int l) const { return !layerOff[v][l]; }
    bool partShown(const PartInst& p, int v) const {
        if (!p.def || !p.inView(v) || p.hiddenIn(v)) return false;
        if (v == PCB) return p.def->isBoard ? layerShown(PCB, LY_BOARD) : (layerShown(PCB, LY_PADS) || layerShown(PCB, LY_SILK));
        return layerShown(v, LY_PARTS);
    }
    bool wireShown(const Wire& w) const {
        if (w.view == PCB) return layerShown(PCB, w.layer == TOP ? LY_COPPER_TOP : LY_COPPER_BOTTOM);
        return layerShown(w.view, LY_WIRES);
    }
    // layers panel (panels_layers.cpp)
    int rightTab = 0;            // 0 parts bin, 1 layers
    bool drawerLayers = false;   // compact layout
    std::string layerFilter;
    std::set<int> layerCollapsed;  // collapsed group ids (breadboard part ids; -1 wires, -2 notes)
    Uint64 lastLayerClick = 0;
    int lastLayerClickPart = -1;

    // layout
    float userScale = 1.0f;
    bool compact = false;
    bool drawerParts = false, drawerInspector = false;
    std::string search;
    int category = -1;
    Rect rTop, rCanvas, rRight, rBottom, rBin, rInspector;
    int winW = 0, winH = 0;

    std::mutex pendingMutex;
    std::vector<std::function<void()>> pending;  // work queued from other threads (file dialogs)
    std::vector<LibEntry> customEntries;     // imported / embedded custom parts shown in the bin
    bool quitNow = false;
    std::string capturePath;   // --screenshot: save the next frame before presenting
    bool captureOk = false;

    // ---- app.cpp ----
    void layout();
    void ensureConn();
    bool dirty() const { return doc.revision != savedRevision; }
    void runOnMain(std::function<void()> f);
    void checkpoint();
    void beginEdit();
    void endEdit();
    void undo();
    void redo();
    void changed();
    void pruneSelection();

    void newDoc();
    void confirmDiscard(std::function<void()> then);
    bool loadFile(const std::string& path);
    bool saveFile(const std::string& path, const std::string& versionMessage = "");
    void save();
    void saveAs();
    void openDialog();
    void exportFile(const std::string& kind);
    void importPartDialog();
    void loadExample(size_t i);
    void autosave(bool force);
    std::string autosavePath() const;
    void showAbout();
    void showShortcuts();

    void deleteSelection();
    void rotateSelection(int dir);
    void flipSelection();
    void copySelection(bool cut);
    void paste();
    void duplicate();
    void selectAll();
    void addPart(const LibEntry& e, Vec2 world);
    void addNote();
    void setPartProp(int partId, const std::string& key, const std::string& value);
    void bringToFront(bool front);
    void runAutoroute();
    void runDrc();
    void toggleSim();
    void setView(int v);
    void handleShortcuts();

    // ---- panels_code.cpp: a board's program (Arduino sketch) ----
    bool codeOpen = false;
    int codePart = -1;
    Rect rCode;
    CodeEditorState codeState;
    bool codeEditing = false;
    Uint64 lastCodeEdit = 0;
    int codeCheckRev = -1, codeCheckLine = 0;
    std::string codeCheckMsg;
    std::map<int, std::string> serialLog;  // per board
    std::string serialSend;
    void openCode(int partId);
    void drawCodePanel(Rect r);
    std::string defaultSketch(const PartInst& p);
    void exportSketch(int partId);
    void showSaveDialog(const std::string& name, const SDL_DialogFileFilter* filters, int n, std::function<void(const std::string&)> onPath);
    // ---- upload.cpp: real boards via arduino-cli ----
    struct UploadJob;
    std::shared_ptr<UploadJob> uploadJob;
    int uploadPart = -1;
    std::string arduinoCli, uploadFqbn, uploadPort, uploadStatus, uploadResult;
    bool uploadResultOk = true;
    std::vector<std::string> uploadPorts;
    void openUploadSheet(int partId);
    void runCli(const std::vector<std::string>& args, const std::string& what, std::function<void(int, const std::string&)> done);
    void refreshPorts();
    void installCore();
    void startUpload(bool compileOnly = false);

    // ---- panels_video.cpp: Export ▸ Video or GIF ----
    struct VideoJob;
    std::shared_ptr<VideoJob> videoJob;
    int videoPreset = 0, videoView = 0, videoSeconds = 10;
    bool videoGif = false, videoSimulate = true, videoWatermark = true;
    std::string videoTitle, lastVideoError;
    void openVideoSheet();
    void startVideo(const std::string& path);
    bool renderVideoFrame();
    void stepVideoExport();
    ~App();

    // ---- agent.cpp + panels_ai.cpp: the AI tab (Claude Code & other CLI agents) ----
    struct ChatMsg {
        enum Role { R_USER, R_ASSISTANT, R_TOOL, R_TOOL_ERROR, R_ERROR, R_INFO } role;
        std::string text;
        bool streaming = false;
    };
    struct AgentState;
    std::shared_ptr<AgentState> agent;
    std::string agentKind = "claude", agentCustomCommand, agentInput;
    bool drawerAI = false;
    size_t agentSeen = 0;  // messages already scrolled to
    bool agentWasRunning = false;
    int agentStartRev = 0;
    bool agentsSupported() const;
    void detectAgents();
    void ensureAgentBridge();
    void startAgent(const std::string& prompt);
    void stopAgent();
    void newAgentChat();
    std::vector<ChatMsg> agentMessages(bool& running, bool& detected, std::map<std::string, std::string>& paths);
    std::string standaloneMcpConfig() const;
    void drawAIPanel(Rect r);
    void drawRightTabs(Rect r, float y);

    // ---- web.cpp: browser build + embeddable viewer ----
    bool embedMode = false;      // <iframe> viewer: canvas + small bar only
    bool readOnly = false;       // no editing (pan, zoom, simulate, press buttons)
    std::string editorUrl;       // "Open in designElectronics" target (set by the web page)
    void drawEmbedBar();
    void handleViewerKeys();
    void openEmbedSheet();       // Share ▸ Embed in a website…
    std::string shareFragment(); // "#d=…" with the design packed into the link

    // ---- app_history.cpp: version history + GitHub ----
    GitHubSettings gh;
    bool ghBusy = false;
    std::string ghStatus, ghCommitsErr, lastCommitUrl;
    std::vector<GitCommit> ghCommits;
    bool ghCommitsLoaded = false;
    int historyTab = 0;
    std::string commitMsg, commitRepo, commitPath, tokenInput, repoInput;
    bool githubConnected() const;
    void loadGitHubSettings();
    void recordVersion(const std::string& data, const std::string& message);
    void pushToGitHub(const std::string& message);
    void refreshGitHubHistory();
    void restoreVersionText(const std::string& text, const std::string& label);
    void openVersionCopy(const std::string& text, const std::string& label);
    void fetchGitHubVersion(const GitCommit& c, bool asCopy);
    void openHistory();
    void openCommitSheet();
    void openGitHubAccount();

    // ---- canvas.cpp ----
    Vec2 toWorld(Vec2 s) const { return (s - cam[view].pan) / cam[view].zoom; }
    Vec2 toScreen(Vec2 w) const { return cam[view].pan + w * cam[view].zoom; }
    float gridSize(int v) const { return v == PCB ? 50.f : 100.f; }
    void fitView(int v, Rect viewport);
    void zoomAt(float factor, Vec2 screen);
    Hit hitTest(Vec2 screen);
    bool pinAt(Vec2 screen, int& part, int& pin, int excludePart, int excludePin);
    void startDrag();
    void finishPointer();
    void drawPrims(const PartInst& p, int v, const Camera& c, bool forIcon = false);
    void canvasInput();
    void drawCanvas(int v, const Camera& c, Rect viewport, bool exporting);
    void drawPartIcon(const PartDefPtr& def, Rect r);
    std::vector<MenuItem> contextMenu(const Hit& h);
    bool renderViewPng(int v, const std::string& path, std::string* msg);

    // ---- panels.cpp ----
    void drawTopBar();
    void drawPartsBin(Rect r);
    void drawInspector(Rect r);
    void drawLayers(Rect r);
    void setPartHidden(int partId, int v, bool hidden);
    void setPartLocked(int partId, bool locked);
    void zoomToPart(int partId);
    void drawToolbar();
    void drawCanvasOverlay();
    std::vector<MenuItem> fileMenu();
    std::vector<MenuItem> editMenu();
    std::vector<MenuItem> partMenu();
    std::vector<MenuItem> viewMenu();
    std::vector<MenuItem> routingMenu();
    std::vector<MenuItem> helpMenu();
    std::vector<MenuItem> allMenus();
};
