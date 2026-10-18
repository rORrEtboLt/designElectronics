#!/usr/bin/env python3
"""Generates parts/logic_74xx.txt: 74-series logic. Pinouts written from the
TI datasheets (SN74HC/SN74LS/SN74LV families); see the datasheet line of each block."""
import csv, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = os.environ.get("KICAD_CHECKLIST", os.path.join(HERE, "..", "..", "build", "kicad_checklist.tsv"))
rows = list(csv.DictReader(open(CHECK, encoding="utf-8"), delimiter="\t"))
LOGIC_LIBS = {"74xx", "74xx_IEEE"}

def kicad_aliases(block, extra_bases=()):
    """KiCad names with the same pinout: the block itself as a base, its derived symbols, and same-number IEEE variants."""
    out = []
    bases = {block, *extra_bases}
    for r in rows:
        if r["library"] not in LOGIC_LIBS: continue
        if r["symbol"] in bases or r["base"] in bases: out.append(r["symbol"])
    return out

P14 = "DIP-14, SOIC-14, TSSOP-14"
P16 = "DIP-16, SOIC-16, TSSOP-16"
P20 = "DIP-20, SOIC-20W, TSSOP-20"
P24 = "DIP-24, SOIC-24W, TSSOP-24"
P24N = "DIP-24 W, SOIC-24W"

GATE4 = "1A 1B 1Y 2A 2B 2Y GND 3Y 3A 3B 4Y 4A 4B VCC"
NOR4 = "1Y 1A 1B 2Y 2A 2B GND 3A 3B 3Y 4A 4B 4Y VCC"
HEX = "1A 1Y 2A 2Y 3A 3Y GND 4Y 4A 5Y 5A 6Y 6A VCC"
TRI3 = "1A 1B 2A 2B 2C 2Y GND 3Y 3A 3B 3C 1Y 1C VCC"
DUAL4 = "1A 1B NC 1C 1D 1Y GND 2Y 2A 2B NC 2C 2D VCC"

def quad(g): return f"VCC=VCC;GND=GND;{g} 1A,1B>1Y;{g} 2A,2B>2Y;{g} 3A,3B>3Y;{g} 4A,4B>4Y"
def hexg(g): return "VCC=VCC;GND=GND;" + ";".join(f"{g} {i}A>{i}Y" for i in range(1, 7))
def tri(g): return f"VCC=VCC;GND=GND;{g} 1A,1B,1C>1Y;{g} 2A,2B,2C>2Y;{g} 3A,3B,3C>3Y"
def dual4(g): return f"VCC=VCC;GND=GND;{g} 1A,1B,1C,1D>1Y;{g} 2A,2B,2C,2D>2Y"
LG = " Simulated as digital logic."

# (block, title, desc, packages, pins, sim params ('' = pinout only), TI datasheet part, extra KiCad bases)
P = []
def add(block, title, desc, pk, pins, sim="", ti="", extra=(), replaces=False):
    P.append(dict(block=block, title=title, desc=desc, pk=pk, pins=pins, sim=sim, ti=ti, extra=extra, replaces=replaces))

