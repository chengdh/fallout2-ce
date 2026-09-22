#include "gamepad_internal.h"

#include "gamepad_cjk.h"
#include "gamepad_l10n.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace fallout {
namespace pad {

static const Action quickActions[] = {
    Inventory, Character, Pipboy, Automap,
    Skilldex, Rest, SwapHands, WeaponMode,
    Combat, EndTurn, Confirm, Center,
    Save, Load, Cancel, Keyboard,
    Sneak, Lockpick, Steal, Traps,
    FirstAid, Doctor, Science, Repair,
};

static const int remappable[] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_START,
};

static const TextId settingNames[9] = {
    TextCursorSpeed, TextStickDeadzone, TextPrecisionSpeed, TextScrollSpeed,
    TextReverseScroll, TextSwapSticks, TextButtonLabels, TextShowPrompts,
    TextActiveController,
};
static const TextId tabNames[4] = { TextTabActions, TextTabSettings, TextTabKeyboard, TextTabHelp };
static constexpr int settingCount = 21;
static const char* letters = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM.,'";

void changeTab(int delta)
{
    state.tab = (state.tab + delta + 4) % 4;
    state.navDirection = -1;
}

void adjust(int delta)
{
    auto& s = state.settings;
    int row = state.selection[1];
    switch (row) {
    case 0: s.speed = std::clamp(s.speed + delta * 50, 100, 1200); break;
    case 1: s.deadzone = std::clamp(s.deadzone + delta, 5, 40); break;
    case 2: s.precision = std::clamp(s.precision + delta * 5, 10, 60); break;
    case 3: s.scrollSpeed = std::clamp(s.scrollSpeed + delta, 2, 20); break;
    case 4: s.invertScroll = !s.invertScroll; break;
    case 5: s.swapSticks = !s.swapSticks; break;
    case 6: s.labels = (s.labels + delta + 5) % 5; break;
    case 7: s.hints = !s.hints; break;
    case 8:
        if (!state.devices.empty()) {
            releaseInputs();
            state.active = (state.active + delta + static_cast<int>(state.devices.size())) % state.devices.size();
            std::fill(std::begin(state.buttons), std::end(state.buttons), false);
            // Require release on the newly selected pad before accepting a
            // held confirm/back button from it.
            std::fill(std::begin(state.blocked), std::end(state.blocked), true);
            state.waitNeutral = true;
        }
        break;
    case 18: {
        int display = s.borderless;
        s = defaults();
        s.borderless = display;
        break;
    }
    case 19: s.directMovement = !s.directMovement; break;
    case 20: toggleDisplay(); return;
    default: {
        int button = remappable[row - 9];
        s.bindings[button] = (s.bindings[button] + delta + ActionCount) % ActionCount;
        break;
    }
    }
    saveSettings();
}

void navigate(int dx, int dy)
{
    int& selected = state.selection[state.tab];
    if (state.tab == 0) {
        int col = (selected % 4 + dx + 4) % 4;
        int row = (selected / 4 + dy + 6) % 6;
        selected = row * 4 + col;
    } else if (state.tab == 1) {
        if (dy) selected = (selected + dy + settingCount) % settingCount;
        if (dx) adjust(dx);
    } else if (state.tab == 2) {
        int col = (selected % 10 + dx + 10) % 10;
        int row = (selected / 10 + dy + 5) % 5;
        selected = row * 10 + col;
    }
}

static void sendText(bool done)
{
    // Drain through the engine one character per input poll, including erase
    // and Enter. This preserves order without overflowing its 40-event queue.
    std::string text = state.typed;
    state.typed.clear();
    setOpen(false);
    if (state.replaceText && state.textInput) state.pendingText.append(64, '\b');
    state.pendingText += text;
    if (done) state.pendingText += '\r';
}

