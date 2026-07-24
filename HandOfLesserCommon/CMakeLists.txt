add_library(HandOfLesserCommon STATIC
	src/transport/transportutil.cpp
	src/transport/udptransport.cpp
	src/transport/namedpipetransport.cpp
	src/math/fingers.cpp
	src/math/math_utils.cpp
	src/hand/finger_bend.cpp
	src/controller/controller.cpp
	src/steamvr/input_wrapper.cpp
	src/steamvr/skeletal_pose_utils.cpp
	src/steamvr/steamvr_tracking_source.cpp
	src/settings/settings.cpp
)

target_link_libraries(HandOfLesserCommon PUBLIC
	json
	eigen
	openxr_loader
	ws2_32
)

target_include_directories(HandOfLesserCommon
	INTERFACE include
	PRIVATE include
	PUBLIC ${OPENVR_INCLUDE_DIR}
)

# Allow us to include from src dir path instead of using relative paths
target_include_directories(HandOfLesserCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(HandOfLesserCommon.Tests
	tests/test_example.cpp
)

target_link_libraries(HandOfLesserCommon.Tests PRIVATE
	HandOfLesserCommon
	gtest
	gtest_main
)

gtest_discover_tests(HandOfLesserCommon.Tests)
