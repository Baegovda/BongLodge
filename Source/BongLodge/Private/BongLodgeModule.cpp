#include "BongLodgeModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#define BONGLODGE_ROOT_KEY TEXT("Root")

static FString BongLodgeFilePath()
{
	// %LOCALAPPDATA%\UnrealEngine\5.7\Saved\BongLodge.json (project independent)
	return FPaths::Combine(FPaths::EngineUserDir(), TEXT("Saved"), TEXT("BongLodge.json"));
}

void FBongLodgeModule::StartupModule()
{
	if (!GIsEditor || IsRunningCommandlet() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	Load();

	FSlateApplication& App = FSlateApplication::Get();
	PreTickHandle = App.OnPreTick().AddRaw(this, &FBongLodgeModule::OnPreTick);
	DestroyHandle = App.OnWindowBeingDestroyed().AddRaw(this, &FBongLodgeModule::OnWindowBeingDestroyed);

	IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	MainFrameHandle = MainFrame.OnMainFrameCreationFinished().AddRaw(this, &FBongLodgeModule::OnMainFrameCreated);
}

void FBongLodgeModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnPreTick().Remove(PreTickHandle);
		FSlateApplication::Get().OnWindowBeingDestroyed().Remove(DestroyHandle);
	}
	if (IMainFrameModule* MainFrame = FModuleManager::GetModulePtr<IMainFrameModule>(TEXT("MainFrame")))
	{
		MainFrame->OnMainFrameCreationFinished().Remove(MainFrameHandle);
	}
	if (bDirty)
	{
		Save();
	}
}

// ---------------------------------------------------------------- identity

static void BongLodgeCollectTabs(const TSharedRef<SWidget>& Widget, TArray<FString>& OutIds, int32 Depth)
{
	if (Depth > 64)
	{
		return;
	}
	if (Widget->GetType() == FName(TEXT("SDockTab")))
	{
		const TSharedRef<SDockTab> Tab = StaticCastSharedRef<SDockTab>(Widget);
		OutIds.AddUnique(Tab->GetLayoutIdentifier().ToString());
		return; // tab content is not inside the tab well widget, no need to descend
	}
	FChildren* Children = Widget->GetChildren();
	if (!Children)
	{
		return;
	}
	for (int32 i = 0; i < Children->Num(); ++i)
	{
		BongLodgeCollectTabs(Children->GetChildAt(i), OutIds, Depth + 1);
	}
}

FString FBongLodgeModule::GetWindowKey(const TSharedRef<SWindow>& Window) const
{
	if (IMainFrameModule* MainFrame = FModuleManager::GetModulePtr<IMainFrameModule>(TEXT("MainFrame")))
	{
		if (MainFrame->GetParentWindow() == Window)
		{
			return BONGLODGE_ROOT_KEY;
		}
	}
	if (!Window->IsRegularWindow() || Window->IsAutosized() || Window->GetType() != EWindowType::Normal)
	{
		return FString(); // menus, tooltips, notifications, dialogs sized to content: ignore
	}
	TArray<FString> Ids;
	BongLodgeCollectTabs(Window, Ids, 0);
	if (Ids.Num() == 0)
	{
		return FString(); // not a dock-tab window (modal dialogs etc.)
	}
	Ids.Sort();
	return TEXT("Tabs:") + FString::Join(Ids, TEXT("|"));
}

// ---------------------------------------------------------------- capture / apply

bool FBongLodgeModule::Capture(const TSharedRef<SWindow>& Window, FBongLodgePlacement& Out) const
{
	TSharedPtr<FGenericWindow> Native = Window->GetNativeWindow();
	if (!Native.IsValid())
	{
		return false;
	}
	Out.bMaximized = Window->IsWindowMaximized();
	Out.bMinimized = Window->IsWindowMinimized();

	int32 X = 0, Y = 0, W = 0, H = 0;
	if (Native->GetRestoredDimensions(X, Y, W, H) && W > 0 && H > 0)
	{
		Out.X = X; Out.Y = Y; Out.W = W; Out.H = H;
	}
	else
	{
		const FVector2D Pos = Window->GetPositionInScreen();
		const FVector2D Size = Window->GetSizeInScreen();
		Out.X = FMath::RoundToInt(Pos.X); Out.Y = FMath::RoundToInt(Pos.Y);
		Out.W = FMath::RoundToInt(Size.X); Out.H = FMath::RoundToInt(Size.Y);
	}
	if (Out.W < 50 || Out.H < 50)
	{
		return false;
	}
	Out.Dpi = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(Out.X + Out.W * 0.5f, Out.Y + Out.H * 0.5f);
	return true;
}

