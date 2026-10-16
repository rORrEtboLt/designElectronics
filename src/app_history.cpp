// designElectronics — version history and GitHub screens (File ▸ Version History,
// Commit to GitHub, GitHub Account). The storage and network code is in history.cpp.
#include <cctype>
#include <ctime>
#include <memory>

#include "app.h"
#include "history.h"

namespace {

std::string localTime(int64_t t) {
    time_t tt = (time_t)t;
    char buf[64];
    strftime(buf, sizeof buf, "%d %b %Y, %H:%M", localtime(&tt));
    return buf;
}

std::string isoShort(const std::string& iso) {
    // "2026-10-16T14:02:11Z" -> "2026-10-16 14:02 UTC"
    if (iso.size() < 16) return iso;
    return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
}

std::string firstLine(const std::string& s) { return s.substr(0, s.find('\n')); }

}  // namespace

bool App::githubConnected() const { return github::available() && !gh.token.empty() && !gh.login.empty(); }

void App::loadGitHubSettings() { gh = github::loadSettings(); }

// Called by saveFile after the file is written.
void App::recordVersion(const std::string& data, const std::string& message) {
    std::string err;
    LocalVersion v = history::record(doc, data, message, &err);
    if (!err.empty()) SDL_Log("version history: %s", err.c_str());
    if (gh.autoPush && githubConnected() && v.n > 0) {
        std::string msg = message.empty() ? "Version " + std::to_string(v.n) + ": " + v.summary : message;
        pushToGitHub(msg);
    }
}

void App::pushToGitHub(const std::string& message) {
    if (!githubConnected()) { ui.toast("Connect GitHub first (File ▸ GitHub Account…)"); return; }
    if (ghBusy) { ui.toast("Still talking to GitHub…"); return; }
    history::ensureUid(doc);
    if (doc.git.repo.empty()) doc.git = github::defaultLink(gh, doc);
    std::string data = doc.serialize(true);
    GitLink link = doc.git;
    GitHubSettings s = gh;
    ghBusy = true;
    ghStatus = "Pushing to " + link.repo + "…";
    auto result = std::make_shared<std::pair<GitCommit, std::string>>();
    github::runAsync(
        [=]() {
            GitCommit c;
            std::string err;
            if (github::commitFile(s, link, data, message, c, err)) result->first = c;
            else result->second = err;
        },
        [this, result, link]() {
            ghBusy = false;
            if (!result->second.empty()) {
                ghStatus = result->second;
                ui.dialog("Couldn't push to GitHub", {result->second}, {});
                return;
            }
            ghStatus = "Committed " + result->first.sha.substr(0, 7) + " to " + link.repo;
            lastCommitUrl = result->first.url;
            ghCommitsLoaded = false;  // history list is stale now
            ui.toast("Committed to GitHub: " + link.repo + "/" + link.path);
        },
        [this](std::function<void()> f) { runOnMain(std::move(f)); });
}

void App::refreshGitHubHistory() {
    if (!githubConnected() || doc.git.repo.empty() || ghBusy) return;
    ghBusy = true;
    ghStatus = "Loading history from GitHub…";
    GitHubSettings s = gh;
    GitLink link = doc.git;
    auto result = std::make_shared<std::pair<std::vector<GitCommit>, std::string>>();
    github::runAsync([=]() { github::listCommits(s, link, result->first, result->second); },
                     [this, result]() {
                         ghBusy = false;
                         ghCommitsLoaded = true;
                         ghCommits = result->first;
                         ghCommitsErr = result->second;
                         ghStatus = ghCommitsErr.empty() ? std::to_string(ghCommits.size()) + " versions on GitHub" : ghCommitsErr;
                     },
                     [this](std::function<void()> f) { runOnMain(std::move(f)); });
}

// Replace the open design's contents with an older version (undoable; save to keep it).
void App::restoreVersionText(const std::string& text, const std::string& label) {
    Document d;
    std::string err;
    if (!d.deserialize(text, &err)) { ui.dialog("Couldn't restore that version", {err}, {}); return; }
    checkpoint();
    std::string uid = doc.uid;
    GitLink link = doc.git;
    int rev = doc.revision;
    doc = std::move(d);
    doc.uid = uid;
    doc.git = link;
    doc.revision = rev + 1;
    pruneSelection();
    drcIssues.clear();
    ui.closeSheet();
    ui.toast("Restored " + label + ". Save to keep it — or Undo to go back.");
}

