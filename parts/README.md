# Parts library data

Every `*.txt` file in this folder is compiled into designElectronics (see
`cmake/embed_parts.cmake`). Each block describes one **pinout**: a part number,
the packages it comes in and its pin names, written from the manufacturer's
datasheet. Other part numbers that share the pinout are listed as aliases and
show up as their own entries in the parts bin.

```ini
# Lines starting with # are comments.
[NE555]
title     = Precision Timer
category  = Analog ICs
desc      = Bipolar timer for delays, oscillators and PWM, 4.5–16 V.
datasheet = https://www.ti.com/lit/ds/symlink/ne555.pdf
package   = DIP-8, SOIC-8, TSSOP-8
pins      = GND TRIG OUT ~RESET CONT THRES DISCH VCC
aliases   = NA555, SA555, SE555
sim       = 555
simParams = U GND=GND,TRIG=TRIG,OUT=OUT,RESET=~RESET,CTRL=CONT,THR=THRES,DIS=DISCH,VCC=VCC
```

| key | meaning |
| --- | --- |
| `[PART]` | Part number. The part's type id is the part number in lower case with `_` for anything that isn't a letter or digit (`[LM2596S-5.0]` → `lm2596s_5_0`); `type = …` overrides it. |
| `title` | What it is, shown after the part number ("LM358 Dual Op-Amp"). |
| `category` | One of the parts-bin categories (`Analog ICs`, `Logic`, `Power`, `Discretes`, `Interface`, `Memory`, …). |
| `desc` | One or two sentences for the Inspector. |
| `datasheet` | Link to the manufacturer's datasheet the pinout was taken from. |
| `package` | Comma-separated package list; the first is the default. See *Packages*. |
| `pins` | Pin names in pin-number order, separated by spaces. `N:NAME` names pin N explicitly (BGA balls: `A1:VDD`), `EP:NAME` the exposed pad and `TAB:NAME` a tab. Names starting with `~` are active low and are drawn with an overline. Unnamed pins become `NC`. |
| `pins.<package>` | A different pin list for one package (full spec or just its family, e.g. `pins.SOT-23-5 = …`). |
| `aliases` | Comma-separated part numbers with the same pinout. |
| `symbol` | Schematic symbol: `box` (default), `npn`, `pnp`, `nmos`, `pmos`, `diode`, `zener`, `schottky`, `tvs`, `led`, `resistor`, `capacitor`, `ecap`, `inductor`, `ferrite`, `fuse`, `crystal`. Discrete symbols find their pins by name: `B C E`, `G D S`, `A K`, `+ -`. |
| `sch` | Box layout, e.g. `left: IN+ IN- ; right: OUT ; top: V+ ; bottom: V-`. Names or numbers; `-` leaves a gap. Without it, supply pins go on top, ground pins on the bottom and the rest down the left and up the right in pin order. |
| `sim`, `simParams` | Simulation model, as in [FILE_FORMAT.md](../docs/FILE_FORMAT.md#simulation-models-sim--simparams). Prefer pin names over numbers so every package works. |
| `prefix` | Reference designator prefix (`U`, `Q`, `D`, `R`, …). Default `U`. |
| `label` | Text printed on the body when it isn't the part number. |
| `style` | Breadboard look for 2-pin through-hole parts: `resistor`, `diode`, `capacitor`, `ecap`, `led`, `inductor`, `crystal`. |
| `ids = numbers` | Pin ids are `pin1`, `pin2`, … instead of the pin names. Only for blocks that replace an older catalog part, so saved designs keep their wires. |
| `replaces = yes` | This block replaces the built-in catalog part with the same type id. |
| `covers` | Other names this block stands for in the coverage checklist (`tools/kicad_checklist.py`), e.g. generic KiCad symbols. Not shown in the app. |
| `prop.<key>` | Extra property with a default value. |

Long values continue on the next lines when those lines start with a space or tab.

## Packages

Written the way datasheets write them. Options after the name are separated by
spaces: a body size `WxL` in mm (along × across the pin rows), a pitch `P0.5`,
an exposed pad `EP` (or `EP3.1x3.1` for its size), `W` for wide-body SOIC/DIP,
`D5` for a radial body diameter.

| family | examples |
| --- | --- |
| Through-hole | `DIP-8`, `DIP-28W`, `SIP-9`, `TO-92`, `TO-220-3`, `TO-220-5`, `TO-247-3`, `TO-126`, `HDR-1x6`, `HDR-2x5 P2.0`, `DO-41`, `DO-35`, `AXIAL P10.16`, `RADIAL P2.5 D6.3`, `LED-5MM`, `HC-49` |
| Gull-wing dual | `SOIC-8`, `SOIC-16W`, `SOP-8`, `SSOP-20`, `QSOP-16`, `TSSOP-14`, `TSSOP-48 P0.5 12.5x6.1`, `MSOP-10 P0.5`, `VSSOP-8`, `TSOP-II-44`, `TSOP-I-48` |
| No-lead dual | `DFN-8 3x3 P0.65 EP`, `SON-6 2x2 P0.65 EP`, `WSON-8 6x5 P1.27 EP` |
| Quad | `QFN-32 5x5 P0.5 EP`, `VQFN-20 3x3 P0.4 EP`, `LQFP-64`, `LQFP-100 14x14`, `TQFP-44`, `PQFP-100 14x20 P0.65` |
| SOT | `SOT-23`, `SOT-23-5`, `SOT-23-6`, `SOT-23-8`, `TSOT-23-6`, `SC-70-5`, `SOT-323`, `SOT-363`, `SOT-563`, `SOT-143` |
| Tab | `SOT-89`, `SOT-223`, `SOT-223-5`, `TO-252-2` (DPAK), `TO-252-3`, `TO-263-2` (D2PAK), `TO-263-5`, `TO-263-7` |
| Two-terminal SMD | `0201` … `2512`, `SOD-123`, `SOD-123F`, `SOD-323`, `SOD-523`, `SOD-128`, `SMA`, `SMB`, `SMC`, `SMAF`, `MINIMELF`, `MELF`, `POWERDI-123` |
| BGA | `BGA-100 10x10 P0.8` with every ball named (`A1:VDD …`) |

Land patterns come from the JEDEC outlines with IPC-7351 nominal fillets (see
`src/packages.cpp`). Pin 1 is at the lower left; dual-row packages count along
the bottom row and back along the top, quad packages count counter-clockwise
from the top of the left side. Tabs share the number of the lead they're joined
to; an exposed pad is pin n+1.

## Checking your work

`designElectronics --selftest` builds every block in every package and fails
if a pin list doesn't fit its package or any pads overlap.
`python3 tools/kicad_checklist.py <kicad-symbols checkout> --coverage` lists
which KiCad library parts still have no block here.
