; Class Widgets Next（C++/Qt Quick 版）Windows 安装器 —— Inno Setup 6
; 移植自上游 Class-Widgets-2 的 scripts/cwn-cicd.iss（归属声明见仓库 THIRD_PARTY.md / README.md）。
; 相对上游的差异（其余段照上游保留）：
;   - 可执行文件名为 ClassWidgetsNext.exe（无空格），Source 指向本仓库的 dist/
;   - AppId 改用 com.classwidgets.* 反域名系（见 [Setup] 注释）
;   - 多语言 .isl 未随本仓库 vendor，暂只保留英文（见 [Languages] 注释）
; CI 用法（.github/workflows/build&release.yml）：环境变量 CWN_VERSION=<版本号> 后执行
;   iscc scripts\cwn-cicd.iss
; 本地用法：先 `cmake --install build --prefix dist`，设置 CWN_VERSION 后再执行 iscc。

#define MyAppName "Class Widgets Next"
#define MyAppVersion GetEnv("CWN_VERSION")
#if MyAppVersion == ""
  #define MyAppVersion "0.0.0"
#endif
#define MyAppPublisher "Class Widgets Next Contributors"
#define MyAppURL "https://github.com/lkjhg10576/Class_Widgets_Next"
; 注意：C++ 版产物与上游 PyInstaller 的 "Class Widgets Next.exe" 不同名（无空格）
#define MyAppExeName "ClassWidgetsNext.exe"

[Setup]
; AppId 采用 com.classwidgets.* 反域名系，与上游应用的持久化标识约定同族
; （上游窗口/配置标识即 com.classwidgets.settings、com.classwidgets.schedules 等，见
;   上游 src/core/central.py；上游 Python 版安装器本身用 GUID
;   {02ED9969-BFB5-45C7-8909-E4D4353D1EF4}）。本仓库为独立产品线，AppId 与上游安装器
; 不同，避免在“应用和功能”里互相识别、覆盖卸载。
AppId=com.classwidgets.next
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
; "ArchitecturesAllowed=x64compatible" specifies that Setup cannot run on anything but x64 and Windows 11 on Arm.
ArchitecturesAllowed=x64compatible
; "ArchitecturesInstallIn64BitMode=x64compatible" requests that the install be done in "64-bit mode" on x64 or Windows 11 on Arm.
; This means it should use the native 64-bit Program Files directory and the 64-bit view of the registry.
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
; Uncomment the following line to run in non administrative install mode (install for current user only).
;PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
OutputDir=..\output
OutputBaseFilename=ClassWidgets-{#MyAppVersion}-Win-Installer
SolidCompression=yes
WizardStyle=modern windows11
DefaultDialogFontName=Microsoft YaHei

[Languages]
; 上游附带的 Installer_Languages\ChineseSimplified.isl / ChineseTraditional.isl / Japanese.isl
; 未随本仓库分发（不属于本次允许新增的文件）；需要多语言时把对应 .isl 放到
; scripts\Installer_Languages\ 下再照上游登记，例如：
;   Name: "chinesesimplified"; MessagesFile: "Installer_Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; 开机自启：上游安装器同样没有此勾选项 —— 自启由应用内设置控制（写
; HKCU\Software\Microsoft\Windows\CurrentVersion\Run，见 src/core/utils/AutoStartup.cpp）
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; dist/ 即运行时根（exe 与 src/qml、assets、RinUI、themes、configs 同级，
; 对应上游 ROOT_PATH 契约；产物由 cmake --install + windeployqt 生成）
Source: "..\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

; ---- 卸载与用户数据保留策略 ----
; 用户数据不在 %APPDATA%，而是随便携式布局放在安装目录内（见 src/core/AppPaths.cpp：
; configsRoot = 运行时根/configs，logsRoot = 运行时根/logs；安装后即 {app}\configs、{app}\logs）。
; Inno Setup 卸载器只删除安装时登记的文件，运行期生成的 configs.json、日志等不会出现在
; 卸载日志中，因此这里刻意不提供 [UninstallDelete] 段 —— 卸载后 {app} 下仅剩用户数据，
; 保留给用户自行处理（或重装时复用），满足“卸载保留用户数据”的要求。
; 注意：默认安装到 {autopf} 时 configs/ 写入需要管理员权限（上游同为便携式数据布局，
; 行为一致）；标准权限安装可用命令行 /currentuser。
