cmake_minimum_required(VERSION 3.15)
project(RDPCallRecorder VERSION 2.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =============================================================
# Path to cloned AudioCapture repository
# =============================================================
set(AUDIOCAPTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../AudioCapture" CACHE PATH "Path to AudioCapture repository")

if(NOT EXISTS "${AUDIOCAPTURE_DIR}/include/AudioCapture.h")
    message(FATAL_ERROR
        "AudioCapture library not found at: ${AUDIOCAPTURE_DIR}\n"
        "Please clone it:\n"
        "  git clone https://github.com/masonasons/AudioCapture.git\n"
        "And set AUDIOCAPTURE_DIR if needed:\n"
        "  cmake -DAUDIOCAPTURE_DIR=C:/path/to/AudioCapture .."
    )
endif()

# =============================================================
# Source files (MP3 + WAV, no Opus/FLAC)
# =============================================================
set(SOURCES
    src/main.cpp

    # AudioCapture core
    ${AUDIOCAPTURE_DIR}/src/AudioCapture.cpp
    ${AUDIOCAPTURE_DIR}/src/ProcessEnumerator.cpp
    ${AUDIOCAPTURE_DIR}/src/CaptureManager.cpp
    ${AUDIOCAPTURE_DIR}/src/Mp3Encoder.cpp
    ${AUDIOCAPTURE_DIR}/src/WavWriter.cpp
    ${AUDIOCAPTURE_DIR}/src/AudioDeviceEnumerator.cpp
    ${AUDIOCAPTURE_DIR}/src/AudioMixer.cpp

    # Stubs for OpusEncoder and FlacEncoder
    src/OpusEncoder_stub.cpp
    src/FlacEncoder_stub.cpp

    # Windows resource file (application icon)
    src/resource.rc
)

# =============================================================
# Executable
# =============================================================
add_executable(RDPCallRecorder WIN32 ${SOURCES})

# Include directories - our stub headers take priority
target_include_directories(RDPCallRecorder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${AUDIOCAPTURE_DIR}/include
)

# =============================================================
# Link Windows libraries (including GUI: comctl32, comdlg32)
# =============================================================
target_link_libraries(RDPCallRecorder PRIVATE
    Avrt.lib
    Mmdevapi.lib
    Ksuser.lib
    Mfplat.lib
    Mfreadwrite.lib
    Mfuuid.lib
    Ole32.lib
    Shell32.lib
    Propsys.lib
    RuntimeObject.lib
    Delayimp.lib
    Comctl32.lib
    Comdlg32.lib
    User32.lib
    Gdi32.lib
    Advapi32.lib
    Secur32.lib
)

# =============================================================
# Build properties
# =============================================================
set_target_properties(RDPCallRecorder PROPERTIES
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "/DELAYLOAD:api-ms-win-core-winrt-l1-1-0.dll"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "RDPCallRecorder"
)

# Compile definitions
target_compile_definitions(RDPCallRecorder PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NO_OPUS_ENCODER
    NO_FLAC_ENCODER
)

# Warnings
if(MSVC)
    target_compile_options(RDPCallRecorder PRIVATE /W3)
else()
    target_compile_options(RDPCallRecorder PRIVATE -Wall -Wextra)
endif()
