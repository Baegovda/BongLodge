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

class FBongLodgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnPreTick(float Dt);
	void OnWindowBeingDestroyed(const SWindow& Window);
	void OnMainFrameCreated(TSharedPtr<SWindow> Root, bool bNewProjectWindow);

	FString GetWindowKey(const TSharedRef<SWindow>& Window) const;
	bool Capture(const TSharedRef<SWindow>& Window, FBongLodgePlacement& Out) const;
	void Apply(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& P);
	static void FitToMonitors(FBongLodgePlacement& P);

	void Load();
	void Save();

	TMap<FString, FBongLodgePlacement> Placements;
	TSet<TWeakPtr<SWindow>> Handled;   // windows already restored / being tracked
	TMap<const SWindow*, FString> KeyCache;
	FDelegateHandle PreTickHandle, DestroyHandle, MainFrameHandle;
	double LastSaveCheck = 0.0;
	bool bDirty = false;
};