add("74HC00", "Quad 2-Input NAND", "Four 2-input NAND gates." + LG, P14, GATE4, quad("NAND"), "sn74hc00", ("74LS00", "7400"), True)
add("74HC02", "Quad 2-Input NOR", "Four 2-input NOR gates." + LG, P14, NOR4, quad("NOR").replace("1A,1B>1Y", "1A,1B>1Y"), "sn74hc02", ("74LS02", "7402"), True)
add("74LS03", "Quad 2-Input NAND (Open Collector)", "Four 2-input NAND gates with open-collector outputs.", P14, GATE4, "", "sn74ls03", ("7401",))
add("74HC04", "Hex Inverter", "Six inverters." + LG, P14, HEX, hexg("NOT"), "sn74hc04", ("74LS04", "7404"), True)
add("74LS05", "Hex Inverter (Open Collector)", "Six inverters with open-collector outputs.", P14, HEX, "", "sn74ls05", ("7405",))
add("74LS06", "Hex Inverter Buffer (30 V Open Collector)", "Six high-voltage open-collector inverting buffers.", P14, HEX, "", "sn7406", ("7406",))
add("74LS07", "Hex Buffer (30 V Open Collector)", "Six high-voltage open-collector buffers.", P14, HEX, "", "sn7407", ("74LCX07", "7407"))
add("74HC08", "Quad 2-Input AND", "Four 2-input AND gates." + LG, P14, GATE4, quad("AND"), "sn74hc08", ("74LS08", "7408"), True)
add("74LS09", "Quad 2-Input AND (Open Collector)", "Four 2-input AND gates with open-collector outputs.", P14, GATE4, "", "sn74ls09", ("7409",))
add("74HC10", "Triple 3-Input NAND", "Three 3-input NAND gates." + LG, P14, TRI3, tri("NAND"), "sn74hc10", ("74LS10", "7410"), True)
add("74HC11", "Triple 3-Input AND", "Three 3-input AND gates." + LG, P14, TRI3, tri("AND"), "sn74hc11", ("74LS11", "7411"), True)
add("74HC14", "Hex Schmitt Inverter", "Six Schmitt-trigger inverters for RC oscillators and debouncing." + LG, P14, HEX, hexg("SNOT"), "sn74hc14", ("74LV14", "7414"), True)
add("74HC20", "Dual 4-Input NAND", "Two 4-input NAND gates." + LG, P14, DUAL4, dual4("NAND"), "sn74hc20", ("74LS20", "7420"), True)
add("74LS21", "Dual 4-Input AND", "Two 4-input AND gates." + LG, P14, DUAL4, dual4("AND"), "sn74ls21", ("7421",))
add("74LS26", "Quad 2-Input NAND (15 V Open Collector)", "High-voltage open-collector NAND gates.", P14, GATE4, "", "sn74ls26", ("7426",))
add("74HC27", "Triple 3-Input NOR", "Three 3-input NOR gates." + LG, P14, TRI3, tri("NOR"), "sn74hc27", ("74LS27", "7427"))
add("74HC30", "8-Input NAND", "One 8-input NAND gate." + LG, P14, "A B C D E F GND Y NC NC G H NC VCC",
    "VCC=VCC;GND=GND;NAND A,B,C,D,E,F,G,H>Y", "sn74hc30", ("74LS30", "7430"))
add("74HC32", "Quad 2-Input OR", "Four 2-input OR gates." + LG, P14, GATE4, quad("OR"), "sn74hc32", ("74LS32", "7432"), True)
add("74LS33", "Quad 2-Input NOR Buffer (Open Collector)", "Four 2-input NOR buffers, open-collector.", P14, NOR4, "", "sn74ls33", ("7433",))
add("74LS37", "Quad 2-Input NAND Buffer", "Four 2-input NAND buffers." + LG, P14, GATE4, quad("NAND"), "sn74ls37", ("7437",))
add("74LS38", "Quad 2-Input NAND Buffer (Open Collector)", "Four 2-input NAND buffers, open-collector.", P14, GATE4, "", "sn74ls38", ("7438",))
add("74HC86", "Quad 2-Input XOR", "Four 2-input exclusive-OR gates." + LG, P14, GATE4, quad("XOR"), "sn74hc86", ("74LS86", "7486"), True)
add("74LS136", "Quad 2-Input XOR (Open Collector)", "Four exclusive-OR gates, open-collector.", P14, GATE4, "", "sn74ls136", ("74136",))
add("74HC125", "Quad Tri-State Buffer (~OE)", "Four buffers with active-low output enables." + LG, P14,
    "~1OE 1A 1Y ~2OE 2A 2Y GND 3Y 3A ~3OE 4Y 4A ~4OE VCC",
    "VCC=VCC;GND=GND;TRI 1A,~1OE>1Y;TRI 2A,~2OE>2Y;TRI 3A,~3OE>3Y;TRI 4A,~4OE>4Y", "sn74hc125", ("74LS125", "74125", "74425"), True)
