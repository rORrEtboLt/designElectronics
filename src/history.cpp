// designElectronics — local version history and GitHub sync (see history.h).
#include "history.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <set>

#ifdef DE_HAVE_CURL
#include <curl/curl.h>
#endif
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

std::string g_root;

std::string prefDir() {
    char* pref = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string p = pref ? pref : "";
    SDL_free(pref);
    return p;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

std::string safeId(const std::string& s) {
    std::string o;
    for (char c : s) if (std::isalnum((unsigned char)c) || c == '-' || c == '_') o += c;
    return o;
}

}  // namespace

// ===========================================================================
// Local history
// ===========================================================================
namespace history {

void setRoot(const std::string& dir) { g_root = dir; }

std::string root() {
    if (g_root.empty()) {
        const char* env = SDL_getenv("DE_HISTORY_DIR");  // tests and portable installs
        g_root = env && *env ? env : joinPath(prefDir(), "history");
    }
    return g_root;
}

void ensureUid(Document& doc) {
    if (!doc.uid.empty()) return;
    std::random_device rd;
    std::mt19937_64 rng(((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)time(nullptr));
    char buf[24];
    snprintf(buf, sizeof buf, "%016llx", (unsigned long long)rng());
    doc.uid = buf;
}

static std::string dirFor(const std::string& uid) { return joinPath(root(), safeId(uid)); }

static std::vector<LocalVersion> loadIndex(const std::string& uid) {
    std::vector<LocalVersion> v;
    bool ok = false;
    std::string text = readTextFile(joinPath(dirFor(uid), "index.json"), &ok);
    if (!ok) return v;
    Json j = Json::parse(text);
    for (auto& e : j["versions"].items()) {
        LocalVersion lv;
        lv.n = e["n"].i();
        lv.time = (int64_t)e["time"].num();
        lv.message = e["message"].str();
        lv.summary = e["summary"].str();
        lv.file = e["file"].str();
        if (lv.n > 0 && !lv.file.empty()) v.push_back(lv);
    }
    return v;
}

static bool saveIndex(const std::string& uid, const std::vector<LocalVersion>& v) {
    Json arr = Json::array();
    for (auto& lv : v) {
        Json e = Json::object();
        e.set("n", lv.n);
        e.set("time", (double)lv.time);
        if (!lv.message.empty()) e.set("message", lv.message);
        e.set("summary", lv.summary);
        e.set("file", lv.file);
        arr.push(e);
    }
    Json j = Json::object();
    j.set("uid", uid);
    j.set("versions", arr);
    return writeTextFile(joinPath(dirFor(uid), "index.json"), j.dump(1));
}

static std::string trimTo(std::string s, size_t n) {
    if (s.size() <= n) return s;
    s.resize(n);
    return s + "…";
}

std::string describeChanges(const Document& a, const Document& b) {
    std::vector<std::string> items;
    std::map<int, const PartInst*> pa, pb;
    for (auto& p : a.parts) pa[p.id] = &p;
    for (auto& p : b.parts) pb[p.id] = &p;
    std::vector<std::string> added, removed;
    int moved = 0;
    for (auto& kv : pb) if (!pa.count(kv.first)) added.push_back(kv.second->label);
    for (auto& kv : pa) if (!pb.count(kv.first)) removed.push_back(kv.second->label);
    auto joinList = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size() && i < 5; i++) s += (i ? ", " : "") + v[i];
        if (v.size() > 5) s += " +" + std::to_string(v.size() - 5);
        return s;
    };
    if (!added.empty()) items.push_back("Added " + joinList(added));
    if (!removed.empty()) items.push_back("Removed " + joinList(removed));
    for (auto& kv : pb) {
        auto it = pa.find(kv.first);
        if (it == pa.end()) continue;
        const PartInst& x = *it->second;
        const PartInst& y = *kv.second;
        if (x.label != y.label) items.push_back("Renamed " + x.label + " → " + y.label);
        Props mx = mergedProps(x.type, x.props), my = mergedProps(y.type, y.props);
        for (auto& p : my) {
            auto q = mx.find(p.first);
            std::string before = q == mx.end() ? "" : q->second;
            if (before != p.second) items.push_back(y.label + " " + p.first + " " + trimTo(before, 20) + " → " + trimTo(p.second, 20));
        }
        for (int v = 0; v < NUM_VIEWS; v++) {
            const Xform &u = x.place[v], &w = y.place[v];
            if (u.pos.x != w.pos.x || u.pos.y != w.pos.y || u.rot != w.rot || u.flip != w.flip) { moved++; break; }
        }
    }
    if (moved) items.push_back("Moved " + std::to_string(moved) + (moved == 1 ? " part" : " parts"));
    std::set<int> wa, wb;
    for (auto& w : a.wires) wa.insert(w.id);
    for (auto& w : b.wires) wb.insert(w.id);
    int wAdd = 0, wDel = 0;
    for (int id : wb) if (!wa.count(id)) wAdd++;
    for (int id : wa) if (!wb.count(id)) wDel++;
    if (wAdd) items.push_back("+" + std::to_string(wAdd) + (wAdd == 1 ? " wire" : " wires"));
    if (wDel) items.push_back("−" + std::to_string(wDel) + (wDel == 1 ? " wire" : " wires"));
    if (a.title != b.title) items.push_back("Title “" + b.title + "”");
    if (a.notes.size() != b.notes.size()) items.push_back("Notes changed");
    if (items.empty()) return "Small changes (wire routing or settings)";
    std::string s;
    for (size_t i = 0; i < items.size() && i < 6; i++) s += (i ? " · " : "") + items[i];
    if (items.size() > 6) s += " · +" + std::to_string(items.size() - 6) + " more";
    return s;
}

