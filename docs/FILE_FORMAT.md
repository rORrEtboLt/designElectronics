# The `.dez` file format (version 1)

A designElectronics project is one UTF-8 **JSON** file with the extension
`.dez`. It is self-contained: besides the design itself it embeds the full
definition of every part type it uses, so it opens in any designElectronics
install, offline, even one that has never seen those parts.

Units: all coordinates and sizes are **mils** (1/1000 inch, 0.0254 mm). The
y axis points down. Breadboard holes are 100 mil apart.

## Top level

```jsonc
{
  "format": "designElectronics",   // required, identifies the file
  "version": 1,                    // format version; readers ignore unknown fields
  "app": "designElectronics 0.1.0",
  "title": "LED with battery",
  "author": "Ada",                 // optional
  "notes": "Project description",  // optional
  "saved": "2026-10-16T05:36:48Z",
  "rules": { "traceWidth": 24, "clearance": 12, "viaPad": 40, "viaDrill": 20 },
  "nextId": 14,
  "parts":   [ ... ],
  "wires":   [ ... ],
  "notes_":  [ ... ],              // sticky notes on the canvas
  "library": [ ... ]               // embedded part definitions
}
```

## Parts

```jsonc
{
  "id": 2,                         // unique among parts, wires and notes
  "type": "resistor",              // part type; looked up in the library
  "label": "R1",
  "props": { "resistance": "470Ω", "power": "1/4W", "tolerance": "±5%" },
  "views": {                       // placement in each view the part appears in
    "breadboard": { "at": [500, 1000] },
    "schematic":  { "at": [300, 0], "rot": 90 },
    "pcb":        { "at": [500, 400], "flip": true }
  }
}
```

`rot` is clockwise in degrees (0, 90, 180, 270), applied after `flip`, which
mirrors horizontally.

## Wires and traces

```jsonc
{
  "id": 6,
  "view": "breadboard",            // breadboard | schematic | pcb
  "points": [[500, 1400], [500, 1600]],   // ≥ 2 points; inner points are bends
  "from": { "part": 1, "pin": "a6" },     // optional: end attached to a pin
  "to":   { "part": 1, "pin": "bp5" },
  "color": "#dc3232",              // breadboard only
  "width": 24, "layer": "top",     // pcb only (layer: top | bottom)
  "autorouted": true               // pcb only, set by the autorouter
}
```

Ends without `from`/`to` are connected by position: an end within 15 mil of a
pin, or within 3 mil of another wire end in the same view, connects to it.

## Connectivity rules

* Two pins of different parts within 15 mil of each other in a view are
  connected (that's how parts plug into breadboards and headers).
* Pins listed together in a part's `buses` are always connected (breadboard
  strips, the two GND pins of an Arduino, …).
* Parts with a `netProp` (ground, power and net labels) connect to every other
  such part with the same value.
* A connection in **any** view counts in **all** views. Views where it isn't
  wired show ratsnest lines.

## Notes

```jsonc
{ "id": 13, "view": "breadboard", "at": [0, -1100], "size": [2200, 600], "text": "Hello" }
```

## Part definitions (`library`, `.dezpart`)

The `library` array holds one definition per part type used. A `.dezpart` file
holds a single definition:

```jsonc
{ "format": "designElectronics-part", "version": 1, "part": { ...definition... } }
```

A definition:

```jsonc
{
  "type": "my_sensor",             // unique id; use a prefix to avoid clashes
  "title": "My Sensor",
  "category": "Custom",
  "description": "Shown in the Inspector",
  "labelPrefix": "U",
  "valueProp": "model",            // property shown next to the label
  "netProp": "",                   // see connectivity rules
  "sim": "",                       // simulator model (resistor, led, battery, …) or empty
  "views": ["breadboard", "schematic", "pcb"],
  "properties": { "model": "XYZ-1" },            // defaults
  "propertyDefs": [ { "key": "model", "label": "Model", "options": ["XYZ-1", "XYZ-2"] } ],
  "pins": [
    { "id": "vcc", "name": "VCC",
      "breadboard": [0, 0], "schematic": [0, 100], "pcb": [0, 0],
      "pad": 70, "drill": 38, "square": true }
  ],
  "buses": [["gnd1", "gnd2"]],
  "holes": [ { "at": [100, 100], "diameter": 125 } ],   // non-plated PCB holes
  "graphics": {
    "breadboard": [ primitives… ],
    "schematic":  [ primitives… ],
    "pcb":        [ primitives… ]   // silkscreen; copper pads come from pins
  }
}
```

### Graphic primitives

| `k` | fields |
| --- | --- |
| `line` | `a`, `b`, `w`, `stroke` |
| `rect` | `a` (min corner), `b` (max corner), optional `r` (corner radius), `fill`, `stroke`, `w` |
| `circle` | `c`, `r`, `fill`, `stroke`, `w` |
| `arc` | `c`, `r`, `a0`, `a1` (degrees, 0 = +x, clockwise), `w`, `stroke` |
| `poly` | `pts` — filled polygon if `fill` is set, otherwise a polyline |
| `text` | `at` (vertical centre), `text`, `size`, `align` (0 left, 1 centre, 2 right), `fill` |