void activate()
{
    if (state.tab == 0) {
        Action action = quickActions[state.selection[0]];
        if (action == Keyboard) { state.typed.clear(); state.tab = 2; }
        else { setOpen(false); pulse(actions[action].key); }
    } else if (state.tab == 1) {
        adjust(1);
    } else if (state.tab == 2) {
        int selected = state.selection[2];
        if (selected < 40) {
            char ch = letters[selected];
            if (!state.caps) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (state.typed.size() < 24) state.typed += ch;
        } else {
            switch (selected - 40) {
            case 0: if (state.typed.size() < 24) state.typed += ' '; break;
            case 1: if (!state.typed.empty()) state.typed.pop_back(); break;
            case 2: state.typed.clear(); break;
            case 3: state.caps = !state.caps; break;
            case 4: setOpen(false); state.pendingText += '\b'; break;
            case 5: state.replaceText = !state.replaceText; break;
            case 6: state.typed = "y"; sendText(false); break;
            case 7: state.typed = "n"; sendText(false); break;
            case 8: sendText(false); break;
            case 9: sendText(true); break;
            }
        }
    }
}

// Original 5x7 pixel alphabet. Rendered directly with SDL: no game art,
// external fonts, GPU textures, or palette changes are required. Localized
// strings mix it with the 12x12 Chinese subset in `gamepad_cjk.h`.
static const unsigned char glyphs[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, // AB
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30}, // CD
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, // EF
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, // GH
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12}, // IJ
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31}, // KL
    {17,27,21,21,17,17,17}, {17,25,25,21,19,19,17}, // MN
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16}, // OP
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, // QR
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, // ST
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, // UV
    {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17}, // WX
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}, // YZ
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
};

struct Color { Uint8 r, g, b; };
static constexpr Color green { 141, 218, 117 };
static constexpr Color dim { 70, 120, 68 };
static constexpr Color amber { 218, 181, 98 };
static constexpr Color metal { 76, 77, 61 };
static constexpr Color shadow { 25, 28, 23 };
static constexpr Color screen { 15, 28, 21 };