add("74HC126", "Quad Tri-State Buffer (OE)", "Four buffers with active-high output enables.", P14,
    "1OE 1A 1Y 2OE 2A 2Y GND 3Y 3A 3OE 4Y 4A 4OE VCC", "", "sn74hc126", ("74LS126", "74126"))
add("74HC132", "Quad 2-Input Schmitt NAND", "Four Schmitt-trigger NAND gates." + LG, P14, GATE4, quad("SNAND"), "sn74hc132", ("74LS132", "74132"))
add("74HC266", "Quad 2-Input XNOR", "Four exclusive-NOR gates." + LG, P14, "1A 1B 1Y 2Y 2A 2B GND 3A 3B 3Y 4Y 4A 4B VCC",
    "VCC=VCC;GND=GND;XNOR 1A,1B>1Y;XNOR 2A,2B>2Y;XNOR 3A,3B>3Y;XNOR 4A,4B>4Y", "sn74hc266", ("74LS266",))
add("74LS51", "Dual AND-OR-Invert", "One 2-wide 3-input and one 2-wide 2-input AND-OR-INVERT gate.", P14,
    "1A 2A 2B 2C 2D 2Y GND 1Y 1D 1E 1F 1B 1C VCC", "", "sn74ls51")

# ---- flip-flops, latches ----
add("74HC74", "Dual D Flip-Flop", "Two positive-edge D flip-flops with active-low preset and clear." + LG, P14,
    "~1CLR 1D 1CLK ~1PRE 1Q ~1Q GND ~2Q 2Q ~2PRE 2CLK 2D ~2CLR VCC",
    "VCC=VCC;GND=GND;DFF 1D,1CLK,~1PRE,~1CLR>1Q,~1Q;DFF 2D,2CLK,~2PRE,~2CLR>2Q,~2Q", "sn74hc74", ("74LS74", "7474"), True)
add("74LS73", "Dual J-K Flip-Flop with Clear", "Two negative-edge J-K flip-flops with clear. VCC on pin 4, GND on pin 11.", P14,
    "1CLK ~1CLR 1K VCC 2CLK ~2CLR 2J ~2Q 2Q 2K GND 1Q ~1Q 1J", "", "sn74ls73a", ("7473",))
add("74LS76", "Dual J-K Flip-Flop with Preset and Clear", "Two negative-edge J-K flip-flops. VCC on pin 5, GND on pin 13.", P16,
    "1CLK ~1PRE ~1CLR 1J VCC 2CLK ~2PRE ~2CLR 2J ~2Q 2Q 2K GND ~1Q 1Q 1K", "", "sn74ls76a", ("7476",))
add("74HC107", "Dual J-K Flip-Flop with Clear", "Two negative-edge J-K flip-flops with clear.", P14,
    "1J ~1Q 1Q 1K 2Q ~2Q GND 2J 2CLK ~2CLR 2K 1CLK ~1CLR VCC", "", "sn74hc107", ("74LS107",))
add("74HC109", "Dual J-K̄ Flip-Flop", "Two positive-edge J-K̄ flip-flops with preset and clear.", P16,
    "~1CLR 1J ~1K 1CLK ~1PRE 1Q ~1Q GND ~2Q 2Q ~2PRE 2CLK ~2K 2J ~2CLR VCC", "", "sn74hc109", ("74LS109",))
add("74HC112", "Dual J-K Flip-Flop", "Two negative-edge J-K flip-flops with preset and clear.", P16,
    "1CLK 1K 1J ~1PRE 1Q ~1Q ~2Q GND 2Q ~2PRE 2J 2K 2CLK ~2CLR ~1CLR VCC", "", "sn74hc112", ("74LS112",))
add("74HC173", "4-Bit D Register, Tri-State", "Four D flip-flops with gated enable and tri-state outputs.", P16,
    "M N 1Q 2Q 3Q 4Q CLK GND ~G1 ~G2 4D 3D 2D 1D CLR VCC", "", "sn74hc173", ("74LS173",))
