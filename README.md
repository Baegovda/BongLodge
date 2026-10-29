# BongLodge (UE 5.7.4 에디터 전용 플러그인)

메인 에디터 창과 떼어낸(플로팅) 탭 창의 위치·크기·최대화/최소화 상태를 마지막으로 닫았을 때 그대로 다시 열어줍니다. 마우스 위치와 상관없이 동작하고, 모니터를 껐다 켜도 동작합니다.
저장 파일: `%LOCALAPPDATA%\UnrealEngine\5.7\Saved\BongLodge.json` (프로젝트와 상관없이 공용)

## 빌드 (Visual Studio 2022 + "C++를 사용한 게임 개발" + MSVC v143 + Windows SDK 필요)
```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin ^
  -Plugin="C:\Temp\BongLodge\BongLodge.uplugin" ^
  -Package="C:\Temp\BongLodge_Built" -TargetPlatforms=Win64 -Rocket
```
## 설치
`C:\Temp\BongLodge_Built` 폴더 안의 내용을 통째로
`C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Marketplace\BongLodge` 로 복사합니다 (관리자 권한 필요). 기본으로 켜져 있습니다.

## 동작 방식과 한계
- 메인 창: 에디터가 창을 만들기 전에(모듈 시작 시점) 저장된 위치를 에디터 설정 `[RootWindow] ScreenPosition / WindowSize / InitiallyMaximized`에 미리 넣어 둡니다. 그래서 메인 창은 처음부터 저장된 자리에 나타납니다.
- 시작 직후 에디터(또는 Windows)가 창을 다른 모니터로 한 번 옮기려는 동작이 있습니다. BongLodge는 시작 후 약 8초 동안 이 이동을 `WM_WINDOWPOSCHANGING` 단계에서 막습니다. 화면에 그려지기 전에 막기 때문에 튀는 게 보이지 않습니다. 사용자가 창을 직접 끌기 시작하면 바로 막는 걸 멈춥니다.
- 플로팅 탭 창(Content Browser 등): 에디터 레이아웃이 자기 위치를 복원하고, BongLodge는 메인 창이 만들어지는 시점에 바로 확인합니다. 이미 제자리면 건드리지 않습니다. 저장된 레이아웃 위치가 꺼진 모니터에 있으면, 레이아웃을 불러오기 전에 메모리상에서 주 모니터로 옮겨 둡니다(파일은 직접 고치지 않습니다).
- 최소화 상태 복원: 창이 한 번 보였다가(약 0.15초) 최소화됩니다. 에디터가 최소화 상태로 창을 시작하는 기능이 없어서 이 부분은 깜빡임이 남습니다.
- 125% / 100% 배율이 다른 모니터 사이에서도 논리 크기를 유지합니다.
- 초기화: `BongLodge.json` 파일을 지우면 됩니다.

---

- 최대화 아님(일반) 상태 창은 화면에 보이는 실제 사각형을 저장합니다(Aero 스냅된 반쪽 크기 포함). 다음 실행 때 최대화로 돌아가지 않습니다.
- 시작 전에 레이아웃(EditorLayout.ini)의 플로팅 창 위치를 저장값으로 바꾸고, 시작 직후 엔진/Windows가 예전 위치로 옮기려는 시도는 차단합니다.
- 에디터를 여러 개 띄워도 저장 시 디스크 내용과 병합하므로 다른 창 기록을 덮어쓰지 않습니다.

## English

BongLodge is an editor-only plugin for UE 5.7. It restores the position, size and maximized/minimized state of the main editor window and of floating tab windows. It doesn't care where the mouse cursor is, it handles mixed-DPI monitors, and if a saved monitor is disconnected it moves the window to the primary monitor.

Build: `RunUAT.bat BuildPlugin -Plugin=<path>\BongLodge.uplugin -Package=<out> -TargetPlatforms=Win64 -Rocket` (needs VS 2022 + MSVC v143). Install the packaged output to `Engine\Plugins\Marketplace\BongLodge`. Saved placements: `%LOCALAPPDATA%\UnrealEngine\5.7\Saved\BongLodge.json`.

How it avoids flicker: before the main frame is built, the saved root placement is written into the editor's `[RootWindow]` config, so the window is created at the right spot. During the first ~8 s, a Windows message handler (`IWindowsMessageHandler`, `WM_WINDOWPOSCHANGING`) blocks foreign moves of placed windows to another monitor before anything is drawn. Off-screen floating layout areas are pulled onto the primary monitor in memory before the layout loads.

Known limitations: restoring a minimized window still shows it for about 0.15 s first. A floating window that is already maximized may briefly flip its state at the same rectangle while starting up, with no visible move.
- Normal (non-maximized) windows save the real on-screen rect (including Aero-snapped halves), so they no longer come back maximized.
- Before layout load, floating areas in EditorLayout.ini are rewritten to the saved rect; stale startup moves by the engine/Windows are blocked.
- Saves merge with the file on disk, so multiple editor instances don't clobber each other.
