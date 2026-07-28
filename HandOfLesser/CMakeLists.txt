add_executable(HandOfLesser
	src/core/ui/display_global.cpp
	src/HandOfLesser.cpp
	src/core/HandOfLesserCore.cpp
	src/core/features_manager.cpp
	src/openxr/HandTracking.cpp
	src/openxr/HandTrackingInterface.cpp
	src/openxr/InstanceHolder.cpp
	src/openxr/virtual_desktop_tracking_source.cpp

	src/core/settings_global.cpp
	src/core/state_global.cpp
	src/openxr/openxr_hand.cpp
	src/hands/pose_filter.cpp
	src/core/ui/user_interface.cpp
	src/core/ui/ui_gesture_debug.cpp
	src/core/ui/ui_graphics.cpp
	src/openxr/XrUtils.cpp
	src/oculus/oculus_hacks.cpp
	src/vrchat/vrchat_osc.cpp
	src/openxr/openxr_state.cpp
	src/windows/crash_handler.cpp
	src/windows/console_log.cpp
	src/windows/high_resolution_timer.cpp
	src/windows/windows_utils.cpp
	src/core/ui/visualizer.cpp
	src/openxr/xr_joint_utils.cpp
	src/hands/gesture/base_gesture.cpp
	src/hands/gesture/proximity_gesture.cpp
	src/hands/gesture/above_below_curl_plane_gesture.cpp
	src/hands/gesture/open_hand_pinch_gesture.cpp
	src/hands/gesture/hold_gesture.cpp
	src/hands/gesture/facing_gesture.cpp
	src/hands/gesture/gate_gesture.cpp
	src/hands/action/base_action.cpp
	src/hands/input/base_input.cpp
	src/hands/action/hand_drag_action.cpp
	
	src/hands/gesture/chain_gesture.cpp
	src/util/hol_utils.cpp
	src/hands/input/settings_toggle_input.cpp
	src/hands/action/trigger_action.cpp
	src/hands/action/button_action.cpp
	
	src/hands/gesture/finger_curl_gesture.cpp
	src/hands/gesture/combo_gesture.cpp
	src/hands/gesture/aim_gesture.cpp
	src/hands/gesture/inverse_gesture.cpp
	src/hands/gesture_binding_builder.cpp
	src/vrchat/vrchat_input.cpp
	src/hands/input/osc_float_input.cpp
	src/hands/input/osc_alternate_float_input.cpp
	src/steamvr/steamvr_input.cpp
	src/steamvr/steamvr_setup.cpp
	src/hands/input/steamvr_float_input.cpp
	src/hands/input/steamvr_bool_input.cpp
	src/steamvr/skeletal_input.cpp
	
	src/openxr/body_tracking.cpp
	src/openxr/openxr_body.cpp
)

find_package(OpenGL REQUIRED)

target_link_libraries(HandOfLesser PRIVATE
	dbghelp
	d3d11
	eigen
	glfw
	HandOfLesserCommon
	imgui
	openxr_loader
	openxr-hpp
	oscpp
	meta_openxr_preview
	${OPENGL_LIBRARY}
	${OPENVR_LIBRARIES}
)

target_include_directories(HandOfLesser PRIVATE ${OPENVR_INCLUDE_DIR})

# Allow us to include from src dir path instead of using relative paths
target_include_directories(HandOfLesser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(HandOfLesser PUBLIC
	NOMINMAX
	WIN32_LEAN_AND_MEAN
	XR_KHR_D3D11_enable
	XR_USE_GRAPHICS_API_D3D11
	XR_USE_PLATFORM_WIN32
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../output)
set(ARCH_TARGET "win64")
set_target_properties(HandOfLesser PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/drivers/00handoflesser/resources/bin/${ARCH_TARGET}>
	PDB_OUTPUT_DIRECTORY $<1:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/drivers/00handoflesser/resources/bin/${ARCH_TARGET}>
)

add_custom_command(TARGET HandOfLesser
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${OPENVR_LIB_DIR}/bin/${ARCH_TARGET}/openvr_api.dll
		${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/drivers/00handoflesser/resources/bin/${ARCH_TARGET}/openvr_api.dll
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		$<TARGET_PDB_FILE:HandOfLesser>
		${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/drivers/00handoflesser/resources/bin/${ARCH_TARGET}/
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
	set_property(TARGET HandOfLesser PROPERTY CXX_STANDARD 20)
endif()

if (MSVC)
	target_compile_options(HandOfLesser PRIVATE
		$<$<CONFIG:Release>:/Zi>
		$<$<CONFIG:RelWithDebInfo>:/Zi>
	)
	target_link_options(HandOfLesser PRIVATE
		$<$<CONFIG:Release>:/DEBUG:FULL>
		$<$<CONFIG:RelWithDebInfo>:/DEBUG:FULL>
	)
endif()
