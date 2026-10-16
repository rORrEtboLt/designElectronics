# Building designElectronics

The same source builds for macOS, Windows, Linux, iOS and Android through
SDL3. Desktop builds can use system packages. Mobile builds compile SDL3 and
SDL3_ttf from source next to the app.

## Desktop

### macOS
```sh
brew install cmake sdl3 sdl3_ttf
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
open build/designElectronics.app
```
The bundle registers `.dez` and `.dezpart`, so double-clicking a project in
Finder opens it.

### Linux
Use your distribution's SDL3 packages if it has them; otherwise vendor SDL
(below). Then:
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
sudo cmake --install build      # installs the .desktop file + MIME type for .dez
```

### Windows (MSVC)
Install SDL3 and SDL3_ttf with vcpkg (`vcpkg install sdl3 sdl3-ttf`), or vendor them:
```bat
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

### Vendoring SDL (any platform)
```sh
git clone --depth 1 -b release-3.4.x https://github.com/libsdl-org/SDL.git external/SDL
git clone --depth 1 --recursive -b release-3.2.x https://github.com/libsdl-org/SDL_ttf.git external/SDL_ttf
cmake -S . -B build -DDE_VENDOR_SDL=ON && cmake --build build -j
```

## iOS / iPadOS

```sh
# vendor SDL first (see above)
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DDE_VENDOR_SDL=ON
open build-ios/designElectronics.xcodeproj
```
Pick your signing team in Xcode and run on a device or simulator.

* `platform/apple/Info.plist.in` turns on **File Sharing** and *open in place*,
  so saved `.dez` files show up in the Files app under *designElectronics*. From
  there you can share them through AirDrop, Messages, Mail and so on.
* Opening a `.dez` attachment from another app launches designElectronics. SDL
  delivers the file as a drop event and the app loads it.

## Android

SDL ships a ready-made Gradle project. Wrap the app like this:

```sh
# vendor SDL first (see above)
cp -r external/SDL/android-project android
cd android
ln -s ../.. app/jni/designElectronics          # this repo
ln -s ../../external/SDL app/jni/SDL
ln -s ../../external/SDL_ttf app/jni/SDL_ttf
mkdir -p app/src/main/assets/fonts && cp ../assets/fonts/* app/src/main/assets/fonts/
```

1. In `app/jni/CMakeLists.txt`, add the SDL, SDL_ttf and designElectronics
   subdirectories. Our CMake builds a `main` shared library when `ANDROID` is
   set, which is what `SDLActivity` loads.
2. In `app/build.gradle`, set `applicationId "org.designelectronics.app"`.
3. Merge `platform/android/AndroidManifest-snippet.xml` into
   `app/src/main/AndroidManifest.xml` so `.dez` files open in the app.
4. `./gradlew installDebug`

On Android the system file picker handles **Save / Share** and **Open**
(SDL3 dialogs return `content://` URIs, which the app reads and writes
directly). Saving to Google Drive, sending through a messenger and so on work
from there.

## Command-line options

```
designElectronics [project.dez]
designElectronics --selftest                 # headless tests, non-zero exit on failure
designElectronics --export-examples DIR      # write built-in examples as .dez
designElectronics --screenshot out.png [--example N] [--view bb|sch|pcb] [--sim]
                  [--autoroute] [--size WxH] [--drawer parts|inspector] [--select N]
                  [--events "frame:d|m|u:x:y,…"]   # scripted pointer events for UI tests
```
