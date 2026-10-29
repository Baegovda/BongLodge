#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class SWindow;
class FGenericWindow;

struct FBongLodgePlacement
{
	int32 X = 0, Y = 0, W = 0, H = 0;   // restored (non-maximized) rect, physical virtual-desktop pixels
	float Dpi = 1.f;                     // DPI scale of the monitor at save time
	bool bMaximized = false;
	bool bMinimized = false;
};

class FBongLodgeMessageGuard;

class FBongLodgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnPreTick(float Dt);
	void OnWindowBeingDestroyed(const SWindow& Window);
	void OnMainFrameCreated(TSharedPtr<SWindow> Root, bool bNewProjectWindow);
	void OnSystemDPIChanged(TSharedRef<SWindow> Window);

	/** Before the main frame / layout is created: feed our saved root placement into [RootWindow]
	 *  and pull off-screen floating areas of the saved layouts back onto a connected monitor. */
	void PrepareConfigBeforeLayoutLoad();

	// Startup: WM_DPICHANGED is deferred by FWindowsApplication and then applied with a stale
	// suggested rect, which throws freshly placed windows onto another monitor. During startup we
	// mark the native window as "manual DPI" for that one message and fix the DPI scale ourselves.
	double StartupGraceUntil = 0.0;
	TArray<TWeakPtr<SWindow>> PendingDpiFix;
	FDelegateHandle DpiHandle;

	/** Windows-only: blocks foreign cross-monitor moves of freshly placed windows during startup
	 *  at WM_WINDOWPOSCHANGING time (before anything is drawn at the wrong place). */
	TUniquePtr<FBongLodgeMessageGuard> MessageGuard;
	void GuardWindow(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& Fitted);
	void HandleNewWindow(const TSharedRef<SWindow>& Window);

	FString GetWindowKey(const TSharedRef<SWindow>& Window) const;
	bool Capture(const TSharedRef<SWindow>& Window, FBongLodgePlacement& Out) const;
	void Apply(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& P, bool bStartEnforce = true);
	static void ApplyFitted(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& P);
	static void FitToMonitors(FBongLodgePlacement& P);

	void Load();
	void Save();

	TMap<FString, FBongLodgePlacement> Placements;
	TSet<TWeakPtr<SWindow>> Handled;   // windows already restored / being tracked
	TMap<const SWindow*, FString> KeyCache;

	// After we place a window, the editor may still move it once more during startup
	// (observed in 5.7: windows shifted to another monitor ~0.2-0.6 s after first show).
	// For a short grace period we re-apply the target and do not record those moves.
	struct FEnforce { TWeakPtr<SWindow> Window; FBongLodgePlacement Target; double Until = 0.0; double StartTime = 0.0; int32 Corrections = 0; };
	TArray<FEnforce> Enforced;
	void Enforce(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& Fitted);
	bool IsEnforced(const SWindow* Window) const;
	FDelegateHandle PreTickHandle, DestroyHandle, MainFrameHandle;
	double LastSaveCheck = 0.0;
	bool bDirty = false;
};
