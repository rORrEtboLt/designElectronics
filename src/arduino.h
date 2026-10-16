// designElectronics — Arduino sketch interpreter.
//
// Runs the program stored on a microcontroller board inside the circuit
// simulator: setup() once, then loop() forever, in step with simulated time.
// The sketch is compiled to a small bytecode VM so it can pause at delay()
// (no threads needed, works in the browser too).
//
// Supported: the C/C++ subset typical Arduino sketches use (int/long/float/
// bool/byte/char/String variables, 1-D arrays, functions, if/for/while/do/
// switch, #define constants) and the core API: pinMode, digitalWrite/Read,
// analogRead/Write, delay, millis/micros, tone, shiftOut/In, map/constrain,
// math, random, bit helpers, Serial, and ESP32 ledc*. Calls on other library
// objects (WiFi, Wire, Servo, LCD…) are accepted and ignored with a warning —
// the full sketch still compiles and runs on the real board.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ard {

enum PinMode { PM_UNSET = 0, PM_INPUT, PM_OUTPUT, PM_INPUT_PULLUP, PM_INPUT_PULLDOWN };

struct PinState {
    int mode = PM_UNSET;
    double level = 0;   // outputs: 0..1 (fraction of the I/O voltage; PWM = duty)
    double toneHz = 0;  // tone() frequency, 0 = none
};

// What the interpreter needs to know about the board it runs on.
struct BoardInfo {
    std::string family = "avr";   // avr, esp32, esp8266, rp2040, stm32, samd, nrf52, renesas, sam, teensy
    double vref = 5;              // analogRead reference / I/O voltage
    int adcBits = 10;
    int intBits = 16;             // size of `int`
    long ledBuiltin = 13;
    std::map<std::string, long> constants;               // board pin constants (A0, D4, PC13…)
    std::function<int(long arduinoPin)> resolvePin;      // Arduino pin number -> part pin index (-1 = not on this board)
};

struct Host {
    // Voltage at a part pin (V vs GND); NaN when the pin isn't connected to anything.
    std::function<double(int partPin)> readVoltage;
};

struct CompileError {
    int line = 0;
    std::string message;
};

class Sketch {
public:
    Sketch();
    ~Sketch();
    Sketch(const Sketch&) = delete;
    Sketch& operator=(const Sketch&) = delete;

    bool compile(const std::string& source, const BoardInfo& board, CompileError& err);

    // Run until the sketch waits beyond `nowMs` (delay), or the per-millisecond
    // instruction budget is used up. Call once per simulated millisecond.
    void run(double nowMs, Host& host);

    bool running() const;
    const std::string& runtimeError() const;  // empty while healthy
    int runtimeLine() const;

    std::string takeSerial();                  // new Serial output since the last call
    void serialInput(const std::string& text);

    // Pin states keyed by part pin index.
    const std::map<int, PinState>& pins() const;
    const std::vector<std::string>& warnings() const;
    int currentLine() const;                   // line being executed (for the editor)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ard