add("74HC174", "Hex D Flip-Flop with Clear", "Six D flip-flops, common clock and clear.", P16,
    "~CLR 1Q 1D 2D 2Q 3D 3Q GND CLK 4Q 4D 5Q 5D 6D 6Q VCC", "", "sn74hc174", ("74LS174",))
add("74HC175", "Quad D Flip-Flop with Clear", "Four D flip-flops with true and complement outputs.", P16,
    "~CLR 1Q ~1Q 1D 2D ~2Q 2Q GND CLK 3Q ~3Q 3D 4D ~4Q 4Q VCC", "", "sn74hc175", ("74LS175",))
add("74HC273", "Octal D Flip-Flop with Clear", "Eight D flip-flops, common clock and clear.", P20,
    "~CLR 1Q 1D 2D 2Q 3Q 3D 4D 4Q GND CLK 5Q 5D 6D 6Q 7Q 7D 8D 8Q VCC", "", "sn74hc273", ("74LS273",))
add("74HC373", "Octal Transparent Latch, Tri-State", "Eight D latches with tri-state outputs.", P20,
    "~OE 1Q 1D 2D 2Q 3Q 3D 4D 4Q GND LE 5Q 5D 6D 6Q 7Q 7D 8D 8Q VCC", "", "sn74hc373", ("74LS373",))
add("74HC374", "Octal D Flip-Flop, Tri-State", "Eight edge-triggered D flip-flops with tri-state outputs.", P20,
    "~OE 1Q 1D 2D 2Q 3Q 3D 4D 4Q GND CLK 5Q 5D 6D 6Q 7Q 7D 8D 8Q VCC", "", "sn74hc374", ("74LS374",))
add("74HC377", "Octal D Flip-Flop with Enable", "Eight D flip-flops with clock enable.", P20,
    "~CLKEN 1Q 1D 2D 2Q 3Q 3D 4D 4Q GND CLK 5Q 5D 6D 6Q 7Q 7D 8D 8Q VCC", "", "sn74hc377", ("74LS377",))
add("74HC573", "Octal Transparent Latch, Tri-State (flow-through)", "Eight D latches with inputs and outputs on opposite sides.", P20,
    "~OE 1D 2D 3D 4D 5D 6D 7D 8D GND LE 8Q 7Q 6Q 5Q 4Q 3Q 2Q 1Q VCC", "", "sn74hc573a", ("74LS573",))
add("74HC574", "Octal D Flip-Flop, Tri-State (flow-through)", "Eight D flip-flops with inputs and outputs on opposite sides.", P20,
    "~OE 1D 2D 3D 4D 5D 6D 7D 8D GND CLK 8Q 7Q 6Q 5Q 4Q 3Q 2Q 1Q VCC", "", "sn74hc574", ("74LS574",))
add("74HC259", "8-Bit Addressable Latch", "Eight addressable latches with clear.", P16,
    "S0 S1 S2 Q0 Q1 Q2 Q3 GND Q4 Q5 Q6 Q7 D ~G ~CLR VCC", "", "sn74hc259", ("74LS259",))

# ---- decoders, encoders, multiplexers ----
add("74HC138", "3-to-8 Line Decoder", "Selects one of eight active-low outputs." + LG, P16,
    "A B C ~G2A ~G2B G1 ~Y7 GND ~Y6 ~Y5 ~Y4 ~Y3 ~Y2 ~Y1 ~Y0 VCC",
    "VCC=VCC;GND=GND;DEC138 A,B,C,G1,~G2A,~G2B>~Y0,~Y1,~Y2,~Y3,~Y4,~Y5,~Y6,~Y7", "sn74hc138", ("74LS138",), True)
add("74HC137", "3-to-8 Decoder with Address Latch", "Inverting 3-to-8 decoder with input latch.", P16,
    "A0 A1 A2 LE ~OE1 OE0 ~Y7 GND ~Y6 ~Y5 ~Y4 ~Y3 ~Y2 ~Y1 ~Y0 VCC", "", "sn74hc137", ("74LS137",))
