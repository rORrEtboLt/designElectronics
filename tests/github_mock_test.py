#!/usr/bin/env python3
"""Tests GitHub sync against a local mock of the GitHub REST API (no network,
no real account). Starts the mock, runs `designElectronics --github-selftest`
with DE_GITHUB_API pointing at it, and reports the result.

    python3 tests/github_mock_test.py path/to/designElectronics
"""
import base64, hashlib, json, os, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, unquote

TOKEN, LOGIN = "test-token", "octocat"
repos = {}      # "owner/name" -> {"files": {path: (blob_sha, bytes)}, "commits": [ {sha, message, date, path, content} ]}


class Mock(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, obj=None, raw=None):
        body = raw if raw is not None else json.dumps(obj or {}).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/octet-stream" if raw is not None else "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def authed(self):
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            self.send(401, {"message": "Bad credentials"})
            return False
        if "designElectronics" not in self.headers.get("User-Agent", ""):
            self.send(403, {"message": "User-Agent required"})
            return False
        return True

    def route(self):
        u = urlparse(self.path)
        parts = [unquote(p) for p in u.path.split("/") if p]
        return parts, {k: v[0] for k, v in parse_qs(u.query).items()}

    def do_GET(self):
        if not self.authed():
            return
        parts, q = self.route()
        if parts == ["user"]:
            return self.send(200, {"login": LOGIN})
        if len(parts) == 3 and parts[0] == "repos":
            return self.send(200 if "/".join(parts[1:3]) in repos else 404, {"message": "Not Found"})
        repo = "/".join(parts[1:3]) if len(parts) > 3 else ""
        if repo not in repos:
            return self.send(404, {"message": "Not Found"})
        r = repos[repo]
        if parts[3] == "contents":
            path = "/".join(parts[4:])
            ref = q.get("ref")
            if "raw" in self.headers.get("Accept", ""):
                for c in r["commits"]:
                    if c["sha"] == ref and c["path"] == path:
                        return self.send(200, raw=c["content"])
                return self.send(404, {"message": "Not Found"})
            if path in r["files"]:
                return self.send(200, {"sha": r["files"][path][0], "path": path})
            return self.send(404, {"message": "Not Found"})
        if parts[3] == "commits":
            lst = [c for c in reversed(r["commits"]) if c["path"] == q.get("path")]
            return self.send(200, [{"sha": c["sha"], "html_url": "https://github.com/%s/commit/%s" % (repo, c["sha"]),
                                    "commit": {"message": c["message"], "author": {"name": "Octo Cat", "date": c["date"]}}} for c in lst])
        self.send(404, {"message": "Not Found"})

    def body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}")

    def do_POST(self):
        if not self.authed():
            return
        parts, _ = self.route()
        if parts == ["user", "repos"]:
            b = self.body()
            assert b.get("private") is True, "repos must be created private"
            repos[LOGIN + "/" + b["name"]] = {"files": {}, "commits": []}
            return self.send(201, {"full_name": LOGIN + "/" + b["name"], "private": True})
        self.send(404, {"message": "Not Found"})

    def do_PUT(self):
        if not self.authed():
            return
        parts, _ = self.route()
        repo, path = "/".join(parts[1:3]), "/".join(parts[4:])
        if repo not in repos or parts[3] != "contents":
            return self.send(404, {"message": "Not Found"})
        r, b = repos[repo], self.body()
        if path in r["files"] and b.get("sha") != r["files"][path][0]:
            return self.send(409, {"message": "sha does not match"})
        content = base64.b64decode(b["content"])
        blob = hashlib.sha1(content).hexdigest()
        sha = hashlib.sha1((path + str(len(r["commits"])) + b["message"]).encode()).hexdigest()
        date = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        r["files"][path] = (blob, content)
        r["commits"].append({"sha": sha, "message": b["message"], "date": date, "path": path, "content": content})
        self.send(201, {"commit": {"sha": sha, "message": b["message"], "html_url": "https://github.com/%s/commit/%s" % (repo, sha),
                                   "author": {"date": date}}})


def main():
    exe = sys.argv[1]
    srv = ThreadingHTTPServer(("127.0.0.1", 0), Mock)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    env = dict(os.environ, DE_GITHUB_API="http://127.0.0.1:%d" % srv.server_address[1])
    r = subprocess.run([exe, "--github-selftest"], env=env, capture_output=True, text=True, timeout=120)
    print(r.stdout.strip())
    srv.shutdown()
    created = [k for k in repos]
    print("repos created by the app:", created, "(private)")
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
