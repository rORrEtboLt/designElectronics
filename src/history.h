// designElectronics — version history.
//
// Local history: every save of a design stores a full copy in the app's data
// folder (works offline, on every platform), keyed by the design's uid so it
// follows the design when the file is renamed or moved.
//
// GitHub sync: optional. A design can be committed to a GitHub repository on
// demand (or automatically on save); its commits are listed as versions too.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "doc.h"

// ---------------------------------------------------------------------------
// Local history
// ---------------------------------------------------------------------------
struct LocalVersion {
    int n = 0;                // version number, 1 = first save
    int64_t time = 0;         // unix seconds
    std::string message;      // optional, from the user
    std::string summary;      // what changed since the previous version
    std::string file;         // snapshot file name inside the history folder
};

namespace history {

// Root folder for all local histories (default: <pref path>/history). Tests override it.
void setRoot(const std::string& dir);
std::string root();

// Give the design a uid if it has none (call before saving).
void ensureUid(Document& doc);

// Store a version of `doc` (whose serialized text is `data`). Returns the new
// version, or n = 0 when nothing changed since the last version.
LocalVersion record(const Document& doc, const std::string& data, const std::string& message, std::string* err = nullptr);

// Versions of a design, newest first.
std::vector<LocalVersion> list(const std::string& uid);

// Full .dez text of a stored version ("" if missing).
std::string read(const std::string& uid, const LocalVersion& v);

// Human-readable summary of what changed from `a` to `b` ("Added LED2 · R1 resistance 220Ω → 330Ω").
std::string describeChanges(const Document& a, const Document& b);

}  // namespace history

// ---------------------------------------------------------------------------
// GitHub
// ---------------------------------------------------------------------------
struct GitHubSettings {
    std::string token;          // personal access token (until OAuth device sign-in lands)
    std::string login;          // GitHub user the token belongs to
    std::string defaultRepo = "electronics-designs";  // repo name for designs without their own link
    bool autoPush = false;      // commit to GitHub on every save
};

struct GitCommit {
    std::string sha, message, date, author, url;
};

namespace github {

bool available();                     // false when this build has no HTTPS support
void setApiBase(const std::string& base);  // default https://api.github.com (tests use a local mock)

GitHubSettings loadSettings();
bool saveSettings(const GitHubSettings& s);

// Blocking calls — run them on a worker thread (see runAsync).
bool whoAmI(const std::string& token, std::string& login, std::string& err);
// Creates the repo (private) under the token's user if it doesn't exist.
bool ensureRepo(const GitHubSettings& s, const std::string& repo, std::string& err);
bool commitFile(const GitHubSettings& s, const GitLink& link, const std::string& content, const std::string& message,
                GitCommit& out, std::string& err);
bool listCommits(const GitHubSettings& s, const GitLink& link, std::vector<GitCommit>& out, std::string& err);
bool fetchFile(const GitHubSettings& s, const GitLink& link, const std::string& sha, std::string& out, std::string& err);

// Default link for a design that isn't linked yet: <login>/<defaultRepo>, folder per design.
GitLink defaultLink(const GitHubSettings& s, const Document& doc);

// Run `work` on a background thread, then `done` via `post` (which must hand it to the main thread).
void runAsync(std::function<void()> work, std::function<void()> done, std::function<void(std::function<void()>)> post);

std::string base64Encode(const std::string& in);
std::string base64Decode(const std::string& in);

}  // namespace github