add("74HC237", "3-to-8 Decoder with Address Latch, Active-High", "Non-inverting 3-to-8 decoder with input latch.", P16,
    "A0 A1 A2 LE ~OE1 OE0 Y7 GND Y6 Y5 Y4 Y3 Y2 Y1 Y0 VCC", "", "cd74hc237")
add("74HC238", "3-to-8 Decoder, Active-High Outputs", "Non-inverting 3-to-8 line decoder.", P16,
    "A B C ~G2A ~G2B G1 Y7 GND Y6 Y5 Y4 Y3 Y2 Y1 Y0 VCC", "", "cd74hc238")
add("74HC139", "Dual 2-to-4 Line Decoder", "Two 2-to-4 decoders with active-low outputs.", P16,
    "~1G 1A 1B ~1Y0 ~1Y1 ~1Y2 ~1Y3 GND ~2Y3 ~2Y2 ~2Y1 ~2Y0 2B 2A ~2G VCC", "", "sn74hc139", ("74LS139",))
add("74LS42", "BCD-to-Decimal Decoder", "Four-line BCD to ten active-low outputs.", P16,
    "~0 ~1 ~2 ~3 ~4 ~5 ~6 GND ~7 ~8 ~9 D C B A VCC", "", "sn74ls42", ("7442",))
add("74LS47", "BCD to 7-Segment Decoder/Driver (Open Collector)", "Drives a common-anode display: active-low open-collector outputs.", P16,
    "B C ~LT ~BI/RBO ~RBI D A GND ~e ~d ~c ~b ~a ~g ~f VCC", "", "sn74ls47", ("7446", "74LS247"))
add("74LS48", "BCD to 7-Segment Decoder (Pull-Up Outputs)", "Active-high outputs with internal pull-ups.", P16,
    "B C ~LT ~BI/RBO ~RBI D A GND e d c b a g f VCC", "", "sn74ls48", ("7448", "74LS248"))
add("74LS49", "BCD to 7-Segment Decoder (Open Collector, 14-pin)", "Active-high open-collector outputs, blanking input.", P14,
    "B C ~BI D A e GND d c b a g f VCC", "", "sn74ls47", ("74LS249",))
add("74HC151", "8-to-1 Multiplexer", "Selects one of eight inputs; true and complement outputs.", P16,
    "D3 D2 D1 D0 Y W ~G GND C B A D7 D6 D5 D4 VCC", "", "sn74hc151", ("74LS151",))
add("74HC153", "Dual 4-to-1 Multiplexer", "Two 4-input multiplexers with common select.", P16,
    "~1G B 1C3 1C2 1C1 1C0 1Y GND 2Y 2C0 2C1 2C2 2C3 A ~2G VCC", "", "sn74hc153", ("74LS153",))
add("74HC157", "Quad 2-to-1 Multiplexer", "Four 2-input multiplexers with common select and strobe.", P16,
    "A/~B 1A 1B 1Y 2A 2B 2Y GND 3Y 3B 3A 4Y 4B 4A ~G VCC", "", "sn74hc157", ("74LS157",))
add("74HC158", "Quad 2-to-1 Multiplexer, Inverting", "Four inverting 2-input multiplexers.", P16,
    "A/~B 1A 1B ~1Y 2A 2B ~2Y GND ~3Y 3B 3A ~4Y 4B 4A ~G VCC", "", "sn74hc158", ("74LS158",))
add("74HC251", "8-to-1 Multiplexer, Tri-State", "Eight-input multiplexer with tri-state outputs.", P16,
    "D3 D2 D1 D0 Y W ~OE GND C B A D7 D6 D5 D4 VCC", "", "sn74hc251", ("74LS251",))
add("74HC253", "Dual 4-to-1 Multiplexer, Tri-State", "Two 4-input multiplexers with tri-state outputs.", P16,
    "~1OE B 1C3 1C2 1C1 1C0 1Y GND 2Y 2C0 2C1 2C2 2C3 A ~2OE VCC", "", "sn74hc253", ("74LS253",))
