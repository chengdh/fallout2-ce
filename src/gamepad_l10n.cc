// Localized text for the controller overlay.
//
// The panel and the prompt bar draw their own fixed-pixel text, so their
// strings cannot come from the game's `.msg` files. They are kept here instead,
// next to the code that renders them, and follow the game's `[system] language`
// setting.
//
// The Chinese column is drawn from a 12x12 glyph subset because the overlay
// cannot use the engine's TrueType stack (it rasterizes into its own offscreen
// canvas). `tools/gen_gamepad_cjk.py` scans the literals in this file and
// regenerates `src/gamepad_cjk.h`, so any character added below must be fed
// through that script before it shows up on screen.
//
// This file is UTF-8. MSVC needs `/utf-8` to keep the Chinese literals as UTF-8
// bytes in the binary instead of transcoding them to the local code page; CMake
// sets that for this file in `CMakeLists.txt` and `tests/CMakeLists.txt`.

#include "gamepad_l10n.h"

#include "gamepad_internal.h"

#include <cctype>

namespace fallout {
namespace pad {

enum Language {
    LanguageEnglish,
    LanguageChinese,
};

static Language gLanguage = LanguageEnglish;

struct Entry {
    const char* english;
    const char* chinese;
};

// Every string the overlay can show, one line per `TextId` and in the same
// order. The assertion below catches an omitted entry, which would otherwise
// render as an empty label.
static const Entry texts[] = {
    /* TextTabActions       */ { "ACTIONS", "动作" },
    /* TextTabSettings      */ { "SETTINGS", "设置" },
    /* TextTabKeyboard      */ { "KEYBOARD", "键盘" },
    /* TextTabHelp          */ { "HELP", "帮助" },
    /* TextTitle            */ { "CONTROLLER INTERFACE", "手柄控制界面" },
    /* TextActionsHint      */ { "SHORTCUTS USE THE CURRENT GAME SCREEN'S KEY BINDINGS.", "快捷键沿用当前游戏界面的按键绑定。" },
    /* TextFooter           */ { "%s SELECT   %s CLOSE   %s/%s TABS   D-PAD NAVIGATE", "%s 选择   %s 关闭   %s/%s 换页   十字键 移动" },
    /* TextHelpTitle        */ { "VAULT-TEC FIELD OPERATING INSTRUCTIONS", "VAULT-TEC 现场操作说明" },
    /* TextPromptBar        */ { "%s: %s   %s: %s   %s: PANEL   LT: PRECISION", "%s：%s   %s：%s   %s：面板   LT：精确" },
    /* TextPromptMove       */ { "STICK: MOVE CHARACTER   L3: CURSOR MODE   FULL STICK: RUN", "摇杆：移动角色   L3：光标模式   推满：奔跑" },
    /* TextPromptCursor     */ { "STICK: CURSOR   L3: CHANGE GAMEPLAY MODE   RIGHT STICK: SCROLL", "摇杆：光标   L3：切换游戏模式   右摇杆：滚动" },
    /* TextCursorSpeed      */ { "CURSOR SPEED", "光标速度" },
    /* TextStickDeadzone    */ { "STICK DEADZONE", "摇杆死区" },
    /* TextPrecisionSpeed   */ { "PRECISION SPEED", "精确模式速度" },
    /* TextScrollSpeed      */ { "SCROLL SPEED", "滚动速度" },
    /* TextReverseScroll    */ { "REVERSE SCROLL", "反向滚动" },
    /* TextSwapSticks       */ { "SWAP STICKS", "交换摇杆" },
    /* TextButtonLabels     */ { "BUTTON LABELS", "按键标签" },
    /* TextShowPrompts      */ { "SHOW PROMPTS", "显示操作提示" },
    /* TextActiveController */ { "ACTIVE CONTROLLER", "当前手柄" },
    /* TextBinding          */ { "%s BINDING", "%s 键位" },
    /* TextRestoreDefaults  */ { "RESTORE DEFAULT CONTROLS", "恢复默认键位" },
    /* TextStickMode        */ { "GAMEPLAY STICK MODE", "游戏摇杆模式" },
    /* TextDisplayMode      */ { "DISPLAY MODE", "显示模式" },
    /* TextSpeedValue       */ { "%d PX/SEC", "%d 像素/秒" },
    /* TextPercentValue     */ { "%d %%", "%d %%" },
    /* TextNumberValue      */ { "%d", "%d" },
    /* TextOn               */ { "ON", "开" },
    /* TextOff              */ { "OFF", "关" },
    /* TextControllerValue  */ { "%d / %d", "%d / %d" },
    /* TextResetValue       */ { "SELECT TO RESET", "按确认重置" },
    /* TextMoveCharacter    */ { "MOVE CHARACTER", "移动角色" },
    /* TextPointAndClick    */ { "POINT AND CLICK", "指向点击" },
    /* TextBorderless       */ { "BORDERLESS FULLSCREEN", "无边框全屏" },
    /* TextWindowed         */ { "WINDOWED", "窗口模式" },
    /* TextPageValue        */ { "PAGE %d/3  -  UP/DOWN SELECT  -  LEFT/RIGHT ADJUST", "第 %d/3 页  -  上下选择  -  左右调整" },
    /* TextDisplayFailed    */ { "DISPLAY CHANGE FAILED - TRY ALT+ENTER AGAIN", "显示切换失败 - 请再试一次 ALT+ENTER" },
    /* TextSaveFailed       */ { "COULD NOT SAVE - SETTINGS APPLY FOR THIS SESSION", "保存失败 - 设置仅在本次游戏中生效" },
    /* TextAutoSave         */ { "AUTO SAVE  -  ALT+ENTER: BORDERLESS / WINDOWED", "自动保存  -  ALT+ENTER：无边框 / 窗口" },
    /* TextKeyboardCount    */ { "%d/24  %s", "%d/24  %s" },
    /* TextKeyboardUppercase*/ { "CAPS", "大写" },
    /* TextKeyboardLowercase*/ { "abc", "小写" },
    /* TextKeyboardSendHint */ { "SEND TYPES TEXT. DONE TYPES TEXT AND PRESSES ENTER.", "发送：只输入文本。完成：输入文本并回车。" },
    /* TextKeyboardReplaceHint*/ { "MODE: REPLACE FIELD. DEL EDITS BUFFER. BKSP EDITS GAME.", "模式：覆盖文本。删除：改缓冲。退格：改游戏内文本。" },
    /* TextKeyboardAppendHint*/ { "MODE: APPEND TO FIELD. SELECT MODE TO CHANGE.", "模式：追加到文本末尾。按模式键可切换。" },
    /* TextKeyboardDigitsHint*/ { "USE DIGITS FOR DIALOGUE CHOICES AND ITEM QUANTITIES.", "数字键用于选择对话选项和物品数量。" },
    /* TextButtonBack       */ { "BACK", "返回" },
    /* TextButtonStart      */ { "START", "开始" },
    /* TextButtonUnknown    */ { "BUTTON", "按键" },
    /* TextUnmappedDevice   */ { "UNMAPPED DEVICE - SEE HELP", "手柄未映射 - 见帮助页" },
    /* TextNoController     */ { "NO CONTROLLER CONNECTED", "没有连接手柄" },
};

static_assert(sizeof(texts) / sizeof(texts[0]) == TextCount, "texts must cover every TextId");

// Extras shown next to the printed label of the physical button.
struct Pair {
    const char* english;
    const char* chinese;
};

// `Settings.labels` order: Auto, Xbox, PlayStation, Nintendo, position.
static const Pair labelStyles[5] = {
    { "AUTO", "自动" },
    { "XBOX", "XBOX" },
    { "PLAYSTATION", "PLAYSTATION" },
    { "NINTENDO", "NINTENDO" },
    { "POSITION", "方位" },
};

// On-screen keyboard command row, in `keyboardTools` order.
static const Pair keyboardTools[10] = {
    { "SPACE", "空格" },
    { "DEL", "删除" },
    { "CLEAR", "清空" },
    { "CAPS", "大写" },
    { "BKSP", "退格" },
    { "MODE", "模式" },
    { "YES", "是" },
    { "NO", "否" },
    { "SEND", "发送" },
    { "DONE", "完成" },
};

// Positional face buttons, in SDL face-button order (south, east, west, north).
// The compass names are the ones RetroArch's Chinese translation uses for the
// same label style, so pad owners read them the same way in both frontends.
static const Pair faceNames[4] = {
    { "SOUTH", "南" },
    { "EAST", "东" },
    { "WEST", "西" },
    { "NORTH", "北" },
};

// `Action` order, matching `actions[]` in `gamepad.cc`. The English column is
// only a reminder: `actionName` falls back to `actions[]` itself.
static const Pair actionNames[ActionCount] = {
    { "UNASSIGNED", "未指定" },
    { "CLICK / DRAG", "点击/拖拽" },
    { "CURSOR MODE", "光标模式" },
    { "ENTER / DONE", "确认/完成" },
    { "ESC / OPTIONS", "取消/选项" },
    { "CONTROL PANEL", "控制面板" },
    { "KEYBOARD", "键盘" },
    { "HOLD TO RUN", "按住奔跑" },
    { "INVENTORY", "物品栏" },
    { "CHARACTER", "人物" },
    { "PIP-BOY", "哔哔小子" },
    { "AUTOMAP", "自动地图" },
    { "SKILLDEX", "技能列表" },
    { "REST", "休息" },
    { "SWAP HANDS", "交换武器" },
    { "WEAPON MODE", "武器模式" },
    { "START COMBAT", "开始战斗" },
    { "END TURN", "结束回合" },
    { "CENTER VIEW", "视角居中" },
    { "SAVE GAME", "保存游戏" },
    { "LOAD GAME", "读取游戏" },
    { "SNEAK", "潜行" },
    { "LOCKPICK", "开锁" },
    { "STEAL", "偷窃" },
    { "TRAPS", "陷阱" },
    { "FIRST AID", "急救" },
    { "DOCTOR", "医疗" },
    { "SCIENCE", "科学" },
    { "REPAIR", "修理" },
    { "MOVE / CURSOR", "移动/光标" },
};

static const Pair helpLines[] = {
    { "LEFT STICK : MOVE / CURSOR  L3 : SWITCH MODE", "左摇杆：移动/光标  L3：切换模式" },
    { "LEFT TRIGGER : PRECISION    RIGHT TRIGGER : CLICK", "左扳机：精确模式    右扳机：点击" },
    { "HOLD CLICK TO DRAG ITEMS OR OPEN OBJECT ACTIONS.", "按住点击可拖拽物品或打开对象菜单。" },
    { "D-PAD : ARROW KEYS / LISTS / CAMERA", "十字键：方向键/列表/镜头" },
    { "BACK / SHARE / MINUS OR F11 : CONTROL PANEL", "BACK/SHARE/MINUS 或 F11：控制面板" },
    { "IN PANEL : D-PAD SELECTS. SHOULDERS CHANGE TABS.", "面板内：十字键选择，肩键切换页。" },
    { "TEXT FIELDS OPEN THE KEYBOARD AFTER PAD INPUT.", "手柄输入后，文本框会自动弹出键盘。" },
    { "USE SETTINGS TO CHANGE BUTTONS, SPEED AND LABELS.", "在设置里可以改按键、速度与标签。" },
    { "SDL MAPPINGS SUPPORT USB AND BLUETOOTH GAMEPADS.", "SDL 映射支持 USB 与蓝牙手柄。" },
    { "UNMAPPED PAD? ADD ITS SDL2 MAPPING TO", "手柄无法识别？请把它的 SDL2 映射加入" },
    { "GAMECONTROLLERDB.TXT BESIDE THE GAME EXECUTABLE.", "游戏目录下的 GAMECONTROLLERDB.TXT。" },
    { "MENUS USE CURSOR. COMBAT MOVEMENT USES AP.", "菜单使用光标，战斗移动消耗行动点。" },
};

static bool sameToken(const char* left, const char* right)
{
    while (*left != '\0' && *right != '\0') {
        if (std::tolower(static_cast<unsigned char>(*left)) != std::tolower(static_cast<unsigned char>(*right))) return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

void setLanguage(const char* language)
{
    // `chs` is what the Chinese translation writes into `fallout2.cfg`; the
    // other spellings are accepted so a repackaged translation still works.
    static const char* chineseNames[] = { "chs", "chinese", "zh_cn", "sc" };
    gLanguage = LanguageEnglish;
    if (language == nullptr) return;
    for (const char* name : chineseNames) {
        if (sameToken(language, name)) { gLanguage = LanguageChinese; return; }
    }
}

bool isChinese()
{
    return gLanguage == LanguageChinese;
}

const char* l10n(TextId id)
{
    if (id < 0 || id >= TextCount) return "";
    return isChinese() ? texts[id].chinese : texts[id].english;
}

const char* actionName(int action)
{
    if (action < 0 || action >= ActionCount) return actions[None].name;
    return isChinese() ? actionNames[action].chinese : actions[action].name;
}

const char* labelStyleName(int style)
{
    if (style < 0 || style >= 5) style = 0;
    return isChinese() ? labelStyles[style].chinese : labelStyles[style].english;
}

const char* keyboardToolName(int index)
{
    static const int count = static_cast<int>(sizeof(keyboardTools) / sizeof(keyboardTools[0]));
    if (index < 0 || index >= count) return "";
    return isChinese() ? keyboardTools[index].chinese : keyboardTools[index].english;
}

const char* helpLine(int index)
{
    if (index < 0 || index >= helpLineCount()) return "";
    return isChinese() ? helpLines[index].chinese : helpLines[index].english;
}

int helpLineCount()
{
    return static_cast<int>(sizeof(helpLines) / sizeof(helpLines[0]));
}

const char* faceLabel(int index)
{
    if (index < 0 || index >= 4) return "";
    return isChinese() ? faceNames[index].chinese : faceNames[index].english;
}

} // namespace pad
} // namespace fallout
