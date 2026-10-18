#!/usr/bin/env python3
"""Generates parts/logic_little.txt: single, dual and triple gates (74LVC1G/2G/3G and
friends). Pinouts from the TI datasheets named in each block (DBV/DCK pin numbering)."""
import csv, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
rows = list(csv.DictReader(open(os.environ.get("KICAD_CHECKLIST", os.path.join(HERE, "..", "..", "build", "kicad_checklist.tsv")), encoding="utf-8"), delimiter="\t"))
def kicad_aliases(base):
    return [r["symbol"] for r in rows if r["library"] == "74xGxx" and (r["base"] == base) and r["symbol"] != base]
P5 = "SOT-23-5, SC-70-5"
P6 = "SOT-23-6, SC-70-6"
P8 = "VSSOP-8 P0.5 2.3x2.0, SSOP-8 P0.65 2.9x2.8"
G2 = "A B GND Y VCC"
B1 = "NC A GND Y VCC"
P = []
def add(block, title, desc, pk, pins, sim="", ti=None):
    P.append(dict(block=block, title=title, desc=desc, pk=pk, pins=pins, sim=sim, ti=ti or "sn" + block.lower()))
LG = " Simulated as digital logic."
def g2(g): return f"VCC=VCC;GND=GND;{g} A,B>Y"
for b, t, g in [("74LVC1G00", "Single 2-Input NAND", "NAND"), ("74LVC1G02", "Single 2-Input NOR", "NOR"), ("74LVC1G08", "Single 2-Input AND", "AND"),
                ("74LVC1G32", "Single 2-Input OR", "OR"), ("74LVC1G86", "Single 2-Input XOR", "XOR"), ("74LVC1G38", "Single 2-Input NAND (Open Drain)", ""),
                ("74LVC1G386", "Single 3-Input XOR", None)]:
    if g is None: continue
    add(b, t, "1.65-5.5 V single gate." + (LG if g else ""), P5, G2, g2(g) if g else "")
for b, t, g in [("74LVC1G04", "Single Inverter", "NOT"), ("74LVC1G06", "Single Inverter (Open Drain)", ""), ("74LVC1G07", "Single Buffer (Open Drain)", ""),
                ("74LVC1G14", "Single Schmitt Inverter", "SNOT"), ("74LVC1G17", "Single Schmitt Buffer", "BUF"), ("74LVC1G34", "Single Buffer", "BUF")]:
    add(b, t, "1.65-5.5 V single gate." + (LG if g else ""), P5, B1, f"VCC=VCC;GND=GND;{g} A>Y" if g else "")