LocalVersion record(const Document& doc, const std::string& data, const std::string& message, std::string* err) {
    LocalVersion out;
    if (doc.uid.empty()) { if (err) *err = "design has no uid"; return out; }
    std::string dir = dirFor(doc.uid);
    if (!SDL_CreateDirectory(dir.c_str())) { if (err) *err = std::string("cannot create ") + dir + ": " + SDL_GetError(); return out; }
    auto versions = loadIndex(doc.uid);
    std::string summary;
    if (!versions.empty()) {
        Document prev;
        std::string e;
        std::string prevText = readTextFile(joinPath(dir, versions.back().file));
        if (prev.deserialize(prevText, &e)) {
            if (prev.snapshot() == doc.snapshot() && message.empty()) return out;  // nothing changed
            summary = describeChanges(prev, doc);
        }
    } else {
        summary = "First version · " + std::to_string(doc.parts.size()) + (doc.parts.size() == 1 ? " part" : " parts");
    }
    out.n = versions.empty() ? 1 : versions.back().n + 1;
    out.time = (int64_t)time(nullptr);
    out.message = message;
    out.summary = summary.empty() ? "Saved" : summary;
    out.file = "v" + std::to_string(out.n) + ".dez";
    if (!writeTextFile(joinPath(dir, out.file), data)) { if (err) *err = "cannot write version file"; out.n = 0; return out; }
    versions.push_back(out);
    // keep the newest 200 versions per design
    while (versions.size() > 200) {
        SDL_RemovePath(joinPath(dir, versions.front().file).c_str());
        versions.erase(versions.begin());
    }
    if (!saveIndex(doc.uid, versions)) { if (err) *err = "cannot write history index"; }
    return out;
}

std::vector<LocalVersion> list(const std::string& uid) {
    auto v = loadIndex(uid);
    std::reverse(v.begin(), v.end());
    return v;
}

std::string read(const std::string& uid, const LocalVersion& v) {
    bool ok = false;
    std::string t = readTextFile(joinPath(dirFor(uid), v.file), &ok);
    return ok ? t : std::string();
}

}  // namespace history

// ===========================================================================
// GitHub
// ===========================================================================
namespace github {

namespace {

std::string g_apiBase;

std::string apiBase() {
    if (g_apiBase.empty()) {
        const char* env = SDL_getenv("DE_GITHUB_API");
        g_apiBase = env && *env ? env : "https://api.github.com";
    }
    return g_apiBase;
}

struct Response {
    long status = 0;
    std::string body, error;
};

#ifdef DE_HAVE_CURL
size_t onData(char* p, size_t sz, size_t n, void* ud) {
    ((std::string*)ud)->append(p, sz * n);
    return sz * n;
}

std::string escapeSegment(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}
#endif

std::string escapePath(const std::string& path) {
#ifdef DE_HAVE_CURL
    std::string o, seg;
    for (char c : path + "/") {
        if (c == '/') { if (!seg.empty()) { if (!o.empty()) o += "/"; o += escapeSegment(seg); } seg.clear(); }
        else seg += c;
    }
    return o;
#else
    return path;
#endif
}

Response request(const std::string& method, const std::string& pathAndQuery, const std::string& token, const std::string& body = "",
                 const std::string& accept = "application/vnd.github+json") {
    Response r;
#ifdef DE_HAVE_CURL
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* c = curl_easy_init();
    if (!c) { r.error = "could not start a network request"; return r; }
    std::string url = apiBase() + pathAndQuery;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, ("Accept: " + accept).c_str());
    h = curl_slist_append(h, "X-GitHub-Api-Version: 2022-11-28");
    h = curl_slist_append(h, "User-Agent: designElectronics");
    if (!token.empty()) h = curl_slist_append(h, ("Authorization: Bearer " + token).c_str());
    if (!body.empty()) h = curl_slist_append(h, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty() || method == "POST" || method == "PUT") {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    }
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) r.error = std::string("network error: ") + curl_easy_strerror(rc);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
#else
    (void)method; (void)pathAndQuery; (void)token; (void)body; (void)accept;
    r.error = "GitHub sync isn't available in this build (no HTTPS support).";
#endif
    return r;
}

