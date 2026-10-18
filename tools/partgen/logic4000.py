#!/usr/bin/env python3
"""Generates parts/logic_4000.txt: CD4000-series CMOS logic (3-18 V). Pinouts from
the TI CD4000B datasheets named in each block; verified with verify_ti.py and page images."""
import csv, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
rows = list(csv.DictReader(open(os.environ.get("KICAD_CHECKLIST", os.path.join(HERE, "..", "..", "build", "kicad_checklist.tsv")), encoding="utf-8"), delimiter="\t"))
LIBS = {"4xxx", "4xxx_IEEE"}
def kicad_aliases(num, extra=()):
    keys = {num, *extra}
    out = []
    for r in rows:
        if r["library"] in LIBS and (r["symbol"] in keys or r["base"] in keys) and r["symbol"] not in out: out.append(r["symbol"])
    return out
P14 = "DIP-14, SOIC-14, TSSOP-14"
P16 = "DIP-16, SOIC-16, TSSOP-16"
P24 = "DIP-24 W, SOIC-24W"
QUAD = "A B J K C D VSS E F L M G H VDD"           # J = f(A,B), K = f(C,D), L = f(E,F), M = f(G,H)
DUAL4 = "J A B C D NC VSS NC E F G H K VDD"        # J = f(A,B,C,D), K = f(E,F,G,H)
TRIPLE = "A B D E F K VSS C J L I H G VDD"         # J = f(A,B,C), K = f(D,E,F), L = f(G,H,I)
HEX = "A G B H C I VSS J D K E L F VDD"            # G = /A ...
LG = " Simulated as digital logic."
def quad(g): return f"VCC=VDD;GND=VSS;ROUT=300;{g} A,B>J;{g} C,D>K;{g} E,F>L;{g} G,H>M"
def hexg(g): return "VCC=VDD;GND=VSS;ROUT=300;" + ";".join(f"{g} {a}>{y}" for a, y in zip("ABCDEF", "GHIJKL"))
def dual4(g): return f"VCC=VDD;GND=VSS;ROUT=300;{g} A,B,C,D>J;{g} E,F,G,H>K"
P = []
def add(num, title, desc, pk, pins, sim="", ti=None, extra=(), replaces=False, block=None):
    P.append(dict(num=num, block=block or "CD" + num, title=title, desc=desc, pk=pk, pins=pins, sim=sim, ti=ti or f"cd{num}b", extra=extra, replaces=replaces))

add("4001", "Quad 2-Input NOR", "CMOS quad NOR, 3-18 V." + LG, P14, QUAD, quad("NOR"), replaces=True)
add("4011", "Quad 2-Input NAND", "CMOS quad NAND, 3-18 V." + LG, P14, QUAD, quad("NAND"), replaces=True)
add("4070", "Quad 2-Input XOR", "CMOS quad exclusive-OR." + LG, P14, QUAD, quad("XOR"), replaces=True)
add("4071", "Quad 2-Input OR", "CMOS quad OR." + LG, P14, QUAD, quad("OR"), replaces=True)
add("4077", "Quad 2-Input XNOR", "CMOS quad exclusive-NOR." + LG, P14, QUAD, quad("XNOR"))
add("4081", "Quad 2-Input AND", "CMOS quad AND." + LG, P14, QUAD, quad("AND"), replaces=True)
add("4093", "Quad 2-Input Schmitt NAND", "Schmitt-trigger NAND gates for oscillators and debouncing." + LG, P14, QUAD, quad("SNAND"), replaces=True,
    extra=("HEF4093B",))
