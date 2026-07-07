;--------------------------------
; Modern UI

	!include "MUI2.nsh"
	!include "x64.nsh"

;--------------------------------
; General

	!ifndef VERSION
	!define VERSION "0.0.0.0"
	!endif

	!define DRIVER_NAME "00handoflesser"
	!define DRIVER_OUTDIR "output\drivers\${DRIVER_NAME}"
	!define COMPANY_NAME "Nordskog"
	!define PRODUCT_NAME "HandOfLesser"
	!define APP_REG_KEY "Software\${COMPANY_NAME}\${PRODUCT_NAME}"
	!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY_NAME} ${PRODUCT_NAME}"
	!ifndef VC_REDIST_EXE
	!define VC_REDIST_EXE "build_cache\vc_redist.x64.exe"
	!endif

	Name "${PRODUCT_NAME}"
	OutFile "distribution\HandOfLesserInstaller.exe"
	InstallDir "$PROGRAMFILES64\${COMPANY_NAME}\${PRODUCT_NAME}"
	InstallDirRegKey HKLM "${APP_REG_KEY}\Main" ""
	RequestExecutionLevel admin
	ShowInstDetails show

	VIProductVersion "${VERSION}"
	VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_NAME}"
	VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_NAME} Installer"
	VIAddVersionKey /LANG=1033 "LegalCopyright" "Open source at https://github.com/Nordskog/HandOfLesser"
	VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
	VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"

;--------------------------------
; Variables

	Var vrPathReg

;--------------------------------
; Interface Settings

	!define MUI_ABORTWARNING

;--------------------------------
; Pages

	!insertmacro MUI_PAGE_LICENSE "LICENSE.md"
	!insertmacro MUI_PAGE_DIRECTORY
	!insertmacro MUI_PAGE_INSTFILES

	!insertmacro MUI_UNPAGE_CONFIRM
	!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages

	!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Functions

Function findVrPathReg
	StrCpy $vrPathReg ""

	ReadRegStr $0 HKLM "Software\WOW6432Node\Valve\Steam" "InstallPath"
	IfFileExists "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return

	ReadRegStr $0 HKCU "Software\Valve\Steam" "SteamPath"
	IfFileExists "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return

	IfFileExists "$PROGRAMFILES32\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$PROGRAMFILES32\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return

	IfFileExists "$PROGRAMFILES64\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$PROGRAMFILES64\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return
FunctionEnd

Function .onInit
	${IfNot} ${RunningX64}
		MessageBox MB_OK|MB_ICONSTOP "${PRODUCT_NAME} only supports 64-bit Windows."
		Abort
	${EndIf}

	SetRegView 64

	ReadRegStr $R0 HKLM "${UNINSTALL_KEY}" "UninstallString"
	StrCmp $R0 "" done

	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION \
			"SteamVR is still running. Cannot install ${PRODUCT_NAME}.$\nPlease close SteamVR and try again."
		Abort

	done:
FunctionEnd

;--------------------------------
; Installer Sections

