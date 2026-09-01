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

`HandOfLesser.exe` is also provided as a standalone application, but is only useful for VRChat OSC with Virtual Desktop, with degraded results. 
Installing the complete driver is highly recommended.

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

The recommended setup uses the HandOfLesser Modular Avatar package. Add <https://theroughy.github.io/HandOfLesser/index.json> as a VCC repository and install `HandOfLesser - Modular Avatar` in the avatar project.
Drag the included `HandOfLesser_ModularAvatar` prefab under the avatar root.

You can also copy the `Unity/Assets/HandOfLesser` folder from this git repository into your unity project's Assets folder and manually generate and assign the files, without using modular avatar. You will find the genrator in the `Tools` menu.

Due to the number of synced parameters and generally complexity of the setup, it is recommended you use a dedicated avatar for this rather than attempting to toggle HandOfLesser integration on and off. To avoid confusion it is set up to always replace VRChat's own hand tracking, evne if "Avatar uses finger tracking" is enabled in the VRChat settings. 

You can adjust finger bend and curl behavior in the `VRChat` tab of the HandOfLesser interface.

## Known Issues

### Simultaneous tracking can switch back to controllers unexpectedly

We guess whether hand or controller tracking is in use by how far away from the controller position the hands are; if they are the same you are using the controllers. 
Quest ( 3 in particular ) will often place controllers directly infront of you if they cannot actively be tracked. When that position overlaps with your actual hand position, it switches to controllers. 

Workaround: Enable `Force hand primary` to force it to assume you are hand tracking, even when holding holding the controllers. You will be unable to swtich back to controllers.

## Build Instructions

Clone recursively and build with Visual Studio 2022.

You can build directly with cmake, but OpenVR does build with newer CMake versions, so Visual Studio 2022 is recommended.

`build_all.bat` does all the things.

`build.bat` builds the application and driver.
`build_installer.bat` generates the installer from files in `./output` and downloads the Microsoft Visual C++ Redistributable into a local build cache.
`build_unity_vpm_package.bat` generates the Modular Avatar VPM package.

All files for distribution are output into `distribution/`

## Developer Setup

`register_dev_driver.bat` registers the driver in `./output` with SteamVR, overriding any existing registration.

## Contact

Discord: <https://discord.gg/k9QNcvvJmF>