static void color(SDL_Renderer* r, Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255); }
static void box(SDL_Renderer* r, int x, int y, int w, int h, Color c)
{
    color(r, c);
    SDL_Rect rect { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}
static void outline(SDL_Renderer* r, int x, int y, int w, int h, Color c)
{
    color(r, c);
    SDL_Rect rect { x, y, w, h };
    SDL_RenderDrawRect(r, &rect);
}

// Decodes one UTF-8 sequence and moves the pointer past it. A stray byte is
// returned as-is so that broken text still makes progress.
static unsigned int decodeUtf8(const char*& string)
{
    unsigned char lead = static_cast<unsigned char>(*string++);
    if (lead < 0x80) return lead;
    int continuation;
    unsigned int code;
    if ((lead & 0xE0) == 0xC0) { code = lead & 0x1F; continuation = 1; }
    else if ((lead & 0xF0) == 0xE0) { code = lead & 0x0F; continuation = 2; }
    else if ((lead & 0xF8) == 0xF0) { code = lead & 0x07; continuation = 3; }
    else return lead;
    while (continuation-- > 0) {
        unsigned char next = static_cast<unsigned char>(*string);
        if ((next & 0xC0) != 0x80) break;
        code = (code << 6) | (next & 0x3F);
        ++string;
    }
    return code;
}

static const CjkGlyph* cjkGlyph(unsigned int code)
{
    auto found = std::lower_bound(cjkGlyphs.begin(), cjkGlyphs.end(), code,
        [](const CjkGlyph& glyph, unsigned int value) { return glyph.code < value; });
    if (found == cjkGlyphs.end() || found->code != code) return nullptr;
    return &*found;
}

// Horizontal space one code point takes, in the overlay's native pixels.
static int advanceOf(unsigned int code)
{
    return code < 0x80 ? 6 : cjkAdvance;
}

static int stringWidth(const char* string)
{
    int width = 0;
    while (*string) width += advanceOf(decodeUtf8(string));
    return width;
}

static void drawCjk(SDL_Renderer* r, int x, int y, unsigned int code, Color c, int scale)
{
    const CjkGlyph* glyph = cjkGlyph(code);
    for (int row = 0; row < cjkCellHeight; ++row) {
        for (int column = 0; column < cjkCellWidth; ++column) {
            bool set;
            if (glyph != nullptr) {
                set = (glyph->rows[row] & (1 << (cjkCellWidth - 1 - column))) != 0;
            } else {
                // Missing glyphs fall back to a hollow box, the usual pixel UI
                // marker for text this build does not carry.
                set = row == 0 || row == cjkCellHeight - 1 || column == 0 || column == cjkCellWidth - 1;
            }
            if (set) box(r, x + column * scale, y + row * scale, scale, scale, c);
        }
    }
}

// Draws one line of overlay text. ASCII uses the built-in 5x7 alphabet and
// every other code point the 12x12 Chinese subset, so a translated string can
// mix "L3" with ideographs. `maxChars` stays counted in Latin characters to
// keep the original call sites honest: single byte strings clip exactly as
// before, and full width scripts merely run out of room sooner.
static void text(SDL_Renderer* r, int x, int y, const char* string, Color c = green, int scale = 1, int maxChars = 90)
{
    int origin = x;
    int limit = maxChars * 6 * scale;
    while (*string) {
        unsigned int code = decodeUtf8(string);
        int advance = advanceOf(code);
        if (x - origin + advance * scale > limit) break;
        if (code >= 0x80) {
            drawCjk(r, x, y + cjkVerticalOffset * scale, code, c, scale);
            x += advance * scale;
            continue;
        }
        unsigned char ch = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(code)));
        const unsigned char* bits = nullptr;
        if (ch >= 'A' && ch <= 'Z') bits = glyphs[ch - 'A'];
        if (ch >= '0' && ch <= '9') bits = glyphs[26 + ch - '0'];
        unsigned char punctuation[7] {};
        if (!bits) {
            switch (ch) {
            case '-': punctuation[3] = 14; break;
            case '_': punctuation[6] = 31; break;
            case '.': punctuation[6] = 4; break;
            case ',': punctuation[5] = 4; punctuation[6] = 8; break;
            case ':': punctuation[2] = punctuation[5] = 4; break;
            case '/': for (int j = 0; j < 7; ++j) punctuation[j] = 1 << (j * 4 / 6); break;
            case '>': punctuation[1] = 8; punctuation[2] = 4; punctuation[3] = 2; punctuation[4] = 4; punctuation[5] = 8; break;
            case '<': punctuation[1] = 2; punctuation[2] = 4; punctuation[3] = 8; punctuation[4] = 4; punctuation[5] = 2; break;
            case '[': punctuation[0] = punctuation[6] = 14; for (int j = 1; j < 6; ++j) punctuation[j] = 8; break;
            case ']': punctuation[0] = punctuation[6] = 14; for (int j = 1; j < 6; ++j) punctuation[j] = 2; break;
            case '+': punctuation[2] = punctuation[4] = 4; punctuation[3] = 14; break;
            case '%': punctuation[0] = 17; punctuation[1] = 2; punctuation[2] = 4; punctuation[3] = 8; punctuation[4] = 17; break;
            case '\'': punctuation[0] = punctuation[1] = 4; break;
            case '?': punctuation[0] = 14; punctuation[1] = 17; punctuation[2] = 2; punctuation[3] = 4; punctuation[5] = 4; break;
            default: break;
            }
            bits = punctuation;
        }
        for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
            if (bits[row] & (1 << (4 - col))) box(r, x + col * scale, y + row * scale, scale, scale, c);
        }
        x += advance * scale;
    }
}

static void screw(SDL_Renderer* r, int x, int y)
{
    box(r, x - 3, y - 3, 7, 7, shadow);
    box(r, x - 2, y - 2, 5, 5, { 121, 121, 92 });
    box(r, x - 2, y, 5, 1, shadow);
}

