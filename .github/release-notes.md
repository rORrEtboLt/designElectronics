Free, open-source electronics design for desktop and phone: breadboard, schematic and PCB views, live simulation, Gerber export, version history and an MCP server for AI assistants.

📖 **Documentation:** https://rorretbolt.github.io/designElectronics/

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