add("74LVC1GU04DRL", "Single Unbuffered Inverter", "Unbuffered inverter for crystal oscillators.", P5, B1, "", "sn74lvc1gu04")
add("74LVC1G125", "Single Tri-State Buffer (~OE)", "Buffer with active-low output enable." + LG, P5, "~OE A GND Y VCC", "VCC=VCC;GND=GND;TRI A,~OE>Y")
add("74LVC1G126", "Single Tri-State Buffer (OE)", "Buffer with active-high output enable.", P5, "OE A GND Y VCC", "")
add("74LVC1G240", "Single Tri-State Inverting Buffer", "Inverting buffer with active-low output enable.", P5, "~OE A GND ~Y VCC", "")
add("74LVC1G79", "Single D Flip-Flop", "Positive-edge D flip-flop.", P5, "D CLK GND Q VCC", "")
add("74LVC1G80", "Single D Flip-Flop, Inverted Output", "Positive-edge D flip-flop with inverted output.", P5, "D CLK GND ~Q VCC", "")
add("74LVC1G10", "Single 3-Input NAND", "3-input NAND gate.", P6, "A GND B Y VCC C", "VCC=VCC;GND=GND;NAND A,B,C>Y")
add("74LVC1G11", "Single 3-Input AND", "3-input AND gate.", P6, "A GND B Y VCC C", "VCC=VCC;GND=GND;AND A,B,C>Y")
add("74LVC1G27", "Single 3-Input NOR", "3-input NOR gate.", P6, "A GND B Y VCC C", "VCC=VCC;GND=GND;NOR A,B,C>Y")
add("74LVC1G57", "Configurable Multiple-Function Gate", "Wire the inputs to make AND, OR, NAND, NOR, XNOR, inverter or buffer.", P6, "In1 GND In0 Y VCC In2", "")
add("74LVC1G58", "Configurable Multiple-Function Gate", "Configurable gate (see the datasheet's function table).", P6, "In1 GND In0 Y VCC In2", "")
add("74LVC1G99", "Ultra-Configurable Gate, Tri-State", "Configurable gate with output enable.", "SSOP-8 P0.65 2.9x2.8, VSSOP-8 P0.5 2.3x2.0", "~OE A B GND C D Y VCC", "")
add("74LVC1G3157", "SPDT Analog Switch", "Single-pole double-throw analog switch.", P6, "B2 GND B1 A VCC S", "", "sn74lvc1g3157")
add("74LVC1G66", "Single Bilateral Analog Switch", "SPST analog switch.", P5, "A B GND C VCC", "")
add("74LVC1G18", "1-of-2 Demultiplexer, Tri-State", "Routes A to Y0 or Y1.", P6, "S GND A Y1 VCC Y0", "")
add("74LVC1G19", "1-of-2 Decoder/Demultiplexer", "Two-output decoder with enable.", P6, "A GND ~E ~Y1 VCC ~Y0", "")
add("74LVC1G0832", "Single 3-Input AND-OR", "Y = (A AND B) OR C.", P6, "A GND B Y VCC C", "")
add("74LVC1G332", "Single 3-Input OR", "3-input OR gate.", P6, "A GND B Y VCC C", "VCC=VCC;GND=GND;OR A,B,C>Y")
add("74LVC1G3208", "Single 3-Input OR-AND", "Y = (A OR B) AND C.", P6, "A GND B Y VCC C", "")
add("74LVC1G175", "Single D Flip-Flop with Clear", "Positive-edge D flip-flop with asynchronous clear.", P6, "CLK GND D Q VCC ~CLR", "")
add("74LVC1G373", "Single D Latch, Tri-State", "Transparent latch with tri-state output.", P6, "LE GND D Q VCC ~OE", "")
add("74LVC1G374", "Single D Flip-Flop, Tri-State", "Edge-triggered D flip-flop with tri-state output.", P6, "CLK GND D Q VCC ~OE", "")
add("74LVC1G74", "Single D Flip-Flop with Set and Clear", "Positive-edge D flip-flop with preset and clear.", P8, "CLK D ~Q GND Q ~CLR ~PRE VCC", "")
add("74AUC1G74", "Single D Flip-Flop with Set and Clear", "0.8-2.7 V positive-edge D flip-flop.", P8, "CLK D ~Q GND Q ~CLR ~PRE VCC", "")
add("74LVC1G123", "Single Retriggerable Monostable", "One-shot with clear.", P8, "~A B ~CLR GND Q CEXT REXT/CEXT VCC", "")
add("74LVC1G139", "2-to-4 Line Decoder", "Two address inputs, four active-low outputs.", P8, "A B ~Y3 GND ~Y2 ~Y1 ~Y0 VCC", "")
add("74LVC1G29", "2-of-3 Decoder/Demultiplexer", "Two inputs, three outputs.", P8, "~G Y1 A0 GND Y2 A1 Y0 VCC", "")
add("74LVC2G00", "Dual 2-Input NAND", "Two NAND gates." + LG, P8, "1A 1B 2Y GND 2A 2B 1Y VCC", "VCC=VCC;GND=GND;NAND 1A,1B>1Y;NAND 2A,2B>2Y")
add("74LVC2G02", "Dual 2-Input NOR", "Two NOR gates." + LG, P8, "1A 1B 2Y GND 2A 2B 1Y VCC", "VCC=VCC;GND=GND;NOR 1A,1B>1Y;NOR 2A,2B>2Y")
add("74LVC2G08", "Dual 2-Input AND", "Two AND gates." + LG, P8, "1A 1B 2Y GND 2A 2B 1Y VCC", "VCC=VCC;GND=GND;AND 1A,1B>1Y;AND 2A,2B>2Y")
add("74LVC2G32", "Dual 2-Input OR", "Two OR gates." + LG, P8, "1A 1B 2Y GND 2A 2B 1Y VCC", "VCC=VCC;GND=GND;OR 1A,1B>1Y;OR 2A,2B>2Y")
add("74LVC2G86", "Dual 2-Input XOR", "Two XOR gates." + LG, P8, "1A 1B 2Y GND 2A 2B 1Y VCC", "VCC=VCC;GND=GND;XOR 1A,1B>1Y;XOR 2A,2B>2Y")
add("74LVC2G38", "Dual 2-Input NAND (Open Drain)", "Two open-drain NAND gates.", P8, "1A 1B 2Y GND 2A 2B 1Y VCC", "")
for b, t, g in [("74LVC2G04", "Dual Inverter", "NOT"), ("74LVC2G06", "Dual Inverter (Open Drain)", ""), ("74LVC2G07", "Dual Buffer (Open Drain)", ""),
                ("74LVC2G14", "Dual Schmitt Inverter", "SNOT"), ("74LVC2G17", "Dual Schmitt Buffer", "BUF"), ("74LVC2G34", "Dual Buffer", "BUF")]:
    add(b, t, "Two gates in six pins." + (LG if g else ""), P6, "1A GND 2A 2Y VCC 1Y", f"VCC=VCC;GND=GND;{g} 1A>1Y;{g} 2A>2Y" if g else "")
