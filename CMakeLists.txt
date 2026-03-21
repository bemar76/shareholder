cmake_minimum_required(VERSION 3.20)
project(shareholder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows subsystem - console app
add_executable(shareholder
    src/main.cpp
    src/config_manager.cpp
    src/credential_store.cpp
    src/drive_mapper.cpp
    src/crypto.cpp
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