add("74HC257", "Quad 2-to-1 Multiplexer, Tri-State", "Four 2-input multiplexers with tri-state outputs.", P16,
    "A/~B 1A 1B 1Y 2A 2B 2Y GND 3Y 3B 3A 4Y 4B 4A ~OE VCC", "", "sn74hc257", ("74LS257",))
add("74LS258", "Quad 2-to-1 Multiplexer, Inverting Tri-State", "Four inverting 2-input multiplexers, tri-state.", P16,
    "A/~B 1A 1B ~1Y 2A 2B ~2Y GND ~3Y 3B 3A ~4Y 4B 4A ~OE VCC", "", "sn74ls258b")
add("74HC85", "4-Bit Magnitude Comparator", "Compares two 4-bit words; cascadable.", P16,
    "B3 A<Bin A=Bin A>Bin A>Bout A=Bout A<Bout GND B0 A0 B1 A1 A2 B2 A3 VCC", "", "sn74hc85", ("74LS85",))
add("74HC688", "8-Bit Identity Comparator", "Active-low P=Q output when two bytes match.", P20,
    "~G P0 Q0 P1 Q1 P2 Q2 P3 Q3 GND P4 Q4 P5 Q5 P6 Q6 P7 Q7 ~P=Q VCC", "", "sn74hc688", ("74LS688",))
add("74HC283", "4-Bit Binary Full Adder", "Adds two 4-bit numbers with carry in and out.", P16,
    "S2 B2 A2 S1 A1 B1 C0 GND C4 S4 B4 A4 S3 A3 B3 VCC", "", "sn74hc283", ("74LS283",))
add("74HC280", "9-Bit Parity Generator/Checker", "Odd/even parity of nine inputs.", P14,
    "G H NC I EVEN ODD GND A B C D E F VCC", "", "sn74hc280", ("74LS280",))

# ---- counters, shift registers ----
add("74HC161", "4-Bit Synchronous Binary Counter", "Presettable counter with asynchronous clear.", P16,
    "~CLR CLK A B C D ENP GND ~LOAD ENT QD QC QB QA RCO VCC", "", "sn74hc161", ("74LS161",))
add("74HC163", "4-Bit Synchronous Binary Counter, Sync Clear", "Presettable counter with synchronous clear.", P16,
    "~CLR CLK A B C D ENP GND ~LOAD ENT QD QC QB QA RCO VCC", "", "sn74hc163")
add("74HC160", "Synchronous Decade Counter", "Presettable BCD counter with asynchronous clear.", P16,
    "~CLR CLK A B C D ENP GND ~LOAD ENT QD QC QB QA RCO VCC", "", "sn74hc160")
add("74HC164", "8-Bit Serial-In Parallel-Out Shift Register", "Serial in, eight parallel outputs, with clear.", P14,
    "A B QA QB QC QD GND CLK ~CLR QE QF QG QH VCC", "", "sn74hc164")
add("74HC165", "8-Bit Parallel-In Serial-Out Shift Register", "Loads eight inputs and shifts them out serially (button matrices).", P16,
    "SH/~LD CLK E F G H ~QH GND QH SER A B C D CLKINH VCC", "", "sn74hc165", ("74LS165",), True)
add("74HC166", "8-Bit Parallel-In Serial-Out Shift Register, Sync Load", "Parallel-load shift register with clear.", P16,
    "SER A B C D CLKINH CLK GND ~CLR E F G QH H SH/~LD VCC", "", "sn74hc166", ("74LS166",))
add("74HC190", "Synchronous Up/Down Decade Counter", "Presettable BCD up/down counter.", P16,
    "B QB QA ~CTEN D/~U QC QD GND D C ~LOAD MAX/MIN ~RCO CLK A VCC", "", "cd74hc190", ("74LS190",))
add("74HC191", "Synchronous Up/Down Binary Counter", "Presettable 4-bit up/down counter.", P16,
    "B QB QA ~CTEN D/~U QC QD GND D C ~LOAD MAX/MIN ~RCO CLK A VCC", "", "sn74hc191")
