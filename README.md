# HandOfLesser

HandOfLesser is a SteamVR driver that uses OpenXR data from VDXR or Quest Link to emulate controllers and body trackers in SteamVR, with a few extra tools for VRChat and controller input cleanup.

## Features

HandOfLesser provides:

- Support for Quest Link ( Oculus OpenXR runtime ).
- Customizable gesture inputs that work in any SteamVR game.
- Hand tracking with fallback to body tracking instead of leaving hands frozen in place.
- Per-joint hand tracking in VRChat using OSC ( VRChat's own hand tracking only transmits per-finger curl; what you see locally is not what other people are seeing )
- Disable capacitive touch on controller buttons ( because it's a common issue )

When using Quest Link you also get:

- Simultaneous controller and hand tracking.
- Use controllers as trackers while hand tracking ( Quest 2 only, Quest 3 controllers have terrible tracking )

## Installation

Download and run the installer from Releases. This installs the SteamVR add-on and the HandOfLesser desktop interface.

Hand and body tracking must be enabled on the Quest.

### Virtual Desktop

Enable `Forward tracking data` inside Virtual Desktop's Quest interface. The option is in the `Streaming` section.

### Quest Link

In the Meta Horizon Link PC app, go to `Settings` > `Developer` and enable `Developer runtime features`.

Because Quest settings are not easily accessible while in Quest Link, also enable `Automatically switch between controllers and hands` before starting Link.

1. Launch SteamVR.
2. Wait for the HandOfLesser desktop interface to open.
3. If the interface steals focus from SteamVR, press the system button on the right controller to pause SteamVR.
4. Select the SteamVR window.
5. Press the system button again to resume SteamVR.

## Basic Usage

When the SteamVR add-on is enabled, the HandOfLesser interface opens on your desktop.

The system default OpenXR runtime is used by default. You can explicitly select a runtime in the `Main` tab:

- Use `Auto` to keep the system default runtime.
- Use `virtualdesktop-openxr` for Virtual Desktop.
- Use `oculus_openxr_64` for Quest Link.

Click `Restart` after changing the runtime.

Set `Hand tracking mode` to `Emulate separate controller` to enable controller emulation.

Review the default gesture inputs in the `Input` tab before continuing. You may want to customize them.

Most options in the interface have tooltips explaining what they do.

## VRChat OSC

Install `HandOfLesser.unitypackage` in your Unity VRChat avatar project.

In Unity, open the `Hand Of Lesser` window and press each button in order.

This generates:

- `Assets\HandOfLesser\generated\handoflesser_parameters`
- `Assets\HandOfLesser\generated\handoflesser_controller`

Assign the generated parameters to your avatar parameters, and the generated controller to your gesture layer. 
Combining these with existing parameters and controllers is left as an exercise for the user.

You can adjust finger bend and curl behavior in the `VRChat` tab of the HandOfLesser interface.

## Known Issues

### Virtual Desktop hand tracking is choppy

Running VDXR alongside Virtual Desktop's SteamVR Add-on results in each only receiving half the data. They are aware and may eventually fix the issue. 

Workaround: Disable the Virtual Desktop SteamVR add-on and launch SteamVR using the "Enter VR" button in Virtual Desktop's Quest interface. 

### Simultaneous tracking can switch back to controllers unexpectedly

We guess whether hand or controller tracking is in use by how far away from the controller position the hands are; if they are the same you are using the controllers. 
Quest ( 3 in particular ) will often place controllers directly infront of you if they cannot actively be tracked. When that position overlaps with your actual hand position, it switches to controllers. 

Workaround: Enable `Force hand primary` to force it to assume you are hand tracking, even when holding holding the controllers. You will be unable to swtich back to controllers.

## Build Instructions

Clone recursively and build with Visual Studio 2022.

You can build directly with cmake, but OpenVR does build with newer CMake versions, so Visual Studio 2022 is recommended.

`build_installer.bat` generates the installer from files in `./output` and downloads the Microsoft Visual C++ Redistributable into a local build cache. It copies the installer and a standalone `HandOfLesser.exe` desktop interface to `./distribution`.

Use Unity to export the contents of `./Unity` as `HandOfLesser.UnityPackage`, then copy it into `./distribution` manually.

## Developer Setup

`register_dev_driver.bat` registers the driver in `./output` with SteamVR, overriding any existing registration.

## Contact

Discord: <https://discord.gg/k9QNcvvJmF>