Section "Install" SecInstall
	SetRegView 64
	SetShellVarContext all

	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION \
			"SteamVR is still running. Cannot install ${PRODUCT_NAME}.$\nPlease close SteamVR and try again."
		Abort

	Call findVrPathReg
	StrCmp $vrPathReg "" 0 foundvrpathreg
		MessageBox MB_OK|MB_ICONSTOP \
			"Could not find SteamVR's vrpathreg.exe.$\nPlease install SteamVR or start SteamVR once before installing ${PRODUCT_NAME}."
		Abort

	foundvrpathreg:
	DetailPrint "Using vrpathreg: $vrPathReg"

	InitPluginsDir
	SetOutPath "$PLUGINSDIR"
	File "${VC_REDIST_EXE}"
	DetailPrint "Installing Microsoft Visual C++ Redistributable..."
	ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart' $0
	DetailPrint "VC++ Redistributable installer exit code: $0"

	SetOutPath "$INSTDIR"
	File "LICENSE.md"
	File "README.md"

	SetOutPath "$INSTDIR\${DRIVER_NAME}"
	File "${DRIVER_OUTDIR}\driver.vrdrivermanifest"

	SetOutPath "$INSTDIR\${DRIVER_NAME}\bin\win64"
	File "${DRIVER_OUTDIR}\bin\win64\driver_00handoflesser.dll"

	SetOutPath "$INSTDIR\${DRIVER_NAME}\resources"
	File "${DRIVER_OUTDIR}\resources\driver.vrresources"

	SetOutPath "$INSTDIR\${DRIVER_NAME}\resources\bin\win64"
	File "${DRIVER_OUTDIR}\resources\bin\win64\HandOfLesser.exe"
	File "${DRIVER_OUTDIR}\resources\bin\win64\HandOfLesser.pdb"
	File "${DRIVER_OUTDIR}\resources\bin\win64\openvr_api.dll"

	SetOutPath "$INSTDIR\${DRIVER_NAME}\resources\icons"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_error.png"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_off.png"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_ready.png"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_ready_alert.png"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_ready_low.png"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_searching.gif"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_searching_alert.gif"
	File "${DRIVER_OUTDIR}\resources\icons\controller_status_standby.png"

	SetOutPath "$INSTDIR\${DRIVER_NAME}\resources\input"
	File "${DRIVER_OUTDIR}\resources\input\controller_profile.json"
	File "${DRIVER_OUTDIR}\resources\input\touch_profile.json"

	SetOutPath "$INSTDIR\${DRIVER_NAME}\resources\localization"
	File "${DRIVER_OUTDIR}\resources\localization\localization.json"

	SetOutPath "$INSTDIR\${DRIVER_NAME}\resources\settings"
	File "${DRIVER_OUTDIR}\resources\settings\default.vrsettings"

	DetailPrint "Registering SteamVR driver..."
	ExecWait '"$vrPathReg" adddriver "$INSTDIR\${DRIVER_NAME}"' $0
	DetailPrint "vrpathreg adddriver exit code: $0"
	nsExec::ExecToLog '"$INSTDIR\${DRIVER_NAME}\resources\bin\win64\HandOfLesser.exe" -activatemultipledrivers'

	WriteRegStr HKLM "${APP_REG_KEY}\Main" "" "$INSTDIR"
	WriteRegStr HKLM "${APP_REG_KEY}\Driver" "" "$INSTDIR\${DRIVER_NAME}"
	WriteRegStr HKLM "${APP_REG_KEY}\SteamVR" "VrPathReg" "$vrPathReg"

	WriteUninstaller "$INSTDIR\Uninstall.exe"
	WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
	WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
	WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${COMPANY_NAME}"
	WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""

SectionEnd

;--------------------------------
; Uninstaller Section

Section "Uninstall"
	SetRegView 64
	SetShellVarContext all

	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION \
			"SteamVR is still running. Cannot uninstall ${PRODUCT_NAME}.$\nPlease close SteamVR and try again."
		Abort

	ReadRegStr $vrPathReg HKLM "${APP_REG_KEY}\SteamVR" "VrPathReg"
	IfFileExists "$vrPathReg" founduninstallvrpathreg 0
		Call un.findVrPathReg

	founduninstallvrpathreg:
	StrCmp $vrPathReg "" skipunregister
		DetailPrint "Unregistering SteamVR driver..."
		ExecWait '"$vrPathReg" removedriver "$INSTDIR\${DRIVER_NAME}"' $0
		DetailPrint "vrpathreg removedriver exit code: $0"

	skipunregister:
	RMDir /r "$INSTDIR\${DRIVER_NAME}"
	Delete "$INSTDIR\LICENSE.md"
	Delete "$INSTDIR\README.md"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"

	DeleteRegKey HKLM "${APP_REG_KEY}\Main"
	DeleteRegKey HKLM "${APP_REG_KEY}\Driver"
	DeleteRegKey HKLM "${APP_REG_KEY}\SteamVR"
	DeleteRegKey HKLM "${APP_REG_KEY}"
	DeleteRegKey HKLM "Software\${COMPANY_NAME}"
	DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd

Function un.findVrPathReg
	StrCpy $vrPathReg ""

	ReadRegStr $0 HKLM "Software\WOW6432Node\Valve\Steam" "InstallPath"
	IfFileExists "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return

	ReadRegStr $0 HKCU "Software\Valve\Steam" "SteamPath"
	IfFileExists "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$0\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return

	IfFileExists "$PROGRAMFILES32\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$PROGRAMFILES32\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return

	IfFileExists "$PROGRAMFILES64\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" 0 +3
		StrCpy $vrPathReg "$PROGRAMFILES64\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
		Return
FunctionEnd
