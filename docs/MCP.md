# Using designElectronics from an AI assistant (MCP)

designElectronics has a built-in [Model Context Protocol](https://modelcontextprotocol.io)
server. Start the app with `--mcp` and an AI assistant (Claude Desktop, Claude
Code, Cursor, VS Code Copilot, or any MCP client) can design circuits for you:
pick parts, wire them, plug them into a breadboard, simulate, lay out and route
the PCB, check it and export Gerbers. The result is a normal `.dez` file you
open in the app.

It runs locally over stdin/stdout. No server, account or network is involved.

## In the app: the AI tab

The desktop app has an **AI** tab that runs Claude Code (or Codex CLI, Gemini CLI, or a custom
command) for you, connected live to the open design — see the
[docs](https://rorretbolt.github.io/designElectronics/#ai). Under the hood it starts
`designElectronics --mcp --connect <port> --token <token>`, which forwards every tool call to
the running app instead of working on a private copy.

## Setup

First build the app (see the [README](../README.md)). The server is the app
binary itself:

| Platform | Binary |
| --- | --- |
| macOS | `/path/to/designElectronics.app/Contents/MacOS/designElectronics` |
| Linux / Windows | `/path/to/designElectronics` (`designElectronics.exe`) |

### Claude Code

```sh
claude mcp add designElectronics -- /path/to/designElectronics --mcp
```

### Claude Desktop, Cursor, VS Code and other clients

Add this to the client's MCP configuration (for Claude Desktop:
*Settings ▸ Developer ▸ Edit Config*):

```json
{
  "mcpServers": {
    "designElectronics": {
      "command": "/path/to/designElectronics",
      "args": ["--mcp"]
    }
  }
}
```

Then ask things like:

* "Design an ESP32 circuit that blinks a red LED on GPIO2 on a breadboard, simulate it and save it to ~/blink.dez."
* "Open ~/blink.dez, make a 50×40 mm PCB for it, autoroute it, run DRC and export Gerbers."
* "Build a 555 astable that blinks at about 2 Hz and check the frequency in simulation."

## Tools

| Tool | What it does |
| --- | --- |
| `list_parts` | Search the parts library by words and/or category. |
| `get_part_info` | Pins, properties (with allowed values) and simulation model of a part type. |
| `new_design` / `open_design` / `load_example` / `save_design` | Start, open, load a built-in example, save as `.dez`. Every save (with an optional `message`) is also kept in the app's Version History. |
| `get_design` | Parts (labels, properties, positions), wires, nets and routing status per view, including which connections are still unrouted. |
| `add_part` | Add a part. Without `x`/`y` (mils) parts fill tidy rows in every view. Returns its pins. |
| `set_part_properties` | Change values, colours, labels, a dev board's `outputs` (e.g. `GPIO2=BLINK:500`) or its program (`properties.code` = an Arduino sketch that runs in the simulator); hide a part in some views or lock it. |
| `move_part` | Move, rotate or flip a part in one view. |
| `place_on_breadboard` | Plug a part in so a given pin sits in a given hole (`a1`…`j30`, rails `tp1`/`tn1`/`bp1`/`bn1`); reports the hole each pin landed in and what it now connects to, and warns about pins on a power rail, off the board or shorted together. |
| `remove_part` / `remove_wire` | Delete things. |
| `connect` | Wire two pins (schematic + breadboard by default). Connections count in every view. |
| `simulate` | Run the simulator: LED brightness, display segments, meter readings, motor activity, damaged parts, warnings, net voltages, plus each board's Serial output and program errors. |
| `autoroute` | Route the PCB (needs a `pcb_board` part under the components in the PCB view). |
| `run_drc` | Design rules check. |
| `export` | `gerber` (zip), `svg`, `bom`, `netlist`, `spice`, written to a file or returned inline. |

Parts and pins are referenced by label (`"R1"`) or id, and pins by id or name
(`"anode"`, `"GPIO2"`, `"D13"`). Coordinates are mils (1/1000 inch); breadboard
holes are 100 mil apart.

## Notes

* The server holds one design in memory. Call `save_design` to write it, then
  open the file in the app (or share it: `.dez` files are self-contained).
* Microcontroller firmware isn't executed. Dev boards drive the pins listed in
  their `outputs` property (`HIGH`, `LOW`, `BLINK:ms`, `PWM:%`).
* Test it without an AI: `python3 tests/mcp_smoke_test.py path/to/designElectronics`.