add("4002", "Dual 4-Input NOR", "CMOS dual 4-input NOR." + LG, P14, DUAL4, dual4("NOR"))
add("4012", "Dual 4-Input NAND", "CMOS dual 4-input NAND." + LG, P14, DUAL4, dual4("NAND"))
add("4072", "Dual 4-Input OR", "CMOS dual 4-input OR." + LG, P14, DUAL4, dual4("OR"))
add("4082", "Dual 4-Input AND", "CMOS dual 4-input AND." + LG, P14, DUAL4, dual4("AND"))
add("4069", "Hex Inverter", "CMOS hex inverter." + LG, P14, HEX, hexg("NOT"), ti="cd4069ub", replaces=True)
add("40106", "Hex Schmitt Inverter", "Six Schmitt-trigger inverters." + LG, P14, HEX, hexg("SNOT"), extra=("4584",))
add("4049", "Hex Inverting Buffer/Converter", "Inverting buffers that also shift 15 V logic down to 5 V.", P16,
    "VCC G A H B I C VSS D J E K NC F L NC", "", ti="cd4049ub")
add("4050", "Hex Buffer/Converter", "Non-inverting buffers that shift 15 V logic down to 5 V.", P16,
    "VCC G A H B I C VSS D J E K NC F L NC", "")
add("4013", "Dual D Flip-Flop", "CMOS dual D flip-flop with active-high set/reset." + LG, P14,
    "Q1 ~Q1 CLK1 RESET1 D1 SET1 VSS SET2 D2 RESET2 CLK2 ~Q2 Q2 VDD",
    "VCC=VDD;GND=VSS;ROUT=300;DFFH D1,CLK1,SET1,RESET1>Q1,~Q1;DFFH D2,CLK2,SET2,RESET2>Q2,~Q2", replaces=True)
add("4027", "Dual J-K Flip-Flop", "CMOS dual J-K master-slave flip-flop with set/reset.", P16,
    "Q2 ~Q2 CLK2 RESET2 K2 J2 SET2 VSS SET1 J1 K1 RESET1 CLK1 ~Q1 Q1 VDD", "")
add("4017", "Decade Counter/Divider", "Lights ten outputs one after another, the classic LED chaser with a 555." + LG, P16,
    "Q5 Q1 Q0 Q2 Q6 Q7 Q3 VSS Q8 Q4 Q9 CO ~CLKINH CLK RST VDD",
    "VCC=16;GND=8;ROUT=300;J4017 14,13,15>3,2,4,7,10,1,5,6,9,11,12", replaces=True)
add("4022", "Divide-by-8 Counter/Divider", "Octal counter with eight decoded outputs.", P16,
    "Q1 Q0 Q2 Q5 Q6 NC Q3 VSS NC Q7 Q4 CO ~CLKINH CLK RST VDD", "")
add("4020", "14-Stage Binary Ripple Counter", "Divides by up to 16384.", P16,
    "Q12 Q13 Q14 Q6 Q5 Q7 Q4 VSS Q1 CLK RST Q9 Q8 Q10 Q11 VDD", "")
add("4024", "7-Stage Binary Ripple Counter", "Divides by up to 128.", P14,
    "CLK RST Q7 Q6 Q5 Q4 VSS NC Q3 NC Q2 Q1 NC VDD", "")
add("4040", "12-Stage Binary Ripple Counter", "Divides by up to 4096.", P16,
    "Q12 Q6 Q5 Q7 Q4 Q3 Q2 VSS Q1 CLK RST Q9 Q8 Q10 Q11 VDD", "")
add("4060", "14-Stage Ripple Counter with Oscillator", "Counter with a built-in RC or crystal oscillator.", P16,
    "Q12 Q13 Q14 Q6 Q5 Q7 Q4 VSS PHI0 ~PHI0 PHI1 RST Q9 Q8 Q10 VDD", "")
add("4028", "BCD-to-Decimal Decoder", "Four-bit BCD to ten active-high outputs.", P16,
    "Q4 Q2 Q0 Q7 Q9 Q5 Q6 VSS Q8 A D C B Q1 Q3 VDD", "")
