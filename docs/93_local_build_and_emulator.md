# Local Build & Emulator Testing

This guide covers building the menu ROM **without** the full VSCode dev container, and
running/testing it in an emulator for fast iteration. It complements
[99_developer_guide.md](99_developer_guide.md), which covers hardware deploy/debug.

> The menu compiles into a single N64 ROM. Most flashcart features (real save handling,
> the actual game-boot handoff, SC64-specific paths) only work on hardware. The emulator
> is for fast iteration on **menu UI, navigation, and logic**; always do final validation
> on real hardware.

---

## 1. Build the ROM with Docker (no local toolchain)

The project's dev container installs the libdragon GCC toolchain via a prebuilt `.deb`.
You can reuse that image to build from the command line.

### One-time image setup

```bash
# Base toolchain image (libdragon GCC .deb + sc64deployer)
docker build -t n64menu-build \
  -f .devcontainer/flashcart/Dockerfile.sc64deployer .devcontainer/flashcart

# Derived image with libdragon pre-built & installed (so ROM builds are fast)
docker build -t n64menu-dev \
  -f .devcontainer/flashcart/Dockerfile.dev .
```

### Building

```bash
./dev_build.sh            # FLAGS=-DNDEBUG, outputs to output/
./dev_build.sh clean      # make clean
FLAGS="-DFEATURE_AUTOLOAD_ROM_ENABLED" ./dev_build.sh
```

`dev_build.sh` just runs `make` inside the `n64menu-dev` container with
`N64_INST=/opt/libdragon`. Outputs land in `output/` (same as a native build):
`N64FlashcartMenu.n64`, `sc64menu.n64`, `menu.bin`, `OS64.v64`, `OS64P.v64`.

> If you change the libdragon submodule, rebuild the `n64menu-dev` image so the
> installed toolchain matches.

---

## 2. Run in an emulator

`output/N64FlashcartMenu.n64` is a plain N64 ROM and boots in N64 emulators. Without a
flashcart, cart-specific features are unavailable, but the menu itself (browser, settings,
navigation, history view, etc.) runs — which is exactly what you want for UI iteration.

### Ares (recommended)

[Ares](https://ares-emu.net/) is highly accurate and the project's reference emulator.

GUI:
1. Open Ares, System → Nintendo 64.
2. Load `output/N64FlashcartMenu.n64`.

Command line (handy for a build→run loop):

```bash
# Linux/macOS (adjust binary name/path to your install)
ares --system "Nintendo 64" output/N64FlashcartMenu.n64
```

To present an SD-card-like filesystem to the menu (so the browser shows ROMs), Ares does
not emulate the SC64 SD interface — see the limitation note below.

### gopher64

[gopher64](https://github.com/gopher64/gopher64) also runs the ROM:

```bash
gopher64 output/N64FlashcartMenu.n64
```

### Limitation: no SD card / flashcart in stock emulators

Stock Ares/gopher64 do **not** emulate the SC64's SD card, so the file browser will be
empty and you can't exercise real ROM loading or the history/last-played boot path
end-to-end in the emulator. The emulator is still useful for:

- Menu navigation, layout, fonts, colors, input handling.
- Settings editor screens and toggles.
- Anything that doesn't require reading the SD card.

For features that depend on SD contents or the boot handoff (including **quick-boot /
auto-boot last played**), validate on hardware with `make run-debug` (see the developer
guide). A debug build enables `debugf()` serial output, which prints to the deployer
console and is the primary way to confirm logic on hardware.

---

## 3. A fast iteration loop

```bash
# 1. edit src/menu/...
# 2. build
./dev_build.sh
# 3. quick smoke test in emulator (UI only)
ares --system "Nintendo 64" output/N64FlashcartMenu.n64
# 4. when UI looks right, validate on hardware
make run-debug        # requires sc64deployer server running on the host
```
