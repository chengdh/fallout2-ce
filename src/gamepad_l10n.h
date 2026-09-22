#ifndef FALLOUT_GAMEPAD_L10N_H_
#define FALLOUT_GAMEPAD_L10N_H_

namespace fallout {
namespace pad {

// The controller overlay follows `[system] language` from `fallout2.cfg`, the
// same setting that picks the game's own text and art. Only English and the
// Chinese translation are translated; every other language keeps English, which
// is what the overlay showed before it could be localized.
void setLanguage(const char* language);
bool isChinese();

// Fixed overlay strings. Entries marked "format" are passed to `snprintf`
// together with the arguments listed in their comment.
enum TextId {
    // Panel chrome.
    TextTabActions, // ACTIONS
    TextTabSettings, // SETTINGS
    TextTabKeyboard, // KEYBOARD
    TextTabHelp, // HELP
    TextTitle, // CONTROLLER INTERFACE
    TextActionsHint, // shortcuts note under the actions grid
    TextFooter, // format: activate, close, previous tab, next tab
    TextHelpTitle, // HELP heading
    // Prompt bar, drawn while the panel is closed.
    TextPromptBar, // format: a, name, b, name, back
    TextPromptMove, // stick/run prompt for direct movement
    TextPromptCursor, // stick prompt for cursor movement
    // Settings: row names.
    TextCursorSpeed,
    TextStickDeadzone,
    TextPrecisionSpeed,
    TextScrollSpeed,
    TextReverseScroll,
    TextSwapSticks,
    TextButtonLabels,
    TextShowPrompts,
    TextActiveController,
    TextBinding, // format: button name
    TextRestoreDefaults,
    TextStickMode,
    TextDisplayMode,
    // Settings: row values.
    TextSpeedValue, // format: pixels per second
    TextPercentValue, // format: percent
    TextNumberValue, // format: number
    TextOn,
    TextOff,
    TextControllerValue, // format: index, count
    TextResetValue,
    TextMoveCharacter,
    TextPointAndClick,
    TextBorderless,
    TextWindowed,
    TextPageValue, // format: page number
    TextDisplayFailed,
    TextSaveFailed,
    TextAutoSave,
    // On-screen keyboard.
    TextKeyboardCount, // format: length, case name
    TextKeyboardUppercase,
    TextKeyboardLowercase,
    TextKeyboardSendHint,
    TextKeyboardReplaceHint,
    TextKeyboardAppendHint,
    TextKeyboardDigitsHint,
    // Remaining button names that are words rather than printed glyphs.
    TextButtonBack,
    TextButtonStart,
    TextButtonUnknown,
    // Status line under the panel title.
    TextUnmappedDevice,
    TextNoController,
    TextCount,
};

const char* l10n(TextId id);

// Indexed tables for the parts of the overlay that are arrays rather than
// individual labels.
const char* actionName(int action); // `Action`, falls back to actions[]'s English
const char* labelStyleName(int style); // Settings: button label styles
const char* keyboardToolName(int index); // On-screen keyboard: command row
const char* helpLine(int index); // HELP page body
const char* faceLabel(int index); // Positional face buttons, 0..3
int helpLineCount();

} // namespace pad
} // namespace fallout

#endif // FALLOUT_GAMEPAD_L10N_H_