// Turn a failed response into a sentence a user can act on.
std::string explain(const Response& r, const std::string& what) {
    if (!r.error.empty()) return r.error;
    std::string msg = Json::parse(r.body)["message"].str();
    switch (r.status) {
        case 401: return "GitHub rejected the token (401). It may have expired — create a new one in File ▸ GitHub Account…";
        case 403: return "GitHub refused " + what + " (403): " + msg + ". The token needs Contents: Read and write (and Administration to create repos).";
        case 404: return "Not found on GitHub (404) while trying to " + what + ". Check the repository name and that the token can access it.";
        case 409:
        case 422: return "GitHub couldn't " + what + " (" + std::to_string(r.status) + "): " + msg;
        default: return "GitHub error " + std::to_string(r.status) + " while trying to " + what + (msg.empty() ? "" : ": " + msg);
    }
}

std::string settingsPath() { return joinPath(prefDir(), "github.json"); }

}  // namespace

bool available() {
#ifdef DE_HAVE_CURL
    return true;
#else
    return false;
#endif
}

void setApiBase(const std::string& base) { g_apiBase = base; }

GitHubSettings loadSettings() {
    GitHubSettings s;
    bool ok = false;
    std::string text = readTextFile(settingsPath(), &ok);
    if (!ok) return s;
    Json j = Json::parse(text);
    s.token = j["token"].str();
    s.login = j["login"].str();
    s.defaultRepo = j["defaultRepo"].str(s.defaultRepo);
    s.autoPush = j["autoPush"].boolean(false);
    return s;
}

bool saveSettings(const GitHubSettings& s) {
    Json j = Json::object();
    j.set("token", s.token);
    j.set("login", s.login);
    j.set("defaultRepo", s.defaultRepo);
    j.set("autoPush", s.autoPush);
    std::string path = settingsPath();
    bool ok = writeTextFile(path, j.dump(1));
#ifndef _WIN32
    if (ok) chmod(path.c_str(), 0600);  // the token is a secret: owner-only
#endif
    return ok;
}

bool whoAmI(const std::string& token, std::string& login, std::string& err) {
    Response r = request("GET", "/user", token);
    if (r.status != 200) { err = explain(r, "check the token"); return false; }
    login = Json::parse(r.body)["login"].str();
    if (login.empty()) { err = "Unexpected reply from GitHub."; return false; }
    return true;
}

bool ensureRepo(const GitHubSettings& s, const std::string& repo, std::string& err) {
    Response r = request("GET", "/repos/" + escapePath(repo), s.token);
    if (r.status == 200) return true;
    if (r.status != 404) { err = explain(r, "open " + repo); return false; }
    size_t slash = repo.find('/');
    std::string owner = repo.substr(0, slash), name = slash == std::string::npos ? repo : repo.substr(slash + 1);
    if (!s.login.empty() && owner != s.login) {
        err = "Repository " + repo + " doesn't exist. I can only create repositories under your own account (" + s.login + ").";
        return false;
    }
    Json body = Json::object();
    body.set("name", name);
    body.set("private", true);
    body.set("auto_init", true);
    body.set("description", "Electronics designs made with designElectronics");
    Response c = request("POST", "/user/repos", s.token, body.dump(-1));
    if (c.status != 201) { err = explain(c, "create the private repository " + repo); return false; }
    return true;
}

