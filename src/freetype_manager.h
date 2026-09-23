#ifndef FREETYPE_MANAGER_H
#define FREETYPE_MANAGER_H

#include "text_font.h"

namespace fallout {

extern FontManager gFtFontManager;

int FtFontsInit();
void FtFontsExit();

// Returns the iconv encoding name of the currently active FreeType font
// (e.g. "GBK" for Chinese, "CP1252" for Western), or nullptr when no
// FreeType font is active. Used by the pip-boy server to transcode game
// text into UTF-8 before JSON serialization.
const char* ftGetActiveEncoding();

} // namespace fallout

#endif /* FONT_MANAGER_H */