static bool BongLodgeIntersects(const FPlatformRect& R, int32 X, int32 Y, int32 W, int32 H, int32 Min)
{
	const int32 L = FMath::Max(R.Left, X), T = FMath::Max(R.Top, Y);
	const int32 Ri = FMath::Min(R.Right, X + W), B = FMath::Min(R.Bottom, Y + H);
	return (Ri - L) >= Min && (B - T) >= Min;
}

void FBongLodgeModule::FitToMonitors(FBongLodgePlacement& P)
{
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);
	if (Metrics.MonitorInfo.Num() == 0)
	{
		return;
	}

	// Pick monitor containing the window centre; else the one with largest overlap; else primary.
	const int32 Cx = P.X + P.W / 2, Cy = P.Y + P.H / 2;
	const FMonitorInfo* Target = nullptr;
	for (const FMonitorInfo& M : Metrics.MonitorInfo)
	{
		if (Cx >= M.WorkArea.Left && Cx < M.WorkArea.Right && Cy >= M.WorkArea.Top && Cy < M.WorkArea.Bottom) { Target = &M; break; }
	}
	if (!Target)
	{
		for (const FMonitorInfo& M : Metrics.MonitorInfo)
		{
			if (BongLodgeIntersects(M.WorkArea, P.X, P.Y, P.W, P.H, 100)) { Target = &M; break; }
		}
	}
	bool bFallback = false;
	if (!Target)
	{
		bFallback = true;
		for (const FMonitorInfo& M : Metrics.MonitorInfo)
		{
			if (M.bIsPrimary) { Target = &M; break; }
		}
		if (!Target) { Target = &Metrics.MonitorInfo[0]; }
	}

	const FPlatformRect& WA = Target->WorkArea;
	// DPI change on the target monitor (e.g. 125% -> 100%): keep logical size.
	const float NewDpi = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint((WA.Left + WA.Right) * 0.5f, (WA.Top + WA.Bottom) * 0.5f);
	if (P.Dpi > 0.f && NewDpi > 0.f && !FMath::IsNearlyEqual(P.Dpi, NewDpi, 0.01f))
	{
		P.W = FMath::RoundToInt(P.W * NewDpi / P.Dpi);
		P.H = FMath::RoundToInt(P.H * NewDpi / P.Dpi);
		P.Dpi = NewDpi;
	}

	const int32 WAW = WA.Right - WA.Left, WAH = WA.Bottom - WA.Top;
	P.W = FMath::Clamp(P.W, 200, WAW);
	P.H = FMath::Clamp(P.H, 150, WAH);
	if (bFallback)
	{
		P.X = WA.Left + (WAW - P.W) / 2;
		P.Y = WA.Top + (WAH - P.H) / 2;
	}
	// Nudge fully into the work area (title bar always reachable).
	P.X = FMath::Clamp(P.X, WA.Left, WA.Right - P.W);
	P.Y = FMath::Clamp(P.Y, WA.Top, WA.Bottom - P.H);
}

void FBongLodgeModule::Apply(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& InP)
{
	FBongLodgePlacement P = InP;
	FitToMonitors(P);

	if (Window->IsWindowMaximized() || Window->IsWindowMinimized())
	{
		Window->Restore();
	}
	// Physical virtual-desktop pixels, same space as SWindow::GetPositionInScreen.
	Window->ReshapeWindow(FVector2D(P.X, P.Y), FVector2D(P.W, P.H));
	if (P.bMaximized)
	{
		Window->Maximize(); // maximizes on the monitor the restored rect is on
	}
	else if (P.bMinimized)
	{
		Window->Minimize();
	}
}

// ---------------------------------------------------------------- events

void FBongLodgeModule::OnMainFrameCreated(TSharedPtr<SWindow> Root, bool bNewProjectWindow)
{
	if (!Root.IsValid() || bNewProjectWindow)
	{
		return;
	}
	if (const FBongLodgePlacement* P = Placements.Find(BONGLODGE_ROOT_KEY))
	{
		Apply(Root.ToSharedRef(), *P);
	}
	Handled.Add(Root);
	KeyCache.Add(Root.Get(), BONGLODGE_ROOT_KEY);
}

