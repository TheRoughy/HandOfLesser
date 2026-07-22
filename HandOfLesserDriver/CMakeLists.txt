set(TARGET_NAME 00handoflesser)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../output/drivers")
add_definitions(-D_WIN32)
set(ARCH_TARGET "win64") # This is where Steam looks for drivers on windows

# This is the name of the driver according to SteamVR
set(DRIVER_NAME "driver_${TARGET_NAME}")

add_library(${DRIVER_NAME} SHARED
    src/driver/hmd_driver_factory.cpp
    src/driver/device_provider.cpp
    src/controller/emulated_controller_driver.cpp
    src/hand_simulation.cpp
    src/controller/hooked_controller.cpp
    src/core/hand_of_lesser.cpp
    src/controller/controller_common.cpp
    src/steamvr/hand_tip_pose.cpp
    src/tracker/emulated_tracker_driver.cpp
    src/hooking/Hooking.cpp
    src/hooking/hooks.cpp
    src/input/InputCommons.h
    src/utils/math_utils.cpp
    src/utils/app_launcher.cpp
)

message( "\${CMAKE_RUNTIME_OUTPUT_DIRECTORY}: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" )
message( "\${TARGET_NAME}: ${TARGET_NAME}" )
message( "\${ARCH_TARGET}: ${ARCH_TARGET}" )
message( "\${OPENVR_LIBRARIES}: ${OPENVR_LIBRARIES}" )

# This is so we can build directly to "<binary_dir>/<target_name>/<platform>/<arch>/<driver_name>.<dll/so>"
set_target_properties(${DRIVER_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/bin/${ARCH_TARGET}>
)

add_subdirectory(utils)

target_link_libraries(${DRIVER_NAME} PRIVATE
    ${OPENVR_LIBRARIES}
    util_driverlog
    util_vrmath
    eigen
    minhook
    HandOfLesserCommon
)

target_include_directories(${DRIVER_NAME} PRIVATE ${OPENVR_INCLUDE_DIR})

# Allow us to include from src dir path instead of using relative paths
target_include_directories(${DRIVER_NAME}  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Copy driver assets to output folder
add_custom_command(TARGET ${DRIVER_NAME}
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}
)
