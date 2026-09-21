# Build static lib only.
set(BUILD_SHARED_LIBS FALSE)

# Only the TrueType rasterizer is needed - no optional codecs.
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_BROTLI ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_ZLIB ON)
set(FT_CONFIG_OPTION_USE_ZLIB OFF)

include(FetchContent)

FetchContent_Declare(freetype
    GIT_REPOSITORY "https://github.com/freetype/freetype"
    GIT_TAG "VER-2-12-1"
    # Without this the whole FreeType history is cloned, which makes
    # configuring the project painfully slow.
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    # FreeType's only submodule (subprojects/dlg) is not used by its build
    # system - it is part of the tools bundled with the FreeType sources.
    # Skipping it avoids a needless clone that fails behind restrictive
    # networks.
    GIT_SUBMODULES ""
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(freetype)

# Use the copy of ftoption.h shipped with this project instead of the one that
# comes with FreeType. It enables `TT_CONFIG_OPTION_SUBPIXEL_HINTING 2`, which
# gives noticeably better results for the small pixel-aligned sizes Fallout 2
# renders its interface text at.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ftoption.h" ORIGINAL_FTOPTION_H)
file(WRITE "${freetype_BINARY_DIR}/include/freetype/config/ftoption.h" "${ORIGINAL_FTOPTION_H}")

set(FREETYPE_LIBRARIES "freetype" PARENT_SCOPE)
set(FREETYPE_INCLUDE_DIRS ${freetype_SOURCE_DIR} ${freetype_BINARY_DIR} PARENT_SCOPE)