void FBongLodgeModule::OnPreTick(float Dt)
{
	FSlateApplication& App = FSlateApplication::Get();
	TArray<TSharedRef<SWindow>> Windows = App.GetTopLevelWindows();
	// floating tab windows are children of the root window
	for (int32 i = 0; i < Windows.Num(); ++i)
	{
		for (const TSharedRef<SWindow>& Child : Windows[i]->GetChildWindows())
		{
			Windows.AddUnique(Child);
		}
	}

	const double Now = FPlatformTime::Seconds();
	const bool bCaptureNow = (Now - LastSaveCheck) > 0.5;

	for (const TSharedRef<SWindow>& Window : Windows)
	{
		const bool bNew = !Handled.Contains(Window);
		if (bNew)
		{
			const FString Key = GetWindowKey(Window);
			if (Key.IsEmpty())
			{
				continue; // retry next tick (tabs may not be populated yet); cheap
			}
			Handled.Add(Window);
			KeyCache.Add(&Window.Get(), Key);
			if (const FBongLodgePlacement* P = Placements.Find(Key))
			{
				Apply(Window, *P); // first tick after creation (see README: 1-frame for floating)
			}
			continue;
		}
		if (bCaptureNow)
		{
			const FString* Key = KeyCache.Find(&Window.Get());
			FBongLodgePlacement Cur;
			if (Key && Capture(Window, Cur))
			{
				FBongLodgePlacement& Old = Placements.FindOrAdd(*Key);
				if (Old.X != Cur.X || Old.Y != Cur.Y || Old.W != Cur.W || Old.H != Cur.H || Old.bMaximized != Cur.bMaximized || Old.bMinimized != Cur.bMinimized)
				{
					Old = Cur;
					bDirty = true;
				}
			}
		}
	}
	if (bCaptureNow)
	{
		LastSaveCheck = Now;
		if (bDirty) { Save(); }
	}
}

void FBongLodgeModule::OnWindowBeingDestroyed(const SWindow& Window)
{
	if (const FString* Key = KeyCache.Find(&Window))
	{
		FBongLodgePlacement Cur;
		TSharedRef<SWindow> Ref = ConstCastSharedRef<SWindow>(StaticCastSharedRef<const SWindow>(Window.AsShared()));
		if (Capture(Ref, Cur))
		{
			Placements.Add(*Key, Cur);
			Save();
		}
		KeyCache.Remove(&Window);
	}
	for (auto It = Handled.CreateIterator(); It; ++It)
	{
		if (!It->IsValid() || It->Pin().Get() == &Window) { It.RemoveCurrent(); }
	}
}

// ---------------------------------------------------------------- persistence

void FBongLodgeModule::Load()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *BongLodgeFilePath()))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return;
	}
	for (const auto& Pair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Obj)) { continue; }
		FBongLodgePlacement P;
		P.X = (int32)(*Obj)->GetNumberField(TEXT("x"));
		P.Y = (int32)(*Obj)->GetNumberField(TEXT("y"));
		P.W = (int32)(*Obj)->GetNumberField(TEXT("w"));
		P.H = (int32)(*Obj)->GetNumberField(TEXT("h"));
		P.Dpi = (float)(*Obj)->GetNumberField(TEXT("dpi"));
		P.bMaximized = (*Obj)->GetBoolField(TEXT("maximized"));
		P.bMinimized = (*Obj)->GetBoolField(TEXT("minimized"));
		Placements.Add(Pair.Key, P);
	}
}

void FBongLodgeModule::Save()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	for (const auto& Pair : Placements)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), Pair.Value.X);
		O->SetNumberField(TEXT("y"), Pair.Value.Y);
		O->SetNumberField(TEXT("w"), Pair.Value.W);
		O->SetNumberField(TEXT("h"), Pair.Value.H);
		O->SetNumberField(TEXT("dpi"), Pair.Value.Dpi);
		O->SetBoolField(TEXT("maximized"), Pair.Value.bMaximized);
		O->SetBoolField(TEXT("minimized"), Pair.Value.bMinimized);
		Root->SetObjectField(Pair.Key, O);
	}
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *BongLodgeFilePath());
	bDirty = false;
}

IMPLEMENT_MODULE(FBongLodgeModule, BongLodge)