static void settingsPage(SDL_Renderer* r)
{
    int selected = state.selection[1];
    int start = (selected / 7) * 7;
    const auto& s = state.settings;
    for (int row = start; row < std::min(start + 7, settingCount); ++row) {
        int y = 147 + (row - start) * 27;
        if (row == selected) { box(r, 78, y - 7, 484, 24, { 42, 63, 36 }); text(r, 85, y, ">", amber); }
        char name[80];
        char value[80];
        if (row < 9) snprintf(name, sizeof(name), "%s", l10n(settingNames[row]));
        else if (row < 18) snprintf(name, sizeof(name), l10n(TextBinding), buttonName(remappable[row - 9]));
        else if (row == 18) snprintf(name, sizeof(name), "%s", l10n(TextRestoreDefaults));
        else if (row == 19) snprintf(name, sizeof(name), "%s", l10n(TextStickMode));
        else snprintf(name, sizeof(name), "%s", l10n(TextDisplayMode));
        switch (row) {
        case 0: snprintf(value, sizeof(value), l10n(TextSpeedValue), s.speed); break;
        case 1: snprintf(value, sizeof(value), l10n(TextPercentValue), s.deadzone); break;
        case 2: snprintf(value, sizeof(value), l10n(TextPercentValue), s.precision); break;
        case 3: snprintf(value, sizeof(value), l10n(TextNumberValue), s.scrollSpeed); break;
        case 4: snprintf(value, sizeof(value), "%s", l10n(s.invertScroll ? TextOn : TextOff)); break;
        case 5: snprintf(value, sizeof(value), "%s", l10n(s.swapSticks ? TextOn : TextOff)); break;
        case 6: snprintf(value, sizeof(value), "%s", labelStyleName(s.labels)); break;
        case 7: snprintf(value, sizeof(value), "%s", l10n(s.hints ? TextOn : TextOff)); break;
        case 8: snprintf(value, sizeof(value), l10n(TextControllerValue), state.active + 1, static_cast<int>(state.devices.size())); break;
        case 18: snprintf(value, sizeof(value), "%s", l10n(TextResetValue)); break;
        case 19: snprintf(value, sizeof(value), "%s", l10n(s.directMovement ? TextMoveCharacter : TextPointAndClick)); break;
        case 20: snprintf(value, sizeof(value), "%s", l10n(s.borderless == 1 ? TextBorderless : TextWindowed)); break;
        default: snprintf(value, sizeof(value), "%s", actionName(s.bindings[remappable[row - 9]])); break;
        }
        text(r, 100, y, name, row == selected ? green : dim);
        text(r, 373, y, value, row == selected ? amber : dim, 1, 29);
    }
    char footer[90];
    snprintf(footer, sizeof(footer), l10n(TextPageValue), selected / 7 + 1);
    text(r, 82, 348, footer, dim);
    text(r, 82, 369, state.displayFailed ? l10n(TextDisplayFailed)
        : state.saveFailed ? l10n(TextSaveFailed)
        : l10n(TextAutoSave), (state.displayFailed || state.saveFailed) ? amber : dim);
}

static void keyboardPage(SDL_Renderer* r)
{
    box(r, 80, 140, 480, 29, { 8, 17, 12 });
    text(r, 88, 150, state.typed.c_str(), green, 1, 24);
    text(r, 88 + static_cast<int>(state.typed.size()) * 6, 150, "_", amber);
    char count[40];
    snprintf(count, sizeof(count), l10n(TextKeyboardCount), static_cast<int>(state.typed.size()),
        l10n(state.caps ? TextKeyboardUppercase : TextKeyboardLowercase));
    text(r, 466, 150, count, dim);
    for (int i = 0; i < 50; ++i) {
        int x = 80 + (i % 10) * 48;
        int y = 180 + (i / 10) * 29;
        bool selected = state.selection[2] == i;
        box(r, x, y, 44, 25, selected ? Color { 48, 72, 40 } : Color { 24, 40, 27 });
        outline(r, x, y, 44, 25, selected ? amber : dim);
        char letter[2] = { i < 40 ? letters[i] : ' ', 0 };
        const char* label = i < 40 ? letter : keyboardToolName(i - 40);
        text(r, x + (44 - stringWidth(label)) / 2, y + 9, label, selected ? amber : green);
    }
    text(r, 82, 340, l10n(TextKeyboardSendHint), dim);
    text(r, 82, 356, l10n(state.replaceText ? TextKeyboardReplaceHint : TextKeyboardAppendHint), dim);
    text(r, 82, 372, l10n(TextKeyboardDigitsHint), dim);
}

static void helpPage(SDL_Renderer* r)
{
    text(r, 82, 143, l10n(TextHelpTitle), amber);
    for (int i = 0; i < helpLineCount(); ++i) text(r, 82, 164 + i * 17, helpLine(i), i < 7 ? green : dim);
}

