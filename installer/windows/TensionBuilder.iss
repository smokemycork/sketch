#define MyAppName "Tension Builder"
#define MyAppVersion "1.0.0"

[Setup]
AppId={{B7F0E8A1-5C3D-4E92-9A61-7D2F4B8C1E55}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=TechHouseTools
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={commonappdata}\TechHouseTools\TensionBuilder
OutputDir=..\..\dist
OutputBaseFilename=TensionBuilder-Setup-Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Files]
Source: "..\..\build\TensionBuilder_artefacts\Release\VST3\Tension Builder.vst3\*"; DestDir: "{commoncf64}\VST3\Tension Builder.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Tension Builder.vst3"