void App::openVersionCopy(const std::string& text, const std::string& label) {
    confirmDiscard([this, text, label]() {
        Document d;
        std::string err;
        if (!d.deserialize(text, &err)) { ui.dialog("Couldn't open that version", {err}, {}); return; }
        doc = std::move(d);
        doc.uid.clear();          // a copy is a new design with its own history
        doc.git = GitLink();
        doc.title += " (" + label + ")";
        doc.touch();
        filePath.clear();
        savedRevision = -1;
        undoStack.clear();
        redoStack.clear();
        selParts.clear(); selWires.clear(); selNotes.clear();
        for (auto& c : cam) c.valid = false;
        ui.closeSheet();
        ui.toast("Opened " + label + " as a new, unsaved design.");
    });
}

void App::fetchGitHubVersion(const GitCommit& c, bool asCopy) {
    if (ghBusy) return;
    ghBusy = true;
    ghStatus = "Downloading " + c.sha.substr(0, 7) + "…";
    GitHubSettings s = gh;
    GitLink link = doc.git;
    std::string sha = c.sha;
    auto result = std::make_shared<std::pair<std::string, std::string>>();
    github::runAsync([=]() { github::fetchFile(s, link, sha, result->first, result->second); },
                     [this, result, sha, asCopy]() {
                         ghBusy = false;
                         if (!result->second.empty()) { ghStatus = result->second; ui.dialog("Couldn't download that version", {result->second}, {}); return; }
                         ghStatus.clear();
                         std::string label = "GitHub " + sha.substr(0, 7);
                         if (asCopy) openVersionCopy(result->first, label);
                         else restoreVersionText(result->first, label);
                     },
                     [this](std::function<void()> f) { runOnMain(std::move(f)); });
}