Colours are `#rrggbb` or `#rrggbbaa`. Optional `tag`: `1` marks an LED lens
(glows in simulation), `2` marks a meter display (shows the simulated reading),
`10`–`17` mark display segments a–g and dp (`fill` = unlit colour, `stroke` = lit colour).

## Simulation models (`sim` + `simParams`)

Any part, including your own, can be simulated by naming a model in `sim` and
mapping its pins in `simParams`: a `;`-separated list of `KEY=value` settings,
bare flags and `ITEM args` lines. A pin is given by its 1-based number, its name or its id; a name also matches the part before a `/` (so `GPIO2` finds the pin named `GPIO2/SDA`).

| `sim` | `simParams` example | Behaviour |
| --- | --- | --- |
| `logic` | `VCC=14;GND=7;NAND 1,2>3;NOT 5>6;ROUT=50` | Digital gates: `AND OR NAND NOR XOR XNOR NOT BUF` (inputs `>` output), Schmitt `SNOT SNAND`, tri-state `TRI a,oe>y`, `DFF d,clk,pre,clr>q,qn` (active-low), `DFFH` (active-high set/reset), `CNT clk,clr>qa..qd`, `SR595 ser,srclk,rclk,srclr,oe>qa..qh,qh'`, `J4017 clk,inh,rst>q0..q9,co`, `DEC138 a,b,c,g1,g2a,g2b>y0..y7`, `BCD7 a,b,c,d,lt,bl,le>a..g`, `MUX s0,s1,s2,e,z>y0..y7`, `SW ctrl>a,b`. Inputs above ½ VCC read HIGH; outputs drive to the rails through `ROUT` Ω. |
| `opamp` | `VCC=8;GND=4;HI=1.5;LO=0.02;OA 3,2>1` | High-gain amplifier (`OA in+,in->out`) limited to `VCC−HI … GND+LO`. |
| `comparator` | `VCC=8;GND=4;CMP 3,2>1` | Open-collector output pulls LOW when in+ < in−. |
| `555` | `U GND=1,TRIG=2,OUT=3,RESET=4,CTRL=5,THR=6,DIS=7,VCC=8` | 555 timer (repeat `U` for a 556). |
| `board` | `GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40` | USB-powered dev board; drives the pins listed in its `outputs` property: `PIN=HIGH`, `LOW`, `BLINK:ms`, `PWM:%`. |
| `mcu` | `VCC=7,20;GND=8,22;ROUT=25` | Bare microcontroller chip powered from the circuit, same `outputs` property. |
| `reg` | `IN=3;OUT=2;GND=1;V=3.3;DROP=1.1` | Linear regulator (`ADJ=` + `VREF=` for LM317-style, `VPROP=` to read the voltage from a property, `BOOST` for step-up). |
| `sensor` | `VCC=1;GND=3;LIN 2:temperature:0:0.01` | Outputs from properties: `LIN pin:prop:a:b` (V = a + b·prop), `RATIO pin:prop` (% of VCC), `DIG pin:prop[:V]`, `OC pin:prop` (open collector). |
| `uln` | `GND=8;IN 1>16` | Darlington sink outputs. |
| `l293` | `VS=8;GND=4;CH 1:2>3` | H-bridge half: `CH enable:input>output`. |
| `relay` | `COIL 1,2;COM=3;NO=4;NC=5;R=70;I=0.04` | Coil resistance and pull-in current. |
| `opto` | `LED 1>2;SW 4>3` | LED + phototransistor switch. |
| `seg7` | `[CA;]COM=3,8;SEG 7,6,4,2,1,9,10,5` | 7-segment display, segments a–g, dp. |
| `rgbled` | `[CA;]COM=2;R=1;G=3;B=4` | RGB LED. |
| `leds` | `LED 15>16:3.0:20` | Generic LEDs `anode>cathode:Vf:R`. |
| `npn` / `pnp` | `BETA=150;VF=0.65` | Bipolar transistor (pins `B`, `C`, `E`). |
| `nmos` | `VTH=2` | MOSFET switch (pins `G`, `D`, `S`). |
| `amp386`, `tl431` | see catalog | Audio amp, shunt reference. |

Built-in passive models (`resistor`, `capacitor`, `inductor`, `potentiometer`,
`photocell`, `led`, `diode`, `battery`, `switch`, `pushbutton`, `motor`,
`buzzer`, `multimeter`) use fixed pin ids — see the built-in parts.

The simulator runs in real time with 1 ms steps: capacitors charge, 555s and
RC oscillators oscillate, and counters and shift registers clock. Parts with no
model are listed as "Not simulated (pinout only)".

## Compatibility

* Readers must ignore unknown fields. Writers keep the field order shown above.
* When a file's `version` is newer than the reader supports, the reader loads
  what it understands and warns.
* Built-in part types are regenerated from their properties on load. The
  embedded definition is used only when the reader doesn't know the type
  (or the type is a custom part).
