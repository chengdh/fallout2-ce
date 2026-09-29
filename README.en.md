# Fallout 2 Community Edition (fork: pipboy-server)

> English version. 中文版见 [README.md](README.md).

> This repository is a **personal modified build** of
> **[alexbatalov/fallout2-ce](https://github.com/alexbatalov/fallout2-ce)** (branch
> `pipboy-server`). On top of the original "Fallout 2 Community Edition" engine it adds
> three features: **(a) a Pip-Boy link server**, **(b) native gamepad input + overlay +
> on-screen keyboard**, and **(c) a TrueType font backend**. All other engine behavior and
> gameplay match upstream. The full upstream install/build instructions remain valid; this
> document focuses on the branch's additions and usage.

The upstream README's generic installation (drop-in executables for Windows/Linux/macOS/
Android/iOS, and the `fallout2.cfg` / `f2_res.ini` / `ddraw.ini` configuration) still
applies. Below we give only the **build essentials for the branch's additions** plus the
branch's unique config options.

---

## 1. Project Background

Fallout 2 Community Edition makes the game run (mostly) hassle-free on multiple platforms.
To get the "game on the main screen, Pip-Boy terminal on the secondary screen" experience
on a dual-screen handheld, this branch adds an in-process **localhost TCP server** that
pushes game state (S.P.E.C.I.A.L., skills, items, status, map, quests, perks, etc.) as
protocol frames to the companion [`pipboy-client`](https://github.com/chengdh/pipboy-client)
app.

Because the original mouse-centric controls are awkward on touch/gamepad, the branch also
adds:
- **Native SDL gamepad input** with a Fallout-style reticle/cursor overlay and on-screen
  keyboard, so the handheld is playable with a controller;
- a **TrueType font backend** (FreeType + iconv) that replaces the built-in bitmap fonts
  and supports resources such as Chinese.

---

## 2. Open-source projects referenced

- **[alexbatalov/fallout2-ce](https://github.com/alexbatalov/fallout2-ce)**: upstream engine;
  all game logic and the build system here are layered on top of it.
- **SDL2 / SDL_GameController**: basis for gamepad input and cross-platform window/events
  (vendored by default).
- **FreeType + iconv**: TrueType font rendering and 8-bit encoding (GBK) → Unicode conversion.
- **[SDL_GameControllerDB](https://github.com/gabomdq/SDL_GameControllerDB)**: gamepad mapping
  database (`third_party/controller-db/gamecontrollerdb.txt`, copied next to the executable).
- **Zpix pixel font**: the 12×12 subset used by the gamepad overlay for self-drawn Chinese at
  small sizes (`src/gamepad_cjk.h`, generated from the Chinese strings by
  `tools/gen_gamepad_cjk.py`).
- **Fallout 4 companion app protocol**: the Pip-Boy Link frame format (length prefix + type
  byte + payload) is borrowed from the F4 companion app, but the payload is JSON (see
  `PIPBOY.md`).

---

## 3. Installation Guide

### 3.1 Build (including the branch's additions)

The engine uses **CMake** (C++17). Dependencies are **vendored** by default, building
zlib / SDL2 / FreeType / iconv from `third_party/`; non-vendored builds require system
`SDL2 >= 2.26`.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- **Platforms**: Windows (`WIN32`), Linux, macOS (universal `x86_64;arm64`, deployment
  target 10.13), **Android** (built as a `SHARED` library, packaged by the
  `pipboy-client` Android shell), **iOS** (arm64 bundle).
- **Gamepad switches**: `FALLOUT_BUILD_CONTROLLER_TESTS`, `FALLOUT_CONTROLLER_SMOKE_DRIVER`
  (includes `tests/gamepad_test.cc` smoke test).
- **TrueType fonts**: require FreeType + iconv (vendored).

> To just play (no build): download the build for your platform from this repo's **Release**
> (Windows `fallout2-ce-windows-<arch>.zip`, macOS `Fallout II Community Edition.dmg`,
> Linux `fallout2-ce-linux-<arch>.tar.gz`, Android `fallout2-ce-android.apk`,
> iOS `fallout2-ce-ios.ipa`), then follow **3.2 Install & run per platform** below to drop the
> original assets and fonts into the right directory. **You must own a legitimate copy of
> *Fallout 2* to play** (GOG / Steam / Epic, etc.).

### 3.1.1 Automatic build (CI)
The repo ships several GitHub Actions workflows:
- **`build-android.yml`**: builds the Android APK on every `push` (any branch) and
  `pull_request`. It uses `android-actions/setup-android` to install the exact NDK
  `23.2.8568313` / `build-tools;32.0.0` / `cmake;3.22.1` pinned in
  `os/android/app/build.gradle`; the APK is uploaded as a downloadable artifact.
- **`ci-build.yml`**: runs the full multi-platform build (static analysis, format check,
  Android / iOS / Windows / Linux / macOS) only on `push` to `main`. Its Android job needs
  an NDK environment configured separately.
- **`build-and-release.yml`**: on `push` to `main` / `pipboy-server` it creates a GitHub
  Release and uploads each platform's artifact (every push yields a timestamped release).

### 3.2 Install & run per platform (original assets / fonts placement)

The engine treats the **working directory** as its only data root at runtime: every
`master.dat`, `critter.dat`, `patch000.dat`, `data/`, `fallout2.cfg`, `fonts/`, and
`gamecontrollerdb.txt` is resolved **relative to it** (on Windows it's the executable's
directory; on macOS / iOS / Android the engine `chdir`s there at startup; on Linux it's the
current directory when you launch). So the core steps are identical on every platform:

1. Install the main program (see below — differs per platform).
2. Copy your legitimate *Fallout 2* `master.dat`, `critter.dat`, `patch000.dat` and the `data/`
   directory into that platform's **game data directory** (below).
3. Copy this repo's / Release's `fonts/` directory (it contains `fonts/english/` and
   `fonts/chs/`) into the **same directory**.
4. On first launch a `fallout2.cfg` is generated in the data directory — edit it as needed
   (see 3.3 to enable the Pip-Boy service).
5. Optional: copy `third_party/controller-db/gamecontrollerdb.txt` into the same directory to
   enable gamepad support (see 4.1).

Below, each platform says where the main program lives, where the data directory is, and where
to put the assets/fonts.

#### Windows
- **Main program**: unpack `fallout2-ce-windows-<arch>.zip` anywhere to get `fallout2-ce.exe`
  (SDL2 / FreeType / libiconv are statically linked in — a single self-contained file, no
  runtime to install).
- **Assets / fonts**: put `master.dat`, `critter.dat`, `patch000.dat`, `data/` and `fonts/`
  **right next to `fallout2-ce.exe`** (the unpack directory).
- Double-click `fallout2-ce.exe`; everything resolves against the exe directory.

#### macOS
- **Main program**: mount `Fallout II Community Edition.dmg` and drag
  `Fallout II Community Edition.app` into Applications. At startup the engine `chdir`s to
  `…/Fallout II Community Edition.app/Contents/MacOS/`, so assets go there.
- **Assets / fonts**: right-click the `.app` → Show Package Contents → open `Contents/MacOS/`,
  and put `master.dat`, `critter.dat`, `patch000.dat`, `data/`, `fonts/` inside that `MacOS`
  folder.
- Then launch the `.app` normally from Applications.

#### Linux
- **Main program**: unpack `fallout2-ce-linux-<arch>.tar.gz` anywhere to get the executable
  `fallout2-ce` (dynamically links system SDL2 / zlib — install first:
  `sudo apt install libsdl2-2.0-0 zlib1g`).
- **Data directory / launch**: make a game directory (e.g. `~/fallout2/`), put the assets in
  it, and launch **from inside it** — on Linux the working directory is the current directory
  at launch:
  ```bash
  mkdir -p ~/fallout2 && cd ~/fallout2
  # put master.dat critter.dat patch000.dat data/ fonts/ into ~/fallout2/
  ~/path/to/fallout2-ce   # run from the ~/fallout2 current directory
  ```
- **Assets / fonts**: everything goes into the current directory at launch (`~/fallout2/`).

#### Android
- **Main program**: sideload `fallout2-ce-android.apk` via `adb install -r …` or a file manager.
- **Data directory**: at startup the engine `chdir`s to the app's external-storage directory
  `/sdcard/Android/data/com.alexbatalov.fallout2ce/files/`
  (Debug package: `…com.alexbatalov.fallout2ce.debug/files/`).
- **Assets / fonts**: use `adb push` or a file manager to put `master.dat`, `critter.dat`,
  `patch000.dat`, `data/`, `fonts/` into that `files/` directory, e.g.:
  ```bash
  adb push master.dat critter.dat patch000.dat \
    /sdcard/Android/data/com.alexbatalov.fallout2ce/files/
  adb push data fonts \
    /sdcard/Android/data/com.alexbatalov.fallout2ce/files/
  ```
- Launch the app to play; no root required.

#### iOS
- **Main program**: sideload `fallout2-ce-ios.ipa` onto the device (AltStore / Sideloadly /
  enterprise signing, etc.).
- **Data directory**: at startup the engine `chdir`s to the app's **Documents** directory;
  copy assets in via File Sharing (macOS Finder "File Sharing" / iTunes / third-party tools).
- **Assets / fonts**: put `master.dat`, `critter.dat`, `patch000.dat`, `data/`, `fonts/` into
  that app's Documents directory.
- Launch the app to play.

### 3.3 Enable the Pip-Boy link server

Edit `fallout2.cfg` in the game directory and add/confirm the `[pipboy]` section:

```ini
[pipboy]
enabled=1
bind_address=127.0.0.1
port=27000
sample_interval_ms=250
```

- `enabled`: whether the service starts by default (see the "Notes" section about the
  TEST BUILD default).
- `bind_address`: `127.0.0.1` / `localhost` / empty = loopback only; any other value binds
  `0.0.0.0` and exposes it to the LAN (security risk).
- `sample_interval_ms`: sampling interval, default 250ms; first a full snapshot, then only
  changed keys.

You can also press **Ctrl+P** at runtime to toggle it instantly (a Chinese toast shows the
current state).

---

## 4. Usage

### 4.1 Gamepad usage

This branch adds native gamepad support based on **SDL_GameController** (see `CONTROLLER.md`).
The gamepad mapping database ships with the executable (`gamecontrollerdb.txt`).

**Default button bindings** (`src/gamepad.cc` `defaults()`):

| Button | Function |
|---|---|
| A / Cross | Left click / drag |
| B / Circle | Right click / cursor mode |
| X / Square | Enter (confirm) |
| Y / Triangle | Open Actions panel |
| Back / Share / Minus | Control panel |
| Start | Esc (close/menu) |
| LB (hold) | Run |
| RB | Open inventory (INVENTORY) |
| L3 (left stick press) | Toggle "character move / point-and-click" mode |
| R3 (right stick press) | On-screen keyboard |

There is also a 27-entry action map binding buttons to original on-screen hotkeys, e.g.
PIP-BOY→`P`, INVENTORY→`I`, SKILLDEX→`S`, AUTOMAP→`TAB`, SAVE→`F4`, etc.

**Movement & camera**:
- Left stick drives character movement directly (partial tilt walks, full tilt runs, hex-step
  animation, camera follows).
- Right stick smoothly pans the world camera and takes priority over follow.
- World input is enabled **only during player input polling** — it never touches enemy AI or UI.

**Customization** (`controller.ini`, local directory first, else SDL prefs dir `FalloutCE/`):
you can save `speed / deadzone / precision / scroll_speed / invert_scroll / swap_sticks /
labels / hints / borderless / direct_movement`, plus 9 rebindable buttons `button_N=<action>`.
`Back` is always reserved for restoring defaults.

**Overlay localization**: the gamepad reticle/hint overlay follows `[system] language` in
`fallout2.cfg`; only **English and Chinese** are translated. Chinese uses an embedded Zpix
12×12 pixel-font subset drawn directly (regular fonts are unreadable at small sizes).

### 4.2 Multi-language configuration guide & limitations

**Config switch**: the primary language is controlled by `[system] language` in
`fallout2.cfg` (default `english`). This value simultaneously drives two things:
1. Text lookup: `text\<language>\*.msg`;
2. Font lookup: `fonts/<language>/font.ini`.

Android asset example (`os/android/.../f2extra/fallout2.cfg`) sets `language=chs`.

**TrueType font backend (branch addition)**: uses FreeType to render interface fonts from
`.ttf/.otf/.ttc`, replacing the built-in bitmap `.aaf`. Startup order: claim the interface
font range `100..104` first, fall back to built-in `.aaf` only on failure. **There is no
runtime switch — the presence of `font.ini` is the switch.**

**Font lookup order**: `fonts/<language>/font.ini` → `fonts/font.ini` (shared layout used by
the Chinese community patch) → built-in fonts as last resort. Only `english` and `chs` ship
(`src/fonts/english/font.ini`, `src/fonts/chs/font.ini`).

**Key `font.ini` fields**: `maxHeight / maxWidth / lineSpacing / heightOffset / wordSpacing /
letterSpacing / fileName / warpMode / encoding`. In particular:
- `encoding`: the **8-bit encoding** of the game strings (e.g. `GBK`), converted to Unicode
  via iconv.
- `warpMode=1`: disables word-based wrapping and switches to **character-based wrapping**
  (CJK has no spaces and must break per character).
- Interface font ID mapping: `100→font0` (main menu/title), `101→font1` (dialog/description),
  `102→font2`, `103→font3` (DONE/YES), `104→font4` (title); `105` unused.

**Key limitation (GBK vs UTF-8)**:
- The game's `.msg` text is **8-bit encoded bytes** (GBK under `chs`), **not UTF-8**. If the
  TrueType backend is missing/broken, the built-in `.aaf` single-byte font will draw those
  GBK bytes as Latin characters one by one → the classic "mojibake".
- Therefore `font.ini`'s `encoding` **must match** the actual encoding of `.msg` (both shipped
  examples write `encoding=GBK`, including the English one — because the engine's text
  pipeline is inherently 8-bit bytes).
- **UTF-8 appears only in the Pip-Boy protocol**: the server uses `gameTextToUtf8()`
  (iconv `GBK→UTF-8`) to convert game strings to valid UTF-8 before putting them in the JSON,
  so the client needs no further conversion.
- Other known limits: some UI still hard-codes 12px line heights/pixel values, so changing
  `maxHeight/lineSpacing` won't scale those interfaces (overlap or gaps); only `english` and
  `chs` fonts ship, adding a language requires both `fonts/<language>/` and `text\<language>\`;
  `.aaf` is still registered at IDs `0..99`, only the interface range `100..104` is taken
  over, so script fonts hard-coded to low IDs still use bitmap fonts and are not localized.

---

## 5. Use of AI in programming

The code changes in this branch are human-led; AI (Claude, via the CodeBuddy coding
assistant) mainly handled **documentation and protocol梳理** (organization):

- **Protocol document `PIPBOY.md`**: drafted and completed by AI, defining the frame format,
  message types, field semantics, protocol versioning, and the "client degrades gracefully
  when optional fields are missing" rule; companion notes in `PIPBOY_CLIENT.md`.
- **Data-mapping write-up**: AI documented the collection points and field names for each data
  class in `src/pipboy_server.cc`'s `collectSnapshot()` (S.P.E.C.I.A.L., Derived, Conditions,
  Skills, Inventory, Armor, Map, Quests, Perks), giving the client implementation a definite
  basis.
- **Debug tooling**: `tools/pipboy_mock_server.py`, `pipboy_probe*.py`, etc. for verifying the
  protocol without a game were assisted by AI.
- For the **gamepad subsystem (`CONTROLLER.md`)** and **TrueType fonts (`FONTS.md`)**, AI
  documented the existing implementation and summarized its limitations; those features are
  part of the modified build, not AI-generated.

AI produced docs, protocol notes, and helper scripts; engine logic, gamepad mappings, and the
build system were implemented and accepted by a human.

---

## Notes (fork-specific)

- **`[pipboy] enabled` default**: currently `settings.h` defaults `enabled=true`. This is a
  **TEST BUILD** setting (so Android devices without a keyboard auto-start the service).
  Before any real release it should be reverted to `false`, with enabling done explicitly via
  `fallout2.cfg` or Ctrl+P (designed to be opt-in).
- **The server accepts a single client** — don't connect two `pipboy-client` instances at once.
- **Doc/code drift**: `PIPBOY.md` says "kick after 15s of inactivity" while the code's actual
  recv timeout is 30s; the code wins (doc will be fixed later).
- **License**: source follows upstream's [Sustainable Use License](LICENSE.md). Any
  distribution must comply with it, and you must own a legitimate copy of *Fallout 2* to play.
