Free, open-source electronics design for desktop and phone: breadboard, schematic and PCB views, live simulation, Gerber export, version history and an MCP server for AI assistants.

📖 **Documentation:** https://rorretbolt.github.io/designElectronics/ · 🌐 **Try it in your browser:** https://rorretbolt.github.io/designElectronics/#app

## What's new in 0.2.1

- **AI tab — build circuits by chatting.** Describe a circuit and the AI tool you already have (**Claude Code**; Codex and Gemini CLI experimental; or any custom command) builds it live in the open design: parts, breadboard placement, wiring, Arduino sketches and a simulation check. Every change is undoable, the agent may only use designElectronics' tools, and it runs on your own AI account.

## What's new in 0.2

- **Program your boards.** Write an Arduino sketch for an Uno, ESP32, Pico, STM32 and more. It runs in the simulator with the executing line highlighted and a Serial Monitor, and uploads to the real board through arduino-cli / Arduino IDE 2.
- **Videos and GIFs for social media.** File ▸ Export ▸ Video or GIF records the running circuit as Reels/Shorts/TikTok (9:16), Instagram posts (4:5), square or YouTube (16:9), including a tour through breadboard, schematic and PCB.
- **Embed circuits in blogs.** File ▸ Embed in a Website gives you an `<iframe>`; readers can switch views and run the simulation. The design travels inside the link — nothing is uploaded.
- **Web app.** The whole editor also runs in the browser.
- **Layers panel**, **version history with optional GitHub sync**, an **MCP server for AI assistants**, 20 more boards (Raspberry Pi 5/4/3/Zero, Arduino Mega/Due/Uno R4…), true-to-size parts and many simulator fixes.

## Download

| System | File |
| --- | --- |
| macOS 11+ (Apple Silicon and Intel) | `designElectronics-macOS.zip` |
| Windows 10/11 (64-bit) | `designElectronics-Windows-x64.zip` |
| Linux x86_64 (glibc 2.35+, X11 or Wayland) | `designElectronics-Linux-x86_64.tar.gz` |

## First launch

These builds aren't code-signed yet, so your system will ask before opening them the first time.

- **macOS:** unzip, move `designElectronics.app` to Applications, then **right-click ▸ Open ▸ Open**. If macOS says the app is damaged, run `xattr -dr com.apple.quarantine /Applications/designElectronics.app` once.
- **Windows:** unzip and run `designElectronics.exe`. If SmartScreen appears, click **More info ▸ Run anyway**. Keep the `assets` folder next to the exe.
- **Linux:** `tar xzf designElectronics-Linux-x86_64.tar.gz && ./designElectronics/designElectronics`.

Start with **File ▸ Examples** (555 blinker, ESP32 blink, Raspberry Pi LED…) or the [quick start](https://rorretbolt.github.io/designElectronics/#quickstart).

GitHub sync is available in the macOS and Linux builds; Windows builds have local version history only for now.
