include(FetchContent)

FetchContent_Declare(iconv
    GIT_REPOSITORY "https://github.com/sonilyan/libiconv_for_fallout2ce.git"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(iconv)

set(ICONV_LIBRARIES "iconv-static" PARENT_SCOPE)
set(ICONV_INCLUDE_DIRS ${iconv_SOURCE_DIR} ${iconv_BINARY_DIR}/include PARENT_SCOPE)
