#pragma once

#include <string>

namespace HOL::SteamVR
{
	enum HandleType
	{
		input,
		output,
		skeleton,
		pose,
		HandleType_MAX
	};

	// Convenience class for steamVR input constants
	// and their touch/click/value suffixes
	class InputWrapper
	{

	public:
		InputWrapper(std::string input, HandleType type = HandleType::input);

		std::string touch() const;
		std::string click() const;
		std::string value() const;

		// joystick
		std::string x() const;
		std::string y() const;

		// Because index
		std::string force() const;
		std::string index() const;
		std::string middle() const;
		std::string ring() const;
		std::string pinky() const;

	private:
		std::string mBaseInput;
	};

	bool isTouchInputPath(const std::string& inputPath);
	bool isClickInputPath(const std::string& inputPath);
	std::string getLogicalButtonPath(const std::string& inputPath);
	std::string formatLogicalButtonLabel(const std::string& buttonPath);

	namespace Input
	{
		// These may not be universal
		InputWrapper const Joystick = InputWrapper("joystick");
		InputWrapper const Thumbstick = InputWrapper("thumbstick");
		InputWrapper const Trackpad = InputWrapper("trackpad");
		InputWrapper const Trigger = InputWrapper("trigger");
		InputWrapper const Grip = InputWrapper("grip");
		InputWrapper const A = InputWrapper("a");
		InputWrapper const B = InputWrapper("b");
		InputWrapper const X = InputWrapper("x");
		InputWrapper const Y = InputWrapper("y");
		InputWrapper const Thumbrest = InputWrapper("thumbrest");
		InputWrapper const System = InputWrapper("system");
		InputWrapper const Menu = InputWrapper("menu");
		InputWrapper const IndexPinch = InputWrapper("index_pinch");
		InputWrapper const MiddlePinch = InputWrapper("middle_pinch");
		InputWrapper const RingPinch = InputWrapper("ring_pinch");
		InputWrapper const PinkyPinch = InputWrapper("pinky_pinch");
		InputWrapper const IndexPoint = InputWrapper("index_point");

		// index
		InputWrapper const Finger = InputWrapper("finger");

		// Skeleton
		InputWrapper const Skeleton = InputWrapper("skeleton");
		InputWrapper const Raw = InputWrapper("raw", HandleType::pose);

	} // namespace Input
} // namespace HOL::SteamVR
