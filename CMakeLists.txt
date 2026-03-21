cmake_minimum_required(VERSION 3.20)

# Version kann per CI-Tag überschrieben werden: -DSHAREHOLDER_VERSION=1.2.3
set(SHAREHOLDER_VERSION "1.0.0" CACHE STRING "Application version")

project(shareholder VERSION ${SHAREHOLDER_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Aktuelles Jahr für den Copyright-String in version.rc
string(TIMESTAMP COPYRIGHT_YEAR "%Y")

# version.rc aus Template generieren (CMake ersetzt @VARIABLEN@)
configure_file(
    src/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)

add_executable(shareholder
    src/main.cpp
    src/config_manager.cpp
    src/credential_store.cpp
    src/drive_mapper.cpp
    src/crypto.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

target_include_directories(shareholder PRIVATE src)

# Link Windows libraries
target_link_libraries(shareholder PRIVATE
    mpr          # WNetAddConnection2
    credui       # Credential UI
    advapi32     # Credential Manager (CredRead/CredWrite)
    bcrypt       # Windows CNG (AES-256-GCM)
    crypt32      # CryptProtectData
)

# Enforce Unicode
target_compile_definitions(shareholder PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Static runtime to produce standalone EXE
set_property(TARGET shareholder PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

install(TARGETS shareholder DESTINATION bin)
