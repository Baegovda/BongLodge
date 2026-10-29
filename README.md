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
- 메인 창: 메인 프레임이 만들어지는 시점(`OnMainFrameCreationFinished`)에 위치를 맞춥니다.
- 플로팅 탭 창: 창이 생성된 다음 첫 틱에서 위치를 맞춥니다. 엔진 바이너리 빌드에는 "창이 보이기 전에" 끼어들 수 있는 공개 훅이 없어서 아주 짧게(한 프레임 정도) 다른 곳에 보였다가 이동할 수 있습니다.
- 저장된 위치의 모니터가 꺼져 있으면 주 모니터 가운데로 옮기고, 크기는 작업 영역에 맞춰 줄입니다. 화면 밖으로 일부 나가 있으면 안쪽으로 밀어 넣습니다.
- 125% / 100% 배율이 다른 모니터 사이에서도 논리 크기를 유지합니다.
- 초기화: 위 json 파일을 지우면 됩니다.

---

## English

BongLodge is an editor-only plugin for UE 5.7. It restores the position, size and maximized/minimized state of the main editor window and of floating tab windows. It doesn't care where the mouse cursor is, it handles mixed-DPI monitors, and if a saved monitor is disconnected it moves the window to the primary monitor.

Build: `RunUAT.bat BuildPlugin -Plugin=<path>\BongLodge.uplugin -Package=<out> -TargetPlatforms=Win64 -Rocket` (needs VS 2022 + MSVC v143). Install the packaged output to `Engine\Plugins\Marketplace\BongLodge`. Saved placements: `%LOCALAPPDATA%\UnrealEngine\5.7\Saved\BongLodge.json`.

Known limitation: floating tab windows are moved on the first tick after creation, so they may flash briefly. Slate has no public pre-show hook in binary engine builds.
