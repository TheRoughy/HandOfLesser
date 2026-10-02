cmake_minimum_required (VERSION 3.8)

set(HOL_VERSION "0.9.1.0")

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:EditAndContinue>$<$<CONFIG:RelWithDebInfo>:ProgramDatabase>")
endif()

project(HandOfLesser VERSION ${HOL_VERSION})
set(CMAKE_CXX_STANDARD 20)

find_package(Git QUIET)
set(HOL_BUILD_COMMIT "unknown")
if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		OUTPUT_VARIABLE HOL_BUILD_COMMIT
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
endif()

# App and driver are copied to output directory, along with version string.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/output/version.txt" "${PROJECT_VERSION} (${HOL_BUILD_COMMIT})\n")

add_compile_definitions(HOL_VERSION_STRING=\"${PROJECT_VERSION}\")
add_compile_definitions(HOL_BUILD_COMMIT=\"${HOL_BUILD_COMMIT}\")

# Silence C++17 deprecation warnings
add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
# Silence C++20 deprecation warnings
add_definitions(-D_SILENCE_CXX20_CODECVT_FACETS_DEPRECATION_WARNING)
# Silence security warnings
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# Suppress MSVC warnings for float/double precision loss (we don't care about this)
if(MSVC)
    add_compile_options(/wd4305)  # truncation from 'double' to 'float'
    add_compile_options(/wd4244)  # conversion from 'double' to 'float', possible loss of data
endif()

# Needed for CTest
include(CTest)
enable_testing()

# Needed for GoogleTest
set(gtest_force_shared_crt ON)
include(GoogleTest)

# Needed for OpenXR
add_library(openxr-hpp INTERFACE)
target_include_directories(openxr-hpp INTERFACE lib/openxr-hpp)

# OpenXR extensions not part of the spec yet
add_library(meta_openxr_preview INTERFACE)
target_include_directories(meta_openxr_preview INTERFACE lib/meta_openxr_preview)

# JSON
add_library(json INTERFACE)
target_include_directories(json INTERFACE lib/nlohmann)

# OSC lib
add_library(oscpp INTERFACE)
target_include_directories(oscpp INTERFACE lib/oscpp/include)

# Eigen
add_library(eigen INTERFACE)
target_include_directories(eigen INTERFACE lib/eigen)

# Needed for OpenVR
set(OPENVR_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/openvr")
set(OPENVR_INCLUDE_DIR "${OPENVR_LIB_DIR}/headers")

message( "\${OPENVR_LIB_DIR}: ${OPENVR_LIB_DIR}" )
message( "\${OPENVR_INCLUDE_DIR}: ${OPENVR_INCLUDE_DIR}" )

# Doesn't update unless explicitly cleared first. gg'
unset(OPENVR_LIBRARIES CACHE)

find_library(OPENVR_LIBRARIES
  NAMES
    openvr_api
  PATHS
    ${OPENVR_LIB_DIR}/bin
    ${OPENVR_LIB_DIR}/lib
  PATH_SUFFIXES
    win64
  NO_DEFAULT_PATH
  NO_CMAKE_FIND_ROOT_PATH
)

message( "\${OPENVR_LIBRARIES}: ${OPENVR_LIBRARIES}" )

# TODO: Move to separate CMakeLists.txt
set(IMGUI_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui)
add_library(imgui STATIC
    ${IMGUI_PATH}/imgui.cpp
    ${IMGUI_PATH}/imgui_demo.cpp
    ${IMGUI_PATH}/imgui_draw.cpp
    ${IMGUI_PATH}/imgui_tables.cpp
    ${IMGUI_PATH}/imgui_widgets.cpp

    ${IMGUI_PATH}/backends/imgui_impl_win32.cpp
    ${IMGUI_PATH}/backends/imgui_impl_glfw.cpp
    ${IMGUI_PATH}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_PATH} ${IMGUI_PATH}/backends)
target_link_libraries(imgui PRIVATE glfw)
# ----------------------------------------------------------------------

add_subdirectory(lib/openxr_sdk)
add_subdirectory(lib/openvr)
add_subdirectory(lib/glfw)
add_subdirectory(lib/googletest)
add_subdirectory(lib/minhook)
add_subdirectory(HandOfLesserCommon)
add_subdirectory(HandOfLesser)
add_subdirectory(HandOfLesserDriver)