add("4029", "Presettable Up/Down Counter", "Binary or BCD up/down counter.", P16,
    "PE Q4 J4 J1 ~CI Q1 ~CO VSS B/~D U/~D Q2 J2 J3 Q3 CLK VDD", "")
add("4046", "Phase-Locked Loop", "Micropower PLL with VCO and two phase comparators.", P16,
    "PP PC1OUT COMPIN VCOOUT INH C1A C1B VSS VCOIN SFOUT R1 R2 PC2OUT SIGIN ZENER VDD", "")
add("4047", "Monostable/Astable Multivibrator", "Low-power monostable or astable multivibrator.", P14,
    "C R RC ~AST AST -TRIG VSS +TRIG RST Q ~Q RETRIG OSC VDD", "")
add("4051", "8-Channel Analog Multiplexer", "Single 8-channel analog mux/demux (needs VEE for negative signals). Simulated.", P16,
    "CH4 CH6 COM CH7 CH5 INH VEE VSS C B A CH3 CH0 CH1 CH2 VDD", "VCC=VDD;GND=VSS;MUX A,B,C,INH,COM>CH0,CH1,CH2,CH3,CH4,CH5,CH6,CH7")
add("4052", "Dual 4-Channel Analog Multiplexer", "Differential 4-channel analog mux/demux.", P16,
    "Y0 Y2 Y Y3 Y1 INH VEE VSS B A X3 X0 X X1 X2 VDD", "")
add("4053", "Triple 2-Channel Analog Multiplexer", "Three single-pole double-throw analog switches.", P16,
    "BY BX CY C CX INH VEE VSS SC SB SA AX AY A B VDD", "")
add("4016", "Quad Bilateral Switch", "Four analog switches (4016: higher on-resistance than the 4066). Simulated.", P14,
    "1A 1B 2B 2A 2C 3C VSS 3A 3B 4B 4A 4C 1C VDD", "VCC=VDD;GND=VSS;SW 1C>1A,1B;SW 2C>2A,2B;SW 3C>3A,3B;SW 4C>4A,4B")
add("4066", "Quad Bilateral Switch", "Four analog switches controlled by logic inputs. Simulated.", P14,
    "1A 1B 2B 2A 2C 3C VSS 3A 3B 4B 4A 4C 1C VDD", "VCC=VDD;GND=VSS;SW 1C>1A,1B;SW 2C>2A,2B;SW 3C>3A,3B;SW 4C>4A,4B", replaces=True)
add("4094", "8-Stage Shift-and-Store Bus Register", "Serial-in shift register with latched tri-state outputs.", P16,
    "STROBE DATA CLK Q1 Q2 Q3 Q4 VSS QS ~QS Q8 Q7 Q6 Q5 OE VDD", "", extra=("HEF4094B",))
add("4518", "Dual BCD Up Counter", "Two synchronous decade counters.", P16,
    "CLKA ENA Q1A Q2A Q3A Q4A RSTA VSS CLKB ENB Q1B Q2B Q3B Q4B RSTB VDD", "")
add("4520", "Dual Binary Up Counter", "Two synchronous 4-bit counters.", P16,
    "CLKA ENA Q1A Q2A Q3A Q4A RSTA VSS CLKB ENB Q1B Q2B Q3B Q4B RSTB VDD", "")
add("4511", "BCD to 7-Segment Latch/Decoder/Driver", "Turns a 4-bit number into 7-segment outputs for a common-cathode display." + LG, P16,
    "B C ~LT ~BL LE D A VSS e d c b a g f VDD",
    "VCC=16;GND=8;ROUT=200;BCD7 7,1,2,6,3,4,5>13,12,11,10,9,15,14", replaces=True)
add("4538", "Dual Precision Monostable", "Two retriggerable, resettable one-shots.", P16,
    "C1 RC1 ~RST1 +TR1 -TR1 Q1 ~Q1 VSS ~Q2 Q2 -TR2 +TR2 ~RST2 RC2 C2 VDD", "", extra=("14528", "14538", "4528"), ti="cd14538b")
