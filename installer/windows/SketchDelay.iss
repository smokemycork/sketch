; Inno Setup script - Windows installer for Sketch Delay (VST3)
#define MyAppName "Sketch Delay"
#define MyAppVersion "1.0.0"

[Setup]
AppId={{3C5E2A77-9B14-4F6D-8E21-5A7D90C4B1F8}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=TechHouseTools
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={commonappdata}\TechHouseTools\SketchDelay
OutputDir=..\..\dist
OutputBaseFilename=SketchDelay-Setup-Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Files]
Source: "..\..\build\SketchDelay_artefacts\Release\VST3\Sketch Delay.vst3\*"; \
    DestDir: "{commoncf64}\VST3\Sketch Delay.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Sketch Delay.vst3"

[Messages]
FinishedLabel=Sketch Delay was installed to your VST3 folder.%n%nIn Ableton Live: Preferences > Plug-Ins > turn on "Use VST3 Plug-In System Folders", then click Rescan. Find it under Plug-Ins > Sketch Delay.
