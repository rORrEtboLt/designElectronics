// IC and discrete packages: PCB land patterns, breadboard bodies and pin order.
//
// A package is named the way datasheets name it, e.g. "SOIC-8", "SOIC-16W",
// "TSSOP-20", "QFN-32 5x5 P0.5 EP", "LQFP-64 10x10", "SOT-23-5", "SOT-223",
// "TO-220-3", "0805", "SMA", "HDR-2x5". Land patterns are computed from JEDEC
// outline dimensions with IPC-7351 nominal fillets (see packages.cpp), so the
// pads match what the manufacturers' recommended layouts show. Check them
// against your part's datasheet before ordering boards.
#pragma once
#include <string>
#include <vector>

#include "partlib.h"

namespace plib {

struct Package {
    std::string spec;      // as written
    std::string family;    // upper-case family, e.g. "SOIC", "QFN", "TO-220"
    int n = 0;             // leads (for BGA: balls)
    int extra = 0;         // exposed pad / tab pads, numbered after the leads
    bool tht = false;
    bool wide = false;     // "W" suffix: wide body (SOIC-16W, DIP-28W)
    float pitch = 0;       // mm
    float bodyX = 0, bodyY = 0;  // mm, along / across the pin rows
    float epX = 0, epY = 0;      // exposed pad size, mm
    int rows = 0;          // headers: 1 or 2 rows; BGA: rows
};

bool parsePackage(const std::string& spec, Package& out, std::string* err = nullptr);

// The package's pads in pad order. `num` is the pad number as datasheets print
// it ("1", "14", or a ball name like "C7" for BGA); `alt` is "EP" for an exposed
// pad or "TAB" for a tab. A tab carries the number of the lead it is joined to,
// so a pin can own two pads. BGA packages place only the balls listed.
struct PackagePad { std::string num, alt; };
std::vector<PackagePad> packagePads(const Package& p, const std::vector<std::string>& balls = {});

// Places d.pins (one per pad, in packagePads order) in the PCB and breadboard
// views and draws the silkscreen and breadboard body. `label` is printed on the
// body. `style` picks a breadboard look for 2-pin through-hole parts
// ("resistor", "diode", "capacitor", "ecap", "led", "inductor", "crystal").
void layoutPackage(PartDef& d, const Package& p, const std::string& label, const std::string& style = "",
                   const std::vector<std::string>& balls = {});

// Re-lays a built-in part on a package: pad i of the package becomes pin
// padPins[i] (-1 = a pad with no pin of its own, such as a tab, which gets an
// extra pin bused to the pin of the same pad number). Replaces the PCB view, and
// the breadboard view too for surface-mount packages (the part sits on a
// breakout board there). Returns false if the spec doesn't parse.
bool applyPackage(PartDef& d, const std::string& spec, const std::vector<int>& padPins, const std::string& label, const std::string& style = "");

// "0805 [SMD]" → "0805": strips the bracketed note the Inspector shows.
std::string packageSpecOf(const std::string& option);

// Every family parsePackage understands, for docs and the self-test.
const std::vector<std::string>& packageFamilies();

}  // namespace plib