// ---------------------------------------------------------------------------
// Version History sheet
// ---------------------------------------------------------------------------
void App::openHistory() {
    if (githubConnected() && !doc.git.repo.empty()) refreshGitHubHistory();
    ui.openSheet("history", "Version History", 680, 600, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float y = body.y;
        // tabs
        auto local = doc.uid.empty() ? std::vector<LocalVersion>() : history::list(doc.uid);
        std::string t0 = "On this device (" + std::to_string(local.size()) + ")";
        float w0 = g.textWidth(t0, 14 * s) + 28 * s, w1 = g.textWidth("GitHub", 14 * s) + 28 * s;
        if (ui.button("hist-tab0", Rect(body.x, y, w0, 32 * s), t0, BTN_TAB, historyTab == 0)) historyTab = 0;
        if (ui.button("hist-tab1", Rect(body.x + w0 + 6 * s, y, w1, 32 * s), "GitHub", BTN_TAB, historyTab == 1)) {
            historyTab = 1;
            if (!ghCommitsLoaded) refreshGitHubHistory();
        }
        y += 44 * s;
        Rect list(body.x, y, body.w, body.b() - y - 8 * s);
        const float rowH = 64 * s;

        auto row = [&](float ry, const std::string& id, const std::string& l1, const std::string& l2, bool current,
                       std::function<void()> restore, std::function<void()> copy, std::function<void()> link) {
            Rect r(list.x, ry, list.w, rowH - 6 * s);
            g.fillRoundRect(r, 8 * s, current ? rgba(232, 242, 255) : rgba(255, 255, 255));
            g.strokeRoundRect(r, 8 * s, std::max(1.f, s), th.border);
            float bx = r.r() - 10 * s;
            auto btn = [&](const std::string& bid, const std::string& label, std::function<void()> fn, bool primary) {
                if (!fn) return;
                float bw = g.textWidth(label, 13 * s) + 22 * s;
                bx -= bw;
                if (ui.button(id + bid, Rect(bx, r.center().y - 15 * s, bw, 30 * s), label, primary ? BTN_PRIMARY : BTN_NORMAL, false, !ghBusy)) fn();
                bx -= 8 * s;
            };
            btn("-copy", "Open copy", copy, false);
            btn("-restore", "Restore", restore, true);
            btn("-link", "View", link, false);
            float tw = bx - r.x - 14 * s;
            auto clip = [&](std::string t, float px) {
                while (t.size() > 3 && g.textWidth(t, px) > tw) { t.resize(t.size() - 4); t += "…"; }
                return t;
            };
            g.text({r.x + 12 * s, r.y + 20 * s}, clip(l1, 14 * s), 14 * s, th.text, 0);
            g.text({r.x + 12 * s, r.y + 41 * s}, clip(l2, 12.5f * s), 12.5f * s, th.textDim, 0);
        };

        if (historyTab == 0) {
            if (local.empty()) {
                ui.wrappedText(list, "No versions yet. Every time you save this design, a version is kept here on this device — "
                                     "you can look back at any of them and restore it.", th.textDim, 14 * s);
                return;
            }
            float oy = ui.beginScroll("hist-local", list, (float)local.size() * rowH);
            for (size_t i = 0; i < local.size(); i++) {
                const LocalVersion& v = local[i];
                float ry = list.y + oy + (float)i * rowH;
                if (ry + rowH < list.y || ry > list.b()) continue;
                std::string l1 = "Version " + std::to_string(v.n) + " · " + localTime(v.time) + (v.message.empty() ? "" : " · " + v.message);
                std::string uid = doc.uid;
                std::string label = "version " + std::to_string(v.n);
                row(ry, "lv" + std::to_string(v.n), l1, v.summary, i == 0 && !dirty(),
                    [this, uid, v, label]() { restoreVersionText(history::read(uid, v), label); },
                    [this, uid, v, label]() { openVersionCopy(history::read(uid, v), label); }, nullptr);
            }
            ui.endScroll();
            return;
        }

        // GitHub tab
        if (!github::available()) {
            ui.wrappedText(list, "GitHub sync isn't available in this build of the app yet. Versions on this device still work.", th.textDim, 14 * s);
            return;
        }
        if (!githubConnected()) {
            float h = ui.wrappedText(list, "Connect your GitHub account to keep every committed version of your designs online, "
                                           "in a private repository you own — and open them from any device.", th.textDim, 14 * s);
            if (ui.button("hist-connect", Rect(list.x, list.y + h + 14 * s, 180 * s, 36 * s), "Connect GitHub…", BTN_PRIMARY)) openGitHubAccount();
            return;
        }
        if (doc.git.repo.empty()) {
            float h = ui.wrappedText(list, "This design isn't on GitHub yet. Commit it to start its online history.", th.textDim, 14 * s);
            if (ui.button("hist-commit", Rect(list.x, list.y + h + 14 * s, 190 * s, 36 * s), "Commit to GitHub…", BTN_PRIMARY)) openCommitSheet();
            return;
        }
        // header: where it lives + refresh
        std::string where = doc.git.repo + " / " + doc.git.path;
        g.text({list.x, list.y + 10 * s}, where, 13 * s, th.textDim, 0);
        if (ui.button("hist-refresh", Rect(list.r() - 90 * s, list.y - 6 * s, 90 * s, 30 * s), ghBusy ? "Loading…" : "Refresh", BTN_NORMAL, false, !ghBusy))
            refreshGitHubHistory();
        Rect l2(list.x, list.y + 32 * s, list.w, list.h - 32 * s);
        if (!ghStatus.empty() && (ghBusy || !ghCommitsErr.empty())) g.text({l2.x, l2.b() + 2 * s}, ghStatus, 12 * s, ghCommitsErr.empty() ? th.textDim : th.danger, 0);
        if (ghCommitsLoaded && ghCommits.empty() && ghCommitsErr.empty()) {
            ui.wrappedText(l2, "No commits of this design yet. Use File ▸ Commit to GitHub….", th.textDim, 14 * s);
            return;
        }
        float oy = ui.beginScroll("hist-gh", l2, (float)ghCommits.size() * rowH);
        for (size_t i = 0; i < ghCommits.size(); i++) {
            GitCommit c = ghCommits[i];
            float ry = l2.y + oy + (float)i * rowH;
            if (ry + rowH < l2.y || ry > l2.b()) continue;
            row(ry, "gh" + c.sha, firstLine(c.message), isoShort(c.date) + " · " + c.author + " · " + c.sha.substr(0, 7), false,
                [this, c]() { fetchGitHubVersion(c, false); }, [this, c]() { fetchGitHubVersion(c, true); },
                c.url.empty() ? std::function<void()>() : [c]() { SDL_OpenURL(c.url.c_str()); });
        }
        ui.endScroll();
    });
}

