#pragma once

#include <array>
#include <string>
#include <unordered_set>
#include <HandOfLesserCommon.h>
#include <GLFW/glfw3.h>
#include "ui_graphics.h"
#include "ui_gesture_debug.h"
#include "visualizer.h"

namespace HOL
{
	class UserInterface
	{
	public:
		UserInterface();
		void init();
		void terminate();
		void onFrame();
		void buildInterface();
		static UserInterface* Current; // We only have a single window for now
		Visualizer* getVisualizer();
		bool shouldCloseWindow();

	private:
		static constexpr int PanelWidth = 600;
		GLFWwindow* mWindow = nullptr;

		void initGLFW();
		void initImgui();
		float mScale = 1.f;
		float mContentScale = 1.f;
		bool mApplyingWindowSize = false;
		static void error_callback(int error, const char* description);
		static void windows_scale_callback(GLFWwindow* window, float xscale, float yscale);
		float scaleSize(float size);
		void updateWindowSize(bool preserveHeight = true);

		Visualizer mVisualizer;
		std::string mImguiIniPath;

		// UI state
		bool mShowAllDevices = false;
		std::vector<std::string> mAvailableOpenXRRuntimes;
		std::string mSelectedTouchOverrideDeviceSerial;
		std::string mSelectedTouchOverrideButtonPath;

		// Gesture binding editor state
		int mEditBindingIndex = -1;
		settings::GestureBinding mEditBinding;
		bool mShowingEditForm = false;
		std::unordered_set<int> mExpandedBindingDebugRows;

		void updateStyles(float scale);

		bool rightAlignButton(const char* label, int verticalLineOffset = 0);
		void showWrappedTooltip(const char* text);
		void InputFloatMultipleSingleLableWithButtons(std::string inputLabelBase,
													  std::string visibleLabel,
													  float smallIncrement,
													  float largeIncrement,
													  std::string format,
													  int width,
													  const std::vector<const float*>& values);

		void InputFloatMultipleTopLableWithButtons(std::string inputLabelBase,
												   const std::vector<std::string>& visibleLabel,
												   float smallIncrement,
												   float largeIncrement,
												   std::string format,
												   int width,
												   const std::vector<const float*>& values);

		void buildSingleFingerTrackingDisplay(const char* windowLabel,
											  HOL::HandSide side,
											  HOL::FingerBend bendValues[HOL::FingerType_MAX],
											  bool treatAsRadians = false,
											  bool treatAsInt = false);
		void buildSingleHandTransformDisplay(HOL::HandSide side);

		void buildMain();
		void buildTracking();
		void buildBodyTracking();
		void buildSteamVR();
		void buildVRChatOSCSettings();
		void buildBindings();
		void buildVisual();
		void buildControllerInput();
		void buildMisc();
		void buildBodyTrackers();
		void buildAbout();
	};
} // namespace HOL
