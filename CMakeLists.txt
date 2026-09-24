cmake_minimum_required(VERSION 3.21)

project(CodexUsageBar VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(APP_VERSION_TEXT "")
if(DEFINED APP_VERSION AND NOT APP_VERSION STREQUAL "")
    set(APP_VERSION_TEXT "${APP_VERSION}")
elseif(DEFINED ENV{GITHUB_REF_NAME} AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
    set(APP_VERSION_TEXT "$ENV{GITHUB_REF_NAME}")
else()
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE APP_VERSION_TEXT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
endif()

if(APP_VERSION_TEXT STREQUAL "")
    set(APP_VERSION_TEXT "dev")
endif()

set(USAGE_BAR_SOURCES
    src/AccountLibrary.cpp
    src/AppFeatures.cpp
    src/AppUpdate.cpp
    src/BrowserSignIn.cpp
    src/GrokBilling.cpp
    src/ProxyConfig.cpp
    src/QuotaEstimate.cpp
    src/ResetStatus.cpp
    src/SessionIndex.cpp
    src/JsonLite.cpp
)

add_executable(CodexUsageBar WIN32
    src/main.cpp
    src/AppBarWindow.cpp
    src/CodexUsageFetcher.cpp
    ${USAGE_BAR_SOURCES}
)

add_executable(CodexFeatureTests
    src/FeatureTests.cpp
    src/CodexUsageFetcher.cpp
    ${USAGE_BAR_SOURCES}
)

target_include_directories(CodexUsageBar PRIVATE src)

target_compile_definitions(CodexUsageBar PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    APP_VERSION_W=L\"${APP_VERSION_TEXT}\"
)

target_compile_options(CodexUsageBar PRIVATE /utf-8)

target_link_libraries(CodexUsageBar PRIVATE
    advapi32
    bcrypt
    crypt32
    d2d1
    dwrite
    gdi32
    ole32
    secur32
    shell32
    shlwapi
    comdlg32
    user32
    winhttp
    ws2_32
)

target_include_directories(CodexFeatureTests PRIVATE src)
target_compile_definitions(CodexFeatureTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(CodexFeatureTests PRIVATE /utf-8)
target_link_libraries(CodexFeatureTests PRIVATE
    advapi32
    bcrypt
    crypt32
    ole32
    secur32
    shell32
    shlwapi
    user32
    winhttp
    ws2_32
)