void render(SDL_Renderer* r)
{
    if (!state.open && (!state.settings.hints || !state.used || state.active < 0 || SDL_GetTicks() - state.lastUse > 6000)) return;
    // Rasterize at integer pixel scale, then scale the completed panel. Direct
    // 1-pixel rectangles leave gaps at fractional renderer scales (e.g. 1.5x).
    int logicalW, logicalH;
    SDL_RenderGetLogicalSize(r, &logicalW, &logicalH);
    float factor = std::min(logicalW / 640.0f, logicalH / 480.0f);
    if (factor <= 0) factor = 1;
    SDL_Renderer* destination = r;
    SDL_Surface* canvas = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!canvas) return;
    r = SDL_CreateSoftwareRenderer(canvas);
    if (!r) { SDL_FreeSurface(canvas); return; }
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    if (!state.open) {
        box(r, 12, 10, 616, 38, shadow);
        outline(r, 12, 10, 616, 38, metal);
        char hint[200];
        snprintf(hint, sizeof(hint), l10n(TextPromptBar),
            buttonName(SDL_CONTROLLER_BUTTON_A), actionName(state.settings.bindings[SDL_CONTROLLER_BUTTON_A]),
            buttonName(SDL_CONTROLLER_BUTTON_B), actionName(state.settings.bindings[SDL_CONTROLLER_BUTTON_B]),
            buttonName(SDL_CONTROLLER_BUTTON_BACK));
        text(r, 22, 19, hint, green, 1, 100);
        text(r, 22, 34, state.worldContext && state.settings.directMovement
            ? l10n(TextPromptMove)
            : l10n(TextPromptCursor), amber);
    } else {
        box(r, 49, 51, 548, 384, { 6, 9, 7 });
        box(r, 42, 44, 548, 384, metal);
        outline(r, 43, 45, 546, 382, { 130, 128, 95 });
        outline(r, 46, 48, 540, 376, shadow);
        for (int y = 54; y < 420; y += 4) box(r, 49, y, 534, 1, { 71, 72, 57 });
        box(r, 62, 64, 508, 38, shadow);
        text(r, 76, 76, "VAULT-TEC", amber, 2);
        text(r, 240, 72, l10n(TextTitle), green);
        text(r, 240, 88, deviceName(), dim, 1, 52);
        box(r, 550, 76, 8, 8, state.active >= 0 ? green : Color { 150, 64, 47 });
        for (int i = 0; i < 4; ++i) {
            box(r, 66 + i * 127, 108, 124, 22, i == state.tab ? Color { 43, 64, 37 } : shadow);
            text(r, 83 + i * 127, 116, l10n(tabNames[i]), i == state.tab ? amber : dim);
        }
        box(r, 66, 134, 500, 252, screen);
        outline(r, 65, 133, 502, 254, shadow);
        for (int y = 136; y < 384; y += 3) box(r, 68, y, 496, 1, { 17, 31, 22 });
        if (state.tab == 0) {
            for (int i = 0; i < 24; ++i) {
                int x = 78 + (i % 4) * 122;
                int y = 146 + (i / 4) * 34;
                bool selected = state.selection[0] == i;
                box(r, x, y, 116, 29, selected ? Color { 43, 66, 37 } : Color { 23, 38, 26 });
                outline(r, x, y, 116, 29, selected ? amber : dim);
                text(r, x + 7, y + 11, actionName(quickActions[i]), selected ? amber : green);
            }
            text(r, 82, 365, l10n(TextActionsHint), dim);
        } else if (state.tab == 1) settingsPage(r);
        else if (state.tab == 2) keyboardPage(r);
        else helpPage(r);
        char footer[160];
        snprintf(footer, sizeof(footer), l10n(TextFooter),
            buttonName(SDL_CONTROLLER_BUTTON_A), buttonName(SDL_CONTROLLER_BUTTON_B),
            buttonName(SDL_CONTROLLER_BUTTON_LEFTSHOULDER), buttonName(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        text(r, 67, 400, footer, amber, 1, 83);
        screw(r, 53, 56); screw(r, 580, 56); screw(r, 53, 416); screw(r, 580, 416);
    }
    SDL_RenderPresent(r);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(destination, canvas);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
        SDL_Rect rect { static_cast<int>((logicalW - 640 * factor) / 2), static_cast<int>((logicalH - 480 * factor) / 2), static_cast<int>(640 * factor), static_cast<int>(480 * factor) };
        SDL_RenderCopy(destination, texture, nullptr, &rect);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(canvas);
}

} // namespace pad
} // namespace fallout