// ---------------------------------------------------------------------------
// Commit to GitHub sheet
// ---------------------------------------------------------------------------
void App::openCommitSheet() {
    if (!github::available()) { ui.dialog("GitHub sync unavailable", {"This build of the app can't reach GitHub yet. Versions on this device still work (File ▸ Version History)."}, {}); return; }
    if (!githubConnected()) { openGitHubAccount(); return; }
    history::ensureUid(doc);
    GitLink link = doc.git.repo.empty() ? github::defaultLink(gh, doc) : doc.git;
    commitRepo = link.repo;
    commitPath = link.path;
    commitMsg.clear();
    ui.openSheet("commit", "Commit to GitHub", 600, 470, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float y = body.y;
        auto field = [&](const std::string& id, const std::string& label, std::string& v, const std::string& ph) {
            g.text({body.x, y + 8 * s}, label, 13 * s, th.textDim, 0);
            ui.textField(id, Rect(body.x, y + 20 * s, body.w, 34 * s), v, ph, true);
            y += 66 * s;
        };
        field("commit-msg", "What changed?", commitMsg, "e.g. Added a status LED on GPIO2");
        field("commit-repo", "Repository (owner/name)", commitRepo, gh.login + "/" + gh.defaultRepo);
        field("commit-path", "File in the repository", commitPath, "my-design/my-design.dez");
        float h = ui.wrappedText(Rect(body.x, y, body.w, 0),
                                 "Each design is kept in its own folder. To keep this design completely separate, give it its own repository — "
                                 "it's created as a private repository if it doesn't exist." +
                                     std::string(dirty() && !filePath.empty() ? " Unsaved changes are saved first." : ""),
                                 th.textDim, 13 * s);
        y += h + 10 * s;
        if (!ghStatus.empty()) {
            g.text({body.x, y + 8 * s}, ghStatus, 13 * s, ghBusy ? th.textDim : (ghStatus.rfind("Committed", 0) == 0 ? th.ok : th.danger), 0);
            if (!lastCommitUrl.empty() && !ghBusy && ghStatus.rfind("Committed", 0) == 0 &&
                ui.button("commit-view", Rect(body.r() - 140 * s, y - 6 * s, 140 * s, 28 * s), "View on GitHub", BTN_FLAT))
                SDL_OpenURL(lastCommitUrl.c_str());
        }
        float by = body.b() - 38 * s;
        bool ok = !ghBusy && commitRepo.find('/') != std::string::npos && !commitPath.empty();
        if (ui.button("commit-go", Rect(body.r() - 170 * s, by, 170 * s, 38 * s), ghBusy ? "Pushing…" : "Commit & Push", BTN_PRIMARY, false, ok)) {
            doc.git.repo = commitRepo;
            doc.git.path = commitPath;
            bool keep = gh.autoPush;
            gh.autoPush = false;  // saving would push too; push once below with the user's message
            if (!filePath.empty() && filePath.rfind("content://", 0) != 0) saveFile(filePath, commitMsg);
            else recordVersion(doc.serialize(true), commitMsg);
            gh.autoPush = keep;
            pushToGitHub(commitMsg);
        }
        if (ui.button("commit-cancel", Rect(body.r() - 270 * s, by, 90 * s, 38 * s), "Close")) ui.closeSheet();
    });
}