add("74HC192", "Synchronous Up/Down Decade Counter (Dual Clock)", "Presettable BCD counter with separate up and down clocks.", P16,
    "B QB QA DOWN UP QC QD GND D C ~LOAD ~CO ~BO CLR A VCC", "", "cd74hc192", ("74LS192",))
add("74HC193", "Synchronous Up/Down Binary Counter (Dual Clock)", "Presettable 4-bit counter with separate up and down clocks.", P16,
    "B QB QA DOWN UP QC QD GND D C ~LOAD ~CO ~BO CLR A VCC", "", "cd74hc193")
add("74HC194", "4-Bit Bidirectional Universal Shift Register", "Shift left/right, parallel load, hold.", P16,
    "~CLR SR A B C D SL GND S0 S1 CLK QD QC QB QA VCC", "", "sn74hc194", ("74LS194",))
add("74HC195", "4-Bit Parallel-Access Shift Register", "J-K̄ serial input, parallel load.", P16,
    "~CLR J ~K A B C D GND SH/~LD CLK ~QD QD QC QB QA VCC", "", "sn74hc195", ("74LS195",))
add("74HC299", "8-Bit Universal Shift Register, Tri-State", "Universal shift/storage register with tri-state I/O.", P20,
    "S0 ~OE1 ~OE2 G/QG E/QE C/QC A/QA QA' ~CLR GND SR CLK B/QB D/QD F/QF H/QH QH' SL S1 VCC", "", "sn74hc299", ("74LS299",))
add("74HC390", "Dual Decade Ripple Counter", "Two divide-by-2 and divide-by-5 counters.", P16,
    "1CKA 1CLR 1QA 1CKB 1QB 1QC 1QD GND 2QD 2QC 2QB 2CKB 2QA 2CLR 2CKA VCC", "", "sn74hc390", ("74LS390",))
add("74HC393", "Dual 4-Bit Binary Ripple Counter", "Two ripple counters, counting on the falling clock edge." + LG, P14,
    "1CLK 1CLR 1QA 1QB 1QC 1QD GND 2QD 2QC 2QB 2QA 2CLR 2CLK VCC",
    "VCC=VCC;GND=GND;CNT 1CLK,1CLR>1QA,1QB,1QC,1QD;CNT 2CLK,2CLR>2QA,2QB,2QC,2QD", "sn74hc393", ("74LS393",), True)
add("74HC595", "8-Bit Shift Register with Output Latches", "Serial-in, parallel-out shift register with output latch: drive eight LEDs from three pins." + LG, P16,
    "QB QC QD QE QF QG QH GND QH' ~SRCLR SRCLK RCLK ~OE SER QA VCC",
    "VCC=16;GND=8;SR595 14,11,12,10,13>15,1,2,3,4,5,6,7,9", "sn74hc595", ("74LS595",), True)
add("74HC594", "8-Bit Shift Register with Output Registers", "Shift register with separate clears for the shift and storage registers.", P16,
    "QB QC QD QE QF QG QH GND QH' ~SRCLR SRCLK RCLK ~RCLR SER QA VCC", "", "sn74hc594", ("74LS594",))

# ---- buffers and transceivers ----
OCT_BUF = "~1OE 1A1 2Y4 1A2 2Y3 1A3 2Y2 1A4 2Y1 GND 2A1 1Y4 2A2 1Y3 2A3 1Y2 2A4 1Y1 ~2OE VCC"
add("74HC240", "Octal Inverting Buffer, Tri-State", "Two groups of four inverting line drivers.", P20, OCT_BUF, "", "sn74hc240", ("74LS240", "74LS240_Split"))
add("74HC241", "Octal Buffer, Tri-State (OE and ~OE)", "Two groups of four buffers, one enable active high.", P20,
    OCT_BUF.replace("~2OE", "2OE"), "", "sn74hc241", ("74LS241", "74LS241_Split"))