add("74LVC2G125", "Dual Tri-State Buffer (~OE)", "Two buffers with active-low enables.", P8, "~1OE 1A 2Y GND 2A 1Y ~2OE VCC", "")
add("74LVC2G126", "Dual Tri-State Buffer (OE)", "Two buffers with active-high enables.", P8, "1OE 1A 2Y GND 2A 1Y 2OE VCC", "")
add("74LVC2G240", "Dual Tri-State Inverting Buffer", "Two inverting buffers with active-low enables.", P8, "~1OE 1A ~2Y GND 2A ~1Y ~2OE VCC", "")
add("74LVC2G241", "Dual Tri-State Buffer (~OE and OE)", "Two buffers, one enable active low and one active high.", P8, "~1OE 1A 2Y GND 2A 1Y 2OE VCC", "")
add("74LVC2G157", "Single 2-Line to 1-Line Data Selector", "2:1 multiplexer with true and inverted outputs.", P8, "A B ~Y GND Y S ~G VCC", "")
add("74LVC2G53", "SPDT Analog Switch (2:1 Mux)", "Single-pole double-throw analog switch.", P8, "COM ~INH GND GND A Y2 Y1 VCC", "")
add("74LVC2G66", "Dual Bilateral Analog Switch", "Two SPST analog switches.", P8, "1A 1B 2C GND 2A 2B 1C VCC", "")
add("74LVC2G74", "Single D Flip-Flop with Set and Clear", "Positive-edge D flip-flop with preset and clear.", P8, "CLK D ~Q GND Q ~CLR ~PRE VCC", "")
add("74LVC2G79", "Dual D Flip-Flop", "Two positive-edge D flip-flops.", P8, "1CLK 1D 2Q GND 2CLK 2D 1Q VCC", "")
add("74LVC2G80", "Dual D Flip-Flop, Inverted Outputs", "Two D flip-flops with inverted outputs.", P8, "1CLK 1D ~2Q GND 2CLK 2D ~1Q VCC", "")
for b, t, g in [("74LVC3G04", "Triple Inverter", "NOT"), ("74LVC3G06", "Triple Inverter (Open Drain)", ""), ("74LVC3G07", "Triple Buffer (Open Drain)", ""),
                ("74LVC3G14", "Triple Schmitt Inverter", "SNOT"), ("74LVC3G17", "Triple Schmitt Buffer", "BUF"), ("74LVC3G34", "Triple Buffer", "BUF")]:
    add(b, t, "Three gates in eight pins." + (LG if g else ""), P8, "1A 3Y 2A GND 2Y 3A 1Y VCC",
        f"VCC=VCC;GND=GND;{g} 1A>1Y;{g} 2A>2Y;{g} 3A>3Y" if g else "")

def write(out):
    o = ["# Little logic: single, dual and triple gates. Pinouts from the TI datasheets",
         "# named in each block (DBV / DCK / DCT / DCU numbering). Generated; edit directly.", ""]
    for p in P:
        al = kicad_aliases(p["block"])
        o += [f"[{p['block']}]", f"title     = {p['title']}", "category  = Logic", f"desc      = {p['desc']}",
              f"datasheet = https://www.ti.com/lit/ds/symlink/{p['ti']}.pdf", f"package   = {p['pk']}", f"pins      = {p['pins']}"]
        if al: o.append("aliases   = " + ", ".join(al))
        if p["sim"]: o += ["sim       = logic", f"simParams = {p['sim']}"]
        o.append("")
    open(out, "w", encoding="utf-8").write("\n".join(o)); print(len(P), "blocks")
if __name__ == "__main__": write(sys.argv[1])
