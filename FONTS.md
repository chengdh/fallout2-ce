# TrueType fonts

This branch adds an alternative font backend that renders text from
TrueType/OpenType files with [FreeType](https://freetype.org/) instead of the
built-in bitmapped `.aaf` interface fonts. The main motivation is languages
whose glyph count makes bitmap fonts impractical, but any font can be used.

The implementation lives in `src/freetype_manager.cc` / `src/freetype_manager.h`.

## How it plugs into the engine

`FontManager` (`src/text_font.h`) describes a swap-in font backend: drawing,
measuring and line breaking all go through function pointers that
`fontSetCurrent` installs. Font IDs `100`..`110` are the *interface* font range,
and `0`..`5` are the text fonts.

The important detail is in `fontManagerAdd` (`src/text_font.cc`): a manager is
**rejected if any font ID in its range is already covered** by a registered one.
Startup order in `src/game.cc` is therefore significant:

```cpp
if (!FtFontsInit()) {            // claims 100 .. 100 + <number of fonts>
    fontManagerAdd(&gFtFontManager);
}

if (!interfaceFontsInit()) {     // rejected while the above succeeded
    fontManagerAdd(&gModernFontManager);
}
```

So whenever a usable `font.ini` is found (see below), **TrueType takes over
the whole interface font range** and every piece of interface text is drawn from
the configured font files. If the configuration is missing or invalid,
`FtFontsInit()` fails, the manager is never registered, and the built-in `.aaf`
fonts are used exactly as before. There is no runtime toggle - adding or
removing `font.ini` is the switch.

Because the `.aaf` fonts are single-byte, losing the TrueType backend on a
localized install is immediately visible: the 8-bit (for example GBK) bytes of
`text\<language>\*.msg` are drawn one at a time as Latin glyphs, which is what
the classic "garbled text" symptom looks like. Startup reports which
configuration was used, so turn on `debugPrint` (`[debug] mode=environment` plus
a `DEBUGACTIVE=log` environment variable, or `mode=log`) to see it.

### Line breaking

`wordWrap` used to be a free function. It is now a function pointer stored on
`FontManager` (the original implementation was renamed to `leagcyWordWrap` and
is still used by the bitmap backends). This is required, not cosmetic: the
generic byte-oriented wrapper breaks lines on ASCII spaces, which cannot work
for scripts that do not use spaces between words.

## Configuration

`font.ini` is looked up in two places, in this order:

1. `fonts/<language>/font.ini` - one font set per language, the layout
   described below and the one the released `fonts` folder uses.
2. `fonts/font.ini` - a single font set for every language, in `fonts/` itself.
   This is the layout used by the Chinese translation of the community edition
   (`yahooboby`'s patch), which hardcodes that path, so an existing Chinese
   install keeps working when only the executable is replaced.

Both are relative to the game directory and the font files named by `fileName`
are resolved against whichever directory the configuration was read from. If
the first candidate yields no usable font at all, the second one is tried, and
only when both fail do the built-in fonts take over.

The per-language layout mirrors how the engine already locates localized text:

```
fonts/
  english/
    font.ini
    r_fallouty.ttf
    TT0807M_.TTF
    JH_FALLOUT.TTF
  chs/
    font.ini
    font.ttf
    zpix.ttf
```

`<language>` is the value of `language` in the `[system]` section of the game
config (`english` by default, matching `ENGLISH` in `src/game_config.h`).
Note that the same setting drives `text\<language>\...` message lookup, so
switching to a non-English language also requires the matching `text\`
directory.

The shared layout instead looks like this, with `fileName` naming the files
next to it:

```
fonts/
  font.ini        # [font0] .. [fontN]
  font0.ttf
  font1.ttf
  ...
```

Each `[fontN]` section in `font.ini` describes one font:

| Key | Meaning |
| --- | --- |
| `maxHeight`, `maxWidth` | Pixel size the glyphs are rasterized at. |
| `lineSpacing`, `heightOffset` | Added to `maxHeight` to form the line height. |
| `wordSpacing`, `letterSpacing` | Extra advance for spaces and between glyphs. |
| `fileName` | Font file, relative to the directory `font.ini` was read from. |
| `warpMode` | `1` disables word wrapping in favour of character wrapping. |
| `encoding` | 8-bit encoding of the game's strings, e.g. `GBK`. Converted to Unicode with iconv. |

Several `[fontN]` sections may point at the same file at different sizes; the
`english` and `chs` sets both do this.

Place the `fonts` directory next to `master.dat`, i.e. wherever the game already
looks for its data files.

## Building

FreeType and iconv are needed in addition to the existing dependencies, and are
handled the same way SDL2 and zlib are:

- `FALLOUT_VENDORED=ON` (default) builds a static FreeType and GNU libiconv from
  `third_party/freetype` and `third_party/iconv` via `FetchContent`.
- `FALLOUT_VENDORED=OFF` uses the ones already installed on the system
  (`find_package(Freetype)` / `find_package(Iconv)`).

The vendored build uses `third_party/freetype/ftoption.h` instead of the one
shipped with FreeType. It enables `TT_CONFIG_OPTION_SUBPIXEL_HINTING 2`, which
noticeably improves the very small pixel sizes Fallout 2 renders text at. A
system FreeType 2.13 or newer no longer has that option, so hinting - and
therefore the exact glyph shapes - will differ slightly when building with
`FALLOUT_VENDORED=OFF`. Prefer the default for release builds.

`src/freetype_manager.cc` calls `iconv` through a small shim because the two
libiconv flavours disagree on the constness of the input buffer: GNU libiconv
declares it as `const char**`, while the copy bundled with macOS and glibc
declares it as `char**`. The shim probes the available declaration at compile
time, so both work.

## Caveats

- **Layout is tuned for a 10 px line height.** A number of UI screens do not use
  `fontGetLineHeight()` for their vertical spacing; they use fixed pixel values
  (`10`, or `11` in the character selector), and the pipboy/holodisk line counts
  scale from a 10 px baseline. Changing `maxHeight` or `lineSpacing` in
  `font.ini` will therefore not rescale those screens - they will overlap or
  leave gaps. The fixed values are marked with comments where they appear.
- Only the `english` and `chs` font sets are shipped. Adding a language means
  adding a `fonts/<language>/` directory and a `text\<language>\` directory.
- `font.ini` lists one `[fontN]` section per font, but the engine only asks for
  interface fonts `100`..`104` (`[font0]`..`[font4]`). A section that is missing
  or whose font file cannot be loaded simply leaves that font to the built-in
  backend, which is fine for screens that are not localized. In particular, a
  `.ttf`, `.otf` or `.ttc` file that FreeType refuses to open is skipped with a
  message instead of breaking startup.
- The `.aaf` fonts remain registered for IDs `0`..`99`; only the interface
  range is taken over. Scripts that select a font by a hardcoded low ID still
  get the bitmapped one, and its text will not be localized.
- The TrueType backend rasterizes into the engine's existing 8-bit indexed
  buffers, including the `FONT_SHADOW` / `FONT_UNDERLINE` effects, so it is a
  drop-in replacement rather than a rewrite of the text pipeline.
