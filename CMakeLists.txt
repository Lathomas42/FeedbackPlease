cmake_minimum_required(VERSION 3.20)
project(FeedbackPlease LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FP_BUILD_GUI "Build the FeedbackPlease GUI (fetches GLFW, Dear ImGui, ImPlot)" ON)
option(FP_WITH_DAQMX "Use NI-DAQmx if it is installed" ON)
set(NIDAQMX_ROOT "C:/Program Files (x86)/National Instruments/Shared/ExternalCompilerSupport/C"
    CACHE PATH "NI-DAQmx ANSI C support folder (contains include/ and lib64/)")

if(MSVC)
  add_compile_options(/W3 /permissive- /utf-8)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# ---- Detection core: pure C++, no hardware or GUI dependencies
add_library(fp_core STATIC
  src/core/Params.cpp
  src/core/WavFile.cpp
  src/core/BandEnvelope.cpp
  src/core/Syllables.cpp
  src/core/FeedbackStateMachine.cpp
  src/core/Detector.cpp
  src/core/Filters.cpp
  src/core/Offline.cpp
  src/core/SongDetector.cpp
  src/core/SongRecorder.cpp
  src/core/SyllableCodes.cpp
  src/core/SynthSong.cpp)
target_include_directories(fp_core PUBLIC src)

# ---- I/O backends + real-time engine
add_library(fp_engine STATIC
  src/io/SimBackend.cpp
  src/io/DaqBackend.cpp
  src/app/Engine.cpp)
target_link_libraries(fp_engine PUBLIC fp_core)

if(FP_WITH_DAQMX)
  find_path(NIDAQMX_INCLUDE_DIR NIDAQmx.h HINTS "${NIDAQMX_ROOT}/include")
  find_library(NIDAQMX_LIBRARY NIDAQmx HINTS "${NIDAQMX_ROOT}/lib64/msvc")
  if(NIDAQMX_INCLUDE_DIR AND NIDAQMX_LIBRARY)
    message(STATUS "NI-DAQmx: ${NIDAQMX_LIBRARY}")
    target_include_directories(fp_engine PRIVATE "${NIDAQMX_INCLUDE_DIR}")
    target_link_libraries(fp_engine PUBLIC "${NIDAQMX_LIBRARY}")
    target_compile_definitions(fp_engine PRIVATE FP_HAVE_DAQMX)
  else()
    message(WARNING "NI-DAQmx not found; building with the simulated source only. Set NIDAQMX_ROOT.")
  endif()
endif()

# ---- Command-line tools and tests
add_executable(offline_detect tools/offline_detect.cpp)
target_link_libraries(offline_detect PRIVATE fp_core)

add_executable(make_test_wavs tools/make_test_wavs.cpp)
target_link_libraries(make_test_wavs PRIVATE fp_core)

add_executable(fp_tests tests/test_core.cpp)
target_link_libraries(fp_tests PRIVATE fp_core)
add_executable(fp_engine_test tests/test_engine.cpp)
target_link_libraries(fp_engine_test PRIVATE fp_engine)
if(NIDAQMX_INCLUDE_DIR AND NIDAQMX_LIBRARY AND FP_WITH_DAQMX)
  target_include_directories(fp_engine_test PRIVATE "${NIDAQMX_INCLUDE_DIR}")
  target_compile_definitions(fp_engine_test PRIVATE FP_HAVE_DAQMX)
endif()
if(WIN32)
  target_link_libraries(fp_engine_test PRIVATE winmm)
endif()

enable_testing()
add_test(NAME core COMMAND fp_tests)
add_test(NAME engine COMMAND fp_engine_test)

# ---- GUI
if(FP_BUILD_GUI)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.90.9 GIT_SHALLOW TRUE)
  FetchContent_Declare(implot GIT_REPOSITORY https://github.com/epezent/implot.git GIT_TAG v0.16 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw imgui implot)
  find_package(OpenGL REQUIRED)

  add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp)
  target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp ${implot_SOURCE_DIR})
  target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

  add_executable(FeedbackPlease src/app/main.cpp src/app/Gui.cpp src/app/Widgets.cpp src/app/TuneView.cpp
    src/app/CrashHandler.cpp)
  target_link_libraries(FeedbackPlease PRIVATE fp_engine imgui_lib)
  if(WIN32)
    target_link_libraries(FeedbackPlease PRIVATE winmm comdlg32 dbghelp shell32 ole32)
  endif()
  if(MSVC)
    # Keep symbols in Release so crash traces have function names and line numbers.
    target_compile_options(FeedbackPlease PRIVATE /Zi)
    target_link_options(FeedbackPlease PRIVATE /DEBUG /OPT:REF /OPT:ICF)
  endif()
endif()