// ---------------------------------------------------------------------------
// GitHub Account sheet
// ---------------------------------------------------------------------------
void App::openGitHubAccount() {
    tokenInput.clear();
    repoInput = gh.defaultRepo;
    ui.openSheet("account", "GitHub Account", 600, 520, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float y = body.y;
        if (!github::available()) {
            ui.wrappedText(body, "This build of the app can't reach GitHub yet (it was built without HTTPS support). "
                                 "Versions on this device still work: File ▸ Version History.", th.textDim, 14 * s);
            return;
        }
        if (githubConnected()) {
            g.text({body.x, y + 10 * s}, "Connected as @" + gh.login, 16 * s, th.ok, 0);
            if (ui.button("acct-disconnect", Rect(body.r() - 120 * s, y - 4 * s, 120 * s, 32 * s), "Disconnect")) {
                gh.token.clear();
                gh.login.clear();
                github::saveSettings(gh);
                ui.toast("Disconnected from GitHub. The token was removed from this device.");
            }
            y += 46 * s;
            g.text({body.x, y + 8 * s}, "Repository for designs (created private if missing)", 13 * s, th.textDim, 0);
            if (ui.textField("acct-repo", Rect(body.x, y + 20 * s, body.w, 34 * s), repoInput, "electronics-designs", true)) {}
            y += 66 * s;
            bool ap = gh.autoPush;
            if (ui.checkbox("acct-autopush", Rect(body.x, y, body.w, 30 * s), "Commit to GitHub automatically every time I save", ap)) gh.autoPush = ap;
            y += 40 * s;
            ui.wrappedText(Rect(body.x, y, body.w, 0),
                           "Designs go into this repository, each in its own folder. A design can also be given its own repository "
                           "when you commit it. Versions on this device are kept either way.",
                           th.textDim, 13 * s);
            if (ui.button("acct-save", Rect(body.r() - 120 * s, body.b() - 38 * s, 120 * s, 38 * s), "Done", BTN_PRIMARY)) {
                std::string r;
                for (char c : repoInput) if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') r += c;
                if (!r.empty()) gh.defaultRepo = r;
                github::saveSettings(gh);
                ui.closeSheet();
            }
            return;
        }
        // not connected
        Rect sign(body.x, y, body.w, 40 * s);
        ui.button("acct-oauth", sign, "Sign in with GitHub — coming soon", BTN_NORMAL, false, false);
        y += 56 * s;
        float h = ui.wrappedText(Rect(body.x, y, body.w, 0),
                                 "For now, connect with a personal access token. On GitHub: Settings ▸ Developer settings ▸ Fine-grained tokens ▸ "
                                 "Generate new token. Give it access to the repositories you'll use (or All repositories) with Contents: Read and write, "
                                 "plus Administration: Read and write so the app can create a private repository for your designs.",
                                 th.textDim, 13 * s);
        y += h + 8 * s;
        if (ui.button("acct-open", Rect(body.x, y, 210 * s, 32 * s), "Open GitHub token page", BTN_FLAT))
            SDL_OpenURL("https://github.com/settings/personal-access-tokens/new");
        y += 44 * s;
        g.text({body.x, y + 8 * s}, "Token", 13 * s, th.textDim, 0);
        ui.textField("acct-token", Rect(body.x, y + 20 * s, body.w, 34 * s), tokenInput, "github_pat_…", true);
        y += 66 * s;
        if (!ghStatus.empty() && !githubConnected()) ui.wrappedText(Rect(body.x, y, body.w, 0), ghStatus, ghBusy ? th.textDim : th.danger, 13 * s);
        ui.wrappedText(Rect(body.x, body.b() - 80 * s, body.w - 140 * s, 0),
                       "The token is stored only on this device, readable only by you. Disconnect removes it.", th.textDim, 12 * s);
        bool can = !ghBusy && tokenInput.size() > 10;
        if (ui.button("acct-connect", Rect(body.r() - 120 * s, body.b() - 38 * s, 120 * s, 38 * s), ghBusy ? "Checking…" : "Connect", BTN_PRIMARY, false, can)) {
            std::string token;
            for (char c : tokenInput) if (!std::isspace((unsigned char)c)) token += c;
            ghBusy = true;
            ghStatus = "Checking the token…";
            auto result = std::make_shared<std::pair<std::string, std::string>>();
            github::runAsync([token, result]() { github::whoAmI(token, result->first, result->second); },
                             [this, token, result]() {
                                 ghBusy = false;
                                 if (!result->second.empty()) { ghStatus = result->second; return; }
                                 gh.token = token;
                                 gh.login = result->first;
                                 github::saveSettings(gh);
                                 tokenInput.clear();
                                 ghStatus.clear();
                                 repoInput = gh.defaultRepo;
                                 ui.toast("Connected to GitHub as @" + gh.login);
                             },
                             [this](std::function<void()> f) { runOnMain(std::move(f)); });
        }
    });
}