add("4543", "BCD to 7-Segment Latch/Decoder/LCD Driver", "Drives LCD, LED or fluorescent 7-segment displays.", P16,
    "LD C B D A PH BI VSS a b c d e g f VDD", "")
add("4021", "8-Stage Parallel-In Shift Register", "Parallel or serial in, serial out.", P16,
    "P8 Q6 Q8 P4 P3 P2 P1 VSS P/~S CLK SER Q7 P5 P6 P7 VDD", "")
add("4009", "Hex Inverting Buffer/Converter (two supplies)", "Older hex converter with separate VCC and VDD.", P16,
    "VCC G A H B I C VSS D J E K NC F L VDD", "", ti="cd4009ub")
add("4010", "Hex Buffer/Converter (two supplies)", "Older non-inverting hex converter with separate VCC and VDD.", P16,
    "VCC G A H B I C VSS D J E K NC F L VDD", "", ti="cd4010b")
add("4023", "Triple 3-Input NAND", "CMOS triple 3-input NAND." + LG, P14, TRIPLE, "")
add("4025", "Triple 3-Input NOR", "CMOS triple 3-input NOR.", P14, TRIPLE, "")
add("4073", "Triple 3-Input AND", "CMOS triple 3-input AND.", P14, TRIPLE, "")
add("4075", "Triple 3-Input OR", "CMOS triple 3-input OR.", P14, TRIPLE, "")
add("4098", "Dual Monostable", "Dual retriggerable one-shot.", P16,
    "C1 RC1 ~RST1 +TR1 -TR1 Q1 ~Q1 VSS ~Q2 Q2 -TR2 +TR2 ~RST2 RC2 C2 VDD", "", ti="cd4098b")
add("4510", "BCD Up/Down Counter", "Presettable BCD up/down counter.", P16,
    "PE Q4 P4 P1 ~CI Q1 ~CO VSS RST U/~D Q2 P2 P3 Q3 CLK VDD", "")
add("4516", "Binary Up/Down Counter", "Presettable 4-bit up/down counter.", P16,
    "PE Q4 P4 P1 ~CI Q1 ~CO VSS RST U/~D Q2 P2 P3 Q3 CLK VDD", "")
add("4504", "Hex Voltage Level Shifter", "Shifts TTL or CMOS levels up to VDD.", P16,
    "VCC AOUT AIN BOUT BIN COUT CIN VSS DIN DOUT EIN EOUT MODE FIN FOUT VDD", "")

def write(out):
    o = ["# CD4000-series CMOS logic (3-18 V). Pinouts from the TI CD4000B datasheets",
         "# named in each block. Generated by a script; edit the blocks directly.", ""]
    for p in P:
        al = [a for a in kicad_aliases(p["num"], p["extra"]) if a != p["block"]]
        for x in (p["num"], "HEF" + p["num"] + "B", "MC1" + p["num"][1:] + "B" if p["num"].startswith("4") and len(p["num"]) == 4 else None):
            if x and x not in al and x != p["block"]: al.append(x)
        o += [f"[{p['block']}]", f"title     = {p['title']}", "category  = Logic", f"desc      = {p['desc']}",
              f"datasheet = https://www.ti.com/lit/ds/symlink/{p['ti']}.pdf", f"package   = {p['pk']}", f"pins      = {p['pins']}", "ids       = numbers"]
        if p["replaces"]: o.append("replaces  = yes")
        if p["replaces"]: o.append(f"type      = cd{p['num']}")
        if al: o.append("aliases   = " + ", ".join(al))
        if p["sim"]: o += [f"sim       = {'logic'}", f"simParams = {p['sim']}"]
        o.append("")
    open(out, "w", encoding="utf-8").write("\n".join(o)); print(len(P), "blocks")
if __name__ == "__main__": write(sys.argv[1])
