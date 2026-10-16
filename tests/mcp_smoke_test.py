#!/usr/bin/env python3
"""Smoke test for the MCP server: builds an ESP32 + LED circuit, simulates it,
routes a PCB and exports Gerbers through the same JSON-RPC calls an AI client
makes.

    python3 tests/mcp_smoke_test.py path/to/designElectronics [output-dir]
"""
import json, subprocess, sys, tempfile
exe = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix="de-mcp-")
import os
env = dict(os.environ, DE_HISTORY_DIR=os.path.join(out, "history"))  # keep test versions out of the user's history
p = subprocess.Popen([exe, "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, env=env)
n = 0
def rpc(method, params=None, notify=False):
    global n
    msg = {"jsonrpc": "2.0", "method": method}
    if params is not None: msg["params"] = params
    if not notify: n += 1; msg["id"] = n
    p.stdin.write(json.dumps(msg) + "\n"); p.stdin.flush()
    if notify: return None
    return json.loads(p.stdout.readline())
def tool(name, **args):
    r = rpc("tools/call", {"name": name, "arguments": args})["result"]
    txt = r["content"][0]["text"]
    status = "ERR" if r["isError"] else "ok "
    print(f"[{status}] {name}: {txt[:300].replace(chr(10),' ')}")
    return None if r["isError"] else json.loads(txt)
init = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}})
print("server:", init["result"]["serverInfo"], init["result"]["protocolVersion"])
rpc("notifications/initialized", notify=True)
print("tools:", [t["name"] for t in rpc("tools/list")["result"]["tools"]])
tool("list_parts", query="raspberry pi")
tool("new_design", title="MCP test: ESP32 LED")
bb = tool("add_part", type="breadboard", properties={"size": "half+"})
esp = tool("add_part", type="esp32_devkit", properties={"outputs": "GPIO2=HIGH"})
r = tool("add_part", type="resistor", properties={"resistance": "220Ω"})
led = tool("add_part", type="led", properties={"color": "Red"})
tool("place_on_breadboard", part=r["label"], pin="pin0", breadboard=bb["label"], hole="f6")
tool("place_on_breadboard", part=led["label"], pin="anode", breadboard=bb["label"], hole="g10")
tool("connect", from_part=esp["label"], from_pin="GPIO2", to_part=bb["label"], to_pin="j6", views=["breadboard"])
tool("connect", from_part=led["label"], from_pin="cathode", to_part=esp["label"], to_pin="GND", views=["breadboard"])
tool("connect", from_part=esp["label"], from_pin="GPIO2", to_part=r["label"], to_pin="pin0", views=["schematic"])
tool("connect", from_part=r["label"], from_pin="pin1", to_part=led["label"], to_pin="anode", views=["schematic"])
tool("connect", from_part=led["label"], from_pin="cathode", to_part=esp["label"], to_pin="GND", views=["schematic"])
sim = tool("simulate", seconds=0.2)
tool("connect", from_part="R1", from_pin="nope", to_part="LED1", to_pin="anode")
board = tool("add_part", type="pcb_board", properties={"width": "80", "height": "50"})
tool("move_part", part=board["label"], view="pcb", x=-300, y=-300)
tool("move_part", part=esp["label"], view="pcb", x=0, y=0)
tool("move_part", part=r["label"], view="pcb", x=300, y=1400)
tool("move_part", part=led["label"], view="pcb", x=1000, y=1450)
tool("autoroute")
tool("run_drc")
tool("export", format="gerber", path=out + "/mcp_test_gerbers.zip")
tool("export", format="bom")
tool("save_design", path=out + "/mcp_test.dez")
d = tool("get_design")
print("routing:", d["routing"])
print("LED1 brightness:", sim["ledBrightness"])
p.stdin.close(); p.wait()
ok = sim["ledBrightness"].get("LED1", 0) > 0.2 and all(v["connectionsToRoute"] == 0 for v in d["routing"].values())
print("MCP smoke test", "passed" if ok else "FAILED")
sys.exit(0 if ok else 1)
