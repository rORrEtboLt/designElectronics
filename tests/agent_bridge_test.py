#!/usr/bin/env python3
"""Checks the AI tab's MCP proxy: `designElectronics --mcp --connect PORT --token T`
must forward tool calls (with the token) to the app and relay the answers.
A fake app listens on 127.0.0.1 — no AI account needed.

    python3 tests/agent_bridge_test.py path/to/designElectronics
"""
import json, socket, subprocess, sys, threading

exe = sys.argv[1]
seen = []
srv = socket.socket(); srv.bind(("127.0.0.1", 0)); srv.listen(1)
port = srv.getsockname()[1]

def fake_app():
    c, _ = srv.accept()
    f = c.makefile("rwb")
    for line in f:
        req = json.loads(line)
        seen.append(req)
        ok = req.get("token") == "secret"
        resp = {"isError": not ok, "text": json.dumps({"added": req["arguments"].get("type")}) if ok else "Error: not authorised"}
        f.write((json.dumps(resp) + "\n").encode()); f.flush()

threading.Thread(target=fake_app, daemon=True).start()
p = subprocess.Popen([exe, "--mcp", "--connect", str(port), "--token", "secret"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.DEVNULL, text=True)
def rpc(i, method, params):
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": i, "method": method, "params": params}) + "\n"); p.stdin.flush()
    return json.loads(p.stdout.readline())
init = rpc(1, "initialize", {"protocolVersion": "2025-06-18", "capabilities": {}})
tools = rpc(2, "tools/list", {})
call = rpc(3, "tools/call", {"name": "add_part", "arguments": {"type": "led"}})
p.stdin.close(); p.wait(timeout=10)
ok = ("live on their screen" in init["result"].get("instructions", "")
      and any(t["name"] == "add_part" for t in tools["result"]["tools"])
      and not call["result"]["isError"] and json.loads(call["result"]["content"][0]["text"]) == {"added": "led"}
      and seen and seen[0]["token"] == "secret" and seen[0]["name"] == "add_part")
print("  instructions mention the live app:", "live on their screen" in init["result"].get("instructions", ""))
print("  forwarded:", seen[0] if seen else None)
print("Agent bridge test", "passed" if ok else "FAILED")
sys.exit(0 if ok else 1)
