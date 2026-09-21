#include "font_manager.h"

#include <stdio.h>
#include <string.h>

#include "color.h"
#include "db.h"
#include "debug.h"
#include "memory_manager.h"
#include "platform_compat.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#include "config.h"

#include "word_wrap.h"

#include <map>
#include <type_traits>
#include <utility>

#include "iconv.h"
#include "settings.h"

// The maximum number of interface fonts.
#define FT_FONT_MAX (16)

namespace fallout {

typedef struct FtFontGlyph {
    short width;
    short rows;
    short left;
    short top;
    // Advance width in whole pixels, straight from the font. `width` is only
    // the ink extent of the bitmap and is narrower than this for any glyph
    // that carries side bearings - which is most of them. Laying glyphs out
    // by `width` eats that bearing and packs the text together.
    short advance;
    unsigned char* buffer;
} FtFontGlyph;

typedef struct FtFontDescriptor {
    FT_Library library;
    FT_Face face;

    unsigned char* filebuffer;

    int maxHeight;
    int maxWidth;
    int letterSpacing;
    int wordSpacing;
    int lineSpacing;
    int heightOffset;
    int warpMode;
    char encoding[64];

    std::map<uint32_t, FtFontGlyph> map;
} FtFontDescriptor;

static int FtFontLoad(int font);
static int FtFontsLoadSet(const char* dir);
static void FtFontSetCurrentImpl(int font);
static int FtFontGetLineHeightImpl();
static int FtFontGetStringWidthImpl(const char* string);
static int FtFontGetCharacterWidthImpl(int ch);
static int FtFontGetMonospacedStringWidthImpl(const char* string);
static int FtFontGetLetterSpacingImpl();
static int FtFontGetBufferSizeImpl(const char* string);
static int FtFontGetMonospacedCharacterWidthImpl();
static void FtFontDrawImpl(unsigned char* buf, const char* string, int length, int pitch, int color);

static int FtFonteWordWrapImpl(const char* string, int width, short* breakpoints, short* breakpointsLengthPtr);
// 0x518680
static bool gFtFontsInitialized = false;

// 0x518684
static int gFtFontsLength = 0;

static int knobWidth = 5;
static int knobHeight = 7;

static unsigned char knobDump[35] = {
    0x34, 0x82, 0xb6, 0x82,
    0x34, 0x82, 0xb6, 0xb6,
    0xb6, 0x82, 0xb6, 0xb6,
    0xb6, 0xb6, 0xb6, 0x82,
    0xb6, 0xb6, 0xb6, 0x82,
    0x34, 0x82, 0xb6, 0x82,
    0x34, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0
};

// 0x518688
FontManager gFtFontManager = {
    100,
    110,
    FtFontSetCurrentImpl,
    FtFontDrawImpl,
    FtFontGetLineHeightImpl,
    FtFontGetStringWidthImpl,
    FtFontGetCharacterWidthImpl,
    FtFontGetMonospacedStringWidthImpl,
    FtFontGetLetterSpacingImpl,
    FtFontGetBufferSizeImpl,
    FtFontGetMonospacedCharacterWidthImpl,
    FtFonteWordWrapImpl,
};

// 0x586838
static FtFontDescriptor gFtFontDescriptors[FT_FONT_MAX];

// 0x58E938
static int gCurrentFtFont;

// 0x58E93C
static FtFontDescriptor* current;

static uint32_t output[1024] = {
    0x0,
};

// GNU libiconv declares the input buffer of `iconv` as `const char**`, while
// the copy bundled with macOS and glibc declares it as `char**`. Probe which
// declaration is available so the call below can be spelled portably.
template <typename InputPointer>
static auto iconvInputProbe(InputPointer) -> decltype(iconv(std::declval<iconv_t>(), std::declval<InputPointer>(), std::declval<size_t*>(), std::declval<char**>(), std::declval<size_t*>()), std::true_type{});

static std::false_type iconvInputProbe(...);

// `const char*` with GNU libiconv, `char*` with the macOS/glibc iconv.
using IconvInputPointer = std::conditional_t<decltype(iconvInputProbe(static_cast<const char**>(nullptr)))::value, const char*, char*>;

// The number of consumed output bytes is reported through `outbytesleft`, so
// the advanced output pointer itself does not need to be returned.
static size_t iconvConvert(iconv_t cd, const char* inbuf, size_t* inbytesleft, char* outbuf, size_t* outbytesleft)
{
    IconvInputPointer input = const_cast<IconvInputPointer>(inbuf);

    return iconv(cd, &input, inbytesleft, &outbuf, outbytesleft);
}

static int LtoU(const char* input, size_t charInPutLen)
{
    if (input[0] == '\x95') {
        size_t output_size = 1024;
        iconv_t cd = iconv_open("UCS-4-INTERNAL", current->encoding);
        charInPutLen -= 1;
        iconvConvert(cd, input + 1, &charInPutLen, (char*)(output + 1), &output_size);
        iconv_close(cd);

        output[0] = '\x95';
        return (1024 - output_size) / 4 + 1;
    } else {
        size_t output_size = 1024;
        iconv_t cd = iconv_open("UCS-4-INTERNAL", current->encoding);
        iconvConvert(cd, input, &charInPutLen, (char*)output, &output_size);
        iconv_close(cd);

        return (1024 - output_size) / 4;
    }
}

// Number of bytes `unicode` occupies in the font's encoding. Every offset this
// module hands back is a byte offset into the original string, so this is what
// has to be accumulated while measuring text.
//
// NOTE: The previous implementation converted through the same buffer that
// holds the decoded text, aliasing the input of `iconv` with its output.
static int FtCharByteLength(uint32_t unicode)
{
    char buffer[8];
    size_t charInPutLen = sizeof(unicode);
    size_t output_size = sizeof(buffer);
    iconv_t cd = iconv_open(current->encoding, "UCS-4-INTERNAL");
    iconvConvert(cd, (const char*)&unicode, &charInPutLen, buffer, &output_size);
    iconv_close(cd);

    int bytes = (int)(sizeof(buffer) - output_size);

    // A character with no representation in the target encoding still has to
    // advance the offset by something, otherwise the wrap loop stops making
    // progress.
    return bytes > 0 ? bytes : 1;
}

static FtFontGlyph GetFtFontGlyph(uint32_t unicode)
{
    if (current->map.count(unicode) > 0) {
        return current->map[unicode];
    } else {
        if (unicode == '\x95') {
            current->map[unicode].left = 0;
            current->map[unicode].top = current->maxHeight / 2;
            current->map[unicode].width = knobWidth;
            current->map[unicode].rows = knobHeight;
            current->map[unicode].advance = knobWidth;
            current->map[unicode].buffer = knobDump;
        } else {
            FT_Load_Glyph(current->face, FT_Get_Char_Index(current->face, unicode), FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
            FT_Render_Glyph(current->face->glyph, FT_RENDER_MODE_NORMAL);

            current->map[unicode].left = current->face->glyph->bitmap_left;
            current->map[unicode].top = current->face->glyph->bitmap_top;
            current->map[unicode].width = current->face->glyph->bitmap.width;
            current->map[unicode].rows = current->face->glyph->bitmap.rows;
            // 26.6 fixed point to whole pixels.
            current->map[unicode].advance = current->face->glyph->advance.x >> 6;

            int count = current->face->glyph->bitmap.width * current->face->glyph->bitmap.rows;

            if (count > 0) {
                current->map[unicode].buffer = (unsigned char*)internal_malloc_safe(count, __FILE__, __LINE__); // FONTMGR.C, 259
                memcpy(current->map[unicode].buffer, current->face->glyph->bitmap.buffer, count);
            } else {
                current->map[unicode].buffer = NULL;
            }
        }

        return current->map[unicode];
    }
}

// Directory the current font set was loaded from, with a trailing separator.
// Font files named by `fileName` are resolved against it, so a `font.ini` in
// `fonts/chs/` must reference `fonts/chs/*.ttf` while one in `fonts/` must
// reference `fonts/*.ttf`.
static char gFtFontDir[COMPAT_MAX_PATH];

// 0x441C80
int FtFontsInit()
{
    const char* language = settings.system.language.c_str();
    char fontDir[COMPAT_MAX_PATH];

    // NOTE: This port keeps one font set per language in `fonts/<language>/`,
    // while the Chinese translation of the community edition ships a single
    // set in `fonts/font.ini` for every language (the font files live next to
    // it). Both layouts are supported, the per-language one first.
    snprintf(fontDir, sizeof(fontDir), "fonts/%s", language);
    if (FtFontsLoadSet(fontDir) == 0) {
        if (FtFontsLoadSet("fonts") == 0) {
            debugPrint("No usable font configuration (\"fonts/%s/font.ini\" and \"fonts/font.ini\" both failed); using the built-in fonts.\n", language);
            return -1;
        }

        debugPrint("Using \"fonts/font.ini\" instead of \"fonts/%s/font.ini\".\n", language);
    }

    gFtFontsInitialized = true;

    FtFontSetCurrentImpl(gCurrentFtFont + 100);

    return 0;
}

// Loads the whole font set from [dir] ("fonts/chs" or "fonts"). Returns the
// number of fonts that could be loaded, which is zero when [dir] holds no
// usable configuration at all.
static int FtFontsLoadSet(const char* dir)
{
    snprintf(gFtFontDir, sizeof(gFtFontDir), "%s/", dir);

    int fontsLength = 0;
    int firstFont = -1;

    for (int font = 0; font < FT_FONT_MAX; font++) {
        if (FtFontLoad(font) == -1) {
            gFtFontDescriptors[font].maxHeight = 0;
            gFtFontDescriptors[font].filebuffer = NULL;
        } else {
            if (firstFont == -1) {
                firstFont = font;
            }

            ++fontsLength;
        }
    }

    if (firstFont == -1) {
        return 0;
    }

    gFtFontsLength = fontsLength;
    gCurrentFtFont = firstFont;
    gFtFontManager.maxFont = fontsLength + 100;

    return fontsLength;
}

// 0x441CEC
void FtFontsExit()
{
    for (int font = 0; font < FT_FONT_MAX; font++) {
        if (gFtFontDescriptors[font].filebuffer != NULL) {
            internal_free_safe(gFtFontDescriptors[font].filebuffer, __FILE__, __LINE__); // FONTMGR.C, 124
        }
    }
    //TODO: clean up
}

// 0x441D20
static int FtFontLoad(int font_index)
{
    char section[16];
    char path[COMPAT_MAX_PATH];
    FtFontDescriptor* desc = &(gFtFontDescriptors[font_index]);

    Config config;
    if (!configInit(&config)) {
        return -1;
    }

    // NOTE: `desc->filebuffer` is what marks a descriptor as usable, so it is
    // only assigned once the face has actually been created. Everything else
    // lives in locals that are released on the way out.
    unsigned char* filebuffer = NULL;
    unsigned char* filePtr = NULL;
    char* encoding = NULL;
    char* fontFileName = NULL;
    File* stream = NULL;
    int fileSize = 0;
    int readleft = 0;
    int rc = -1;

    snprintf(path, sizeof(path), "%sfont.ini", gFtFontDir);
    if (!configRead(&config, path, false)) {
        goto done;
    }

    snprintf(section, sizeof(section), "font%d", font_index);

    if (!configGetInt(&config, section, "maxHeight", &desc->maxHeight)) {
        goto done;
    }
    if (!configGetInt(&config, section, "maxWidth", &desc->maxWidth)) {
        desc->maxWidth = desc->maxHeight;
    }
    if (!configGetInt(&config, section, "lineSpacing", &desc->lineSpacing)) {
        goto done;
    }
    if (!configGetInt(&config, section, "wordSpacing", &desc->wordSpacing)) {
        goto done;
    }
    if (!configGetInt(&config, section, "letterSpacing", &desc->letterSpacing)) {
        goto done;
    }
    if (!configGetInt(&config, section, "heightOffset", &desc->heightOffset)) {
        goto done;
    }
    if (!configGetInt(&config, section, "warpMode", &desc->warpMode)) {
        goto done;
    }
    if (!configGetString(&config, section, "encoding", &encoding)) {
        goto done;
    }

    strncpy(desc->encoding, encoding, sizeof(desc->encoding) - 1);
    desc->encoding[sizeof(desc->encoding) - 1] = '\0';

    if (!configGetString(&config, section, "fileName", &fontFileName)) {
        goto done;
    }

    snprintf(path, sizeof(path), "%s%s", gFtFontDir, fontFileName);

    stream = fileOpen(path, "rb");
    if (stream == NULL) {
        debugPrint("Font file \"%s\" referenced by [%s] does not exist.\n", path, section);
        goto done;
    }

    fileSize = fileGetSize(stream);

    filebuffer = (unsigned char*)internal_malloc_safe(fileSize, __FILE__, __LINE__); // FONTMGR.C, 259

    readleft = fileSize;
    filePtr = filebuffer;

    while (readleft > 10000) {
        int readsize = fileRead(filePtr, 1, 10000, stream);
        if (readsize != 10000) {
            debugPrint("Font file \"%s\" is truncated.\n", path);
            goto done;
        }
        readleft -= 10000;
        filePtr += 10000;
    }

    if (fileRead(filePtr, 1, readleft, stream) != readleft) {
        debugPrint("Font file \"%s\" is truncated.\n", path);
        goto done;
    }

    fileClose(stream);
    stream = NULL;

    if (FT_Init_FreeType(&(desc->library)) != 0) {
        debugPrint("Font file \"%s\" cannot be used (FreeType initialization failed).\n", path);
        goto done;
    }

    if (FT_New_Memory_Face(desc->library, filebuffer, fileSize, 0, &(desc->face)) != 0) {
        debugPrint("Font file \"%s\" is not a font FreeType can load.\n", path);
        FT_Done_FreeType(desc->library);
        desc->library = NULL;
        goto done;
    }

    FT_Select_Charmap(desc->face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(desc->face, desc->maxWidth, desc->maxHeight);

    desc->filebuffer = filebuffer;
    filebuffer = NULL;

    rc = 0;

done:
    if (stream != NULL) {
        fileClose(stream);
    }

    if (filebuffer != NULL) {
        internal_free_safe(filebuffer, __FILE__, __LINE__);
    }

    configFree(&config);

    return rc;
}

// 0x442120
static void FtFontSetCurrentImpl(int font)
{
    if (!gFtFontsInitialized) {
        return;
    }

    font -= 100;

    if (gFtFontDescriptors[font].filebuffer != NULL) {
        gCurrentFtFont = font;
        current = &(gFtFontDescriptors[font]);
    }
}

// 0x442168
static int FtFontGetLineHeightImpl()
{
    if (!gFtFontsInitialized) {
        return 0;
    }

    return current->lineSpacing + current->maxHeight + current->heightOffset;
}

// 0x442188
static int FtFontGetStringWidthImpl(const char* string)
{
    if (!gFtFontsInitialized) {
        return 0;
    }

    if (strlen(string) == 1 && string[0] < 0) {
        return current->wordSpacing;
    } else {
        int count;
        int width = 0;

        count = LtoU((char*)string, strlen(string));

        for (int i = 0; i < count; i++) {
            uint32_t ch = output[i];

            if (ch == L'\n' || ch == L'\r') {
                continue;
            }

            if (ch == '\x95') {
                FtFontGlyph g = GetFtFontGlyph(ch);
                width += g.width + current->letterSpacing + 2;
            } else if (ch == L' ') {
                width += current->wordSpacing + current->letterSpacing;
            } else {
                // Must agree with `FtFontDrawImpl`, which also advances by
                // `g.advance`. When the two disagree, wrapped lines are
                // measured as narrower than they are drawn.
                FtFontGlyph g = GetFtFontGlyph(ch);
                width += g.advance + current->letterSpacing;
            }
        }

        return width;
    }
}

// 0x4421DC
static int FtFontGetCharacterWidthImpl(int ch)
{
    if (!gFtFontsInitialized) {
        return 0;
    }

    return current->wordSpacing;
}

// 0x442210
static int FtFontGetMonospacedStringWidthImpl(const char* str)
{
    return FtFontGetStringWidthImpl(str);
}

// 0x442240
static int FtFontGetLetterSpacingImpl()
{
    if (!gFtFontsInitialized) {
        return 0;
    }

    return current->letterSpacing;
}

// 0x442258
static int FtFontGetBufferSizeImpl(const char* str)
{
    if (!gFtFontsInitialized) {
        return 0;
    }

    return FtFontGetStringWidthImpl(str) * (current->lineSpacing + current->maxHeight + current->heightOffset);
}

// 0x442278
static int FtFontGetMonospacedCharacterWidthImpl()
{
    if (!gFtFontsInitialized) {
        return 0;
    }

    return current->lineSpacing + current->maxHeight;
}
                                                                                                                                                
// 0x4422B4
static void FtFontDrawImpl(unsigned char* buf, const char* string, int length, int pitch, int color)
{
    if (!gFtFontsInitialized) {
        return;
    }

    if ((color & FONT_SHADOW) != 0) {
        color &= ~FONT_SHADOW;
        // NOTE: Other font options preserved. This is different from text font
        // shadows.
        FtFontDrawImpl(buf + pitch + 1, string, length, pitch, (color & ~0xFF) | _colorTable[0]);
    }

    unsigned char* palette = _getColorBlendTable(color & 0xFF);
    int count = LtoU((char*)string, strlen(string));
    int maxTop = -1;

    for (int i = 0; i < count; i++) {
        uint32_t ch = output[i];

        FtFontGlyph g = GetFtFontGlyph(ch);
        if (maxTop < g.top)
            maxTop = g.top;
    }

    maxTop = current->maxHeight - maxTop;

    unsigned char* ptr = buf;
    for (int i = 0; i < count; i++) {
        uint32_t ch = output[i];

        FtFontGlyph g = GetFtFontGlyph(ch);

        int characterWidth;
        if (ch == L'\n' || ch == L'\r') {
            continue;
        } else if (ch == L' ') {
            characterWidth = current->wordSpacing;
        } else if (ch == '\x95') {
            characterWidth = g.width + 2;
        } else {
            // Advance the pen by the font's advance width, not by the ink
            // extent of the bitmap. Their difference is the glyph's side
            // bearings, and dropping them is what packed CJK text into an
            // unreadable smear.
            characterWidth = g.advance;
        }

        unsigned char* end = ptr + characterWidth + current->letterSpacing;
        if (end - buf > length) {
            break;
        }

        ptr += (current->maxHeight - g.top - maxTop) * pitch;

        unsigned char* glyphDataPtr = g.buffer;

        for (int y = 0; y < g.rows && y < current->maxHeight; y++) {
            for (int x = 0; x < g.width; x++) {
                unsigned char byte = *glyphDataPtr++;
                // The blend table starts with eight levels (0/7 .. 7/7 of the
                // foreground) and continues with six entries of
                // `_calculateColor` used for glow effects. Dividing by 26 sent
                // a fully opaque pixel to index 9 - a glow entry - instead of
                // to the plain foreground at index 7.
                byte >>= 5;

                *ptr++ = palette[(byte << 8) + *ptr];
            }

            ptr += pitch - g.width;
        }

        ptr = end;
    }

    if ((color & FONT_UNDERLINE) != 0) {
        int length = ptr - buf;
        unsigned char* underlinePtr = buf + pitch * (current->maxHeight - 1);
        for (int index = 0; index < length; index++) {
            *underlinePtr++ = color & 0xFF;
        }
    }

    _freeColorBlendTable(color & 0xFF);
}

static int FtFonteWordWrapImpl(const char* string, int width, short* breakpoints, short* breakpointsLengthPtr)
{
    breakpoints[0] = 0;
    *breakpointsLengthPtr = 1;

    for (int index = 1; index < WORD_WRAP_MAX_COUNT; index++) {
        breakpoints[index] = -1;
    }

    if (fontGetStringWidth(string) < width) {
        breakpoints[*breakpointsLengthPtr] = (short)strlen(string);
        *breakpointsLengthPtr += 1;
        return 0;
    }

    int accum = 0;
    int prevSpaceOrHyphen = -1;

    int count = LtoU((char*)string, strlen(string));

    int PreCharIndex;

    int uint32Index;
    int CharIndex = 0;

    for (int i = 0; i < count;) 
    {
        const uint32_t ch = output[i];

        // A hard line break in the source text is left to the caller to deal
        // with. Advance the index: skipping a character without advancing
        // never terminates.
        if (ch == L'\n' || ch == L'\r') {
            i++;
            continue;
        }

        FtFontGlyph g = GetFtFontGlyph(ch);

        PreCharIndex = CharIndex;

        // Must agree with `FtFontDrawImpl` and `FtFontGetStringWidthImpl`.
        // `(ch > 128 && ch < 256)` used to be treated as a blank here but not
        // when drawing, so wrapped lines were measured narrower than drawn.
        if (ch == L' ') {
            accum += current->letterSpacing + current->wordSpacing;
        } else if (ch == '\x95') {
            accum += current->letterSpacing + g.width + 2;
        } else {
            accum += current->letterSpacing + g.advance;
        }

        // NOTE: This used to add a literal 1 for every character outside the
        // CJK range. That is only correct for a single byte encoding: in GBK a
        // character like U+00B0 occupies two bytes, and understating its size
        // shifts all following breaks into the middle of a character.
        CharIndex += FtCharByteLength(ch);

        if (accum <= width) {
            // NOTE: quests.txt #807 uses extended ascii.
            if (ch == L' ' || ch == L'-') {
                prevSpaceOrHyphen = CharIndex;
                uint32Index = i;
            } else if (current->warpMode == 1) {
                prevSpaceOrHyphen = -1;
            }
            i++;
        } else {
            if (*breakpointsLengthPtr == WORD_WRAP_MAX_COUNT) {
                return -1;
            }

            if (prevSpaceOrHyphen != -1) {
                // Word wrap. `prevSpaceOrHyphen` is already the byte offset
                // just past the space or hyphen, so the new line starts there
                // and the running offset continues from the same value.
                breakpoints[*breakpointsLengthPtr] = prevSpaceOrHyphen;

                i = uint32Index + 1;
                CharIndex = prevSpaceOrHyphen;
            } else {
                // Character wrap.
                breakpoints[*breakpointsLengthPtr] = PreCharIndex;
                CharIndex = PreCharIndex;
            }

            *breakpointsLengthPtr += 1;
            prevSpaceOrHyphen = -1;
            accum = 0;
        }
    }

    if (*breakpointsLengthPtr == WORD_WRAP_MAX_COUNT) {
        return -1;
    }

    breakpoints[*breakpointsLengthPtr] = CharIndex;
    *breakpointsLengthPtr += 1;

    return 0;
}

} // namespace fallout
