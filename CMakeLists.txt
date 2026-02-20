cmake_minimum_required(VERSION 3.15)
project(RDPCallRecorder VERSION 2.6.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(AUDIOCAPTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../AudioCapture" CACHE PATH "Path to AudioCapture repository")

if(NOT EXISTS "${AUDIOCAPTURE_DIR}/include/AudioCapture.h")
    message(FATAL_ERROR
        "AudioCapture not found at: ${AUDIOCAPTURE_DIR}\n"
        "Clone it: git clone https://github.com/masonasons/AudioCapture.git\n"
        "Or set: cmake -DAUDIOCAPTURE_DIR=C:/path/to/AudioCapture ..")
endif()

set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/Logger.cpp
    src/Utils.cpp
    src/ProcessUtils.cpp
    src/AudioMonitor.cpp
    src/MonitorThread.cpp
    src/TrayIcon.cpp
    src/SettingsDialog.cpp
    src/MainPanel.cpp
    src/AutoUpdate.cpp
    src/WindowUtils.cpp
    ${AUDIOCAPTURE_DIR}/src/AudioCapture.cpp
    ${AUDIOCAPTURE_DIR}/src/ProcessEnumerator.cpp
    ${AUDIOCAPTURE_DIR}/src/CaptureManager.cpp
    ${AUDIOCAPTURE_DIR}/src/Mp3Encoder.cpp
    ${AUDIOCAPTURE_DIR}/src/WavWriter.cpp
    ${AUDIOCAPTURE_DIR}/src/AudioDeviceEnumerator.cpp
    ${AUDIOCAPTURE_DIR}/src/AudioMixer.cpp
    src/OpusEncoder_stub.cpp
    src/FlacEncoder_stub.cpp
    src/resource.rc
)

add_executable(RDPCallRecorder WIN32 ${SOURCES})

target_include_directories(RDPCallRecorder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${AUDIOCAPTURE_DIR}/include
)

target_link_libraries(RDPCallRecorder PRIVATE
    Avrt Mmdevapi Ksuser Mfplat Mfreadwrite Mfuuid Ole32 Shell32 Propsys
    RuntimeObject Delayimp Comctl32 Comdlg32 User32 Gdi32 Advapi32 Secur32 Winhttp
)

set_target_properties(RDPCallRecorder PROPERTIES
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "/DELAYLOAD:api-ms-win-core-winrt-l1-1-0.dll"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "RDPCallRecorder"
)

target_compile_definitions(RDPCallRecorder PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX NO_OPUS_ENCODER NO_FLAC_ENCODER)

if(MSVC)
    target_compile_options(RDPCallRecorder PRIVATE /W3)
else()
    target_compile_options(RDPCallRecorder PRIVATE -Wall -Wextra)
endif()