bool commitFile(const GitHubSettings& s, const GitLink& link, const std::string& content, const std::string& message, GitCommit& out,
                std::string& err) {
    if (!ensureRepo(s, link.repo, err)) return false;
    std::string base = "/repos/" + escapePath(link.repo) + "/contents/" + escapePath(link.path);
    for (int attempt = 0; attempt < 2; attempt++) {
        // the current blob sha is required to update an existing file
        std::string sha;
        Response g = request("GET", base + (link.branch.empty() ? "" : "?ref=" + escapePath(link.branch)), s.token);
        if (g.status == 200) sha = Json::parse(g.body)["sha"].str();
        else if (g.status != 404) { err = explain(g, "read " + link.path); return false; }
        Json body = Json::object();
        body.set("message", message.empty() ? std::string("Update design") : message);
        body.set("content", base64Encode(content));
        if (!sha.empty()) body.set("sha", sha);
        if (!link.branch.empty()) body.set("branch", link.branch);
        Response p = request("PUT", base, s.token, body.dump(-1));
        if (p.status == 200 || p.status == 201) {
            Json j = Json::parse(p.body);
            out.sha = j["commit"]["sha"].str();
            out.url = j["commit"]["html_url"].str();
            out.message = j["commit"]["message"].str();
            out.date = j["commit"]["author"]["date"].str();
            return true;
        }
        if ((p.status == 409 || p.status == 422) && attempt == 0) continue;  // someone pushed in between: retry once
        err = explain(p, "commit " + link.path);
        return false;
    }
    return false;
}

bool listCommits(const GitHubSettings& s, const GitLink& link, std::vector<GitCommit>& out, std::string& err) {
    std::string q = "/repos/" + escapePath(link.repo) + "/commits?per_page=50&path=" + escapePath(link.path);
    if (!link.branch.empty()) q += "&sha=" + escapePath(link.branch);
    Response r = request("GET", q, s.token);
    if (r.status == 404 || r.status == 409) return true;  // repo or branch not created yet: no commits
    if (r.status != 200) { err = explain(r, "list the history"); return false; }
    out.clear();
    Json arr = Json::parse(r.body);  // keep it alive for the loop
    for (auto& c : arr.items()) {
        GitCommit gc;
        gc.sha = c["sha"].str();
        gc.message = c["commit"]["message"].str();
        gc.date = c["commit"]["author"]["date"].str();
        gc.author = c["commit"]["author"]["name"].str();
        gc.url = c["html_url"].str();
        out.push_back(gc);
    }
    return true;
}

bool fetchFile(const GitHubSettings& s, const GitLink& link, const std::string& sha, std::string& out, std::string& err) {
    Response r = request("GET", "/repos/" + escapePath(link.repo) + "/contents/" + escapePath(link.path) + "?ref=" + escapePath(sha), s.token,
                         "", "application/vnd.github.raw+json");
    if (r.status != 200) { err = explain(r, "download that version"); return false; }
    out = r.body;
    return true;
}

GitLink defaultLink(const GitHubSettings& s, const Document& doc) {
    std::string slug;
    for (char c : doc.title) {
        if (std::isalnum((unsigned char)c)) slug += (char)std::tolower((unsigned char)c);
        else if (!slug.empty() && slug.back() != '-') slug += '-';
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "design";
    // one folder per design; the uid suffix keeps two designs with the same title apart
    std::string folder = slug + "-" + doc.uid.substr(0, std::min<size_t>(6, doc.uid.size()));
    GitLink l;
    l.repo = s.login + "/" + s.defaultRepo;
    l.path = folder + "/" + slug + kFileExt;
    return l;
}

void runAsync(std::function<void()> work, std::function<void()> done, std::function<void(std::function<void()>)> post) {
    struct Job {
        std::function<void()> work, done;
        std::function<void(std::function<void()>)> post;
    };
    auto* job = new Job{std::move(work), std::move(done), std::move(post)};
    SDL_Thread* t = SDL_CreateThread(
        [](void* ud) -> int {
            auto* j = (Job*)ud;
            j->work();
            j->post(j->done);
            delete j;
            return 0;
        },
        "github", job);
    if (t) SDL_DetachThread(t);
    else { job->work(); job->post(job->done); delete job; }
}

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        for (int k = 3; k >= 0; k--) out += kB64[(v >> (6 * k)) & 63];
    }
    if (i < in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        if (i + 1 < in.size()) v |= (uint8_t)in[i + 1] << 8;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string base64Decode(const std::string& in) {
    std::string out;
    uint32_t v = 0;
    int bits = 0;
    for (char c : in) {
        const char* p = std::strchr(kB64, c);
        if (!p || !c) continue;  // skips '=', newlines
        v = (v << 6) | (uint32_t)(p - kB64);
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((v >> bits) & 0xFF); }
    }
    return out;
}

}  // namespace github