add("74HC244", "Octal Buffer, Tri-State", "Two groups of four non-inverting line drivers.", P20, OCT_BUF, "", "sn74hc244", ("74LS244", "74LS244_Split"))
add("74HC245", "Octal Bus Transceiver, Tri-State", "Bidirectional 8-bit buffer; DIR sets the direction.", P20,
    "DIR A1 A2 A3 A4 A5 A6 A7 A8 GND B8 B7 B6 B5 B4 B3 B2 B1 ~OE VCC", "", "sn74hc245", ("74LS245",), True)
add("74HC540", "Octal Inverting Buffer, Tri-State (flow-through)", "Eight inverting buffers with inputs and outputs on opposite sides.", P20,
    "~OE1 A1 A2 A3 A4 A5 A6 A7 A8 GND Y8 Y7 Y6 Y5 Y4 Y3 Y2 Y1 ~OE2 VCC", "", "sn74hc540", ("74LS540",))
add("74HC541", "Octal Buffer, Tri-State (flow-through)", "Eight buffers with inputs and outputs on opposite sides.", P20,
    "~OE1 A1 A2 A3 A4 A5 A6 A7 A8 GND Y8 Y7 Y6 Y5 Y4 Y3 Y2 Y1 ~OE2 VCC", "", "sn74hc541", ("74LS541",))
add("74HC365", "Hex Buffer, Tri-State", "Six buffers with two common enables.", P16,
    "~G1 1A 1Y 2A 2Y 3A 3Y GND 4Y 4A 5Y 5A 6Y 6A ~G2 VCC", "", "sn74hc365", ("74LS365",))
add("74HC366", "Hex Inverting Buffer, Tri-State", "Six inverting buffers with two common enables.", P16,
    "~G1 1A ~1Y 2A ~2Y 3A ~3Y GND ~4Y 4A ~5Y 5A ~6Y 6A ~G2 VCC", "", "sn74hc366", ("74LS366",))
add("74HC367", "Hex Buffer, Tri-State (4 + 2)", "Six buffers in groups of four and two.", P16,
    "~1OE 1A1 1Y1 1A2 1Y2 1A3 1Y3 GND 1Y4 1A4 2Y1 2A1 2Y2 2A2 ~2OE VCC", "", "sn74hc367", ("74LS367",))
add("74HC368", "Hex Inverting Buffer, Tri-State (4 + 2)", "Six inverting buffers in groups of four and two.", P16,
    "~1OE 1A1 ~1Y1 1A2 ~1Y2 1A3 ~1Y3 GND ~1Y4 1A4 ~2Y1 2A1 ~2Y2 2A2 ~2OE VCC", "", "sn74hc368", ("74LS368",))

def write(out):
    o = ["# 74-series logic. Pinouts from the TI datasheets named in each block",
         "# (top view, all packages share the numbering). Generated by a script;",
         "# edit the blocks directly.", ""]
    for p in P:
        al = [a for a in kicad_aliases(p["block"], p["extra"]) if a != p["block"]]
        fam = re.sub(r"^74[A-Z]*", "", p["block"])
        for pre in ("74LS", "74HC", "74HCT", "74AC", "74ACT", "74F", "74ALS"):
            pass
        seen = []
        for a in al:
            if a not in seen: seen.append(a)
        o.append(f"[{p['block']}]")
        o.append(f"title     = {p['title']}")
        o.append("category  = Logic")
        o.append(f"desc      = {p['desc']}")
        o.append(f"datasheet = https://www.ti.com/lit/ds/symlink/{p['ti']}.pdf")
        o.append(f"package   = {p['pk']}")
        o.append(f"pins      = {p['pins']}")
        o.append("ids       = numbers")
        if p["replaces"]: o.append("replaces  = yes")
        if seen: o.append("aliases   = " + ", ".join(seen))
        if p["sim"]:
            o.append("sim       = logic")
            o.append(f"simParams = {p['sim']}")
        o.append("")
    open(out, "w", encoding="utf-8").write("\n".join(o))
    print(len(P), "blocks")

if __name__ == "__main__":
    write(sys.argv[1])
