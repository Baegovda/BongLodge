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
#include "Misc/ConfigCacheIni.h"
#include "Internationalization/Regex.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsApplication.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogBongLodgeGuard, Log, All);

#if PLATFORM_WINDOWS
class FBongLodgeMessageGuard : public IWindowsMessageHandler
{
public:
	struct FTarget { RECT Rect; bool bMaximized; double Until; };
	TMap<HWND, FTarget> Targets;
	static inline bool bOwnChange = false;

	virtual bool ProcessMessage(HWND Hwnd, uint32 Msg, WPARAM WParam, LPARAM LParam, int32& OutResult) override
	{
		if (Msg == WM_ENTERSIZEMOVE)
		{
			Targets.Remove(Hwnd); // user starts dragging: stop guarding this window
			return false;
		}
		if (Msg != WM_WINDOWPOSCHANGING || Targets.Num() == 0)
		{
			return false;
		}
		FTarget* T = Targets.Find(Hwnd);
		if (!T)
		{
			return false;
		}
		if (FPlatformTime::Seconds() > T->Until)
		{
			Targets.Remove(Hwnd);
			return false;
		}
		WINDOWPOS* Pos = reinterpret_cast<WINDOWPOS*>(LParam);
		if (!Pos || (Pos->flags & SWP_NOMOVE) || ::IsIconic(Hwnd) || Pos->x <= -30000)
		{
			return false;
		}
		RECT Cur;
		::GetWindowRect(Hwnd, &Cur);
		const int32 W = (Pos->flags & SWP_NOSIZE) ? (Cur.right - Cur.left) : Pos->cx;
		const int32 H = (Pos->flags & SWP_NOSIZE) ? (Cur.bottom - Cur.top) : Pos->cy;
		RECT New = { Pos->x, Pos->y, Pos->x + W, Pos->y + H };
		if (bOwnChange)
		{
			return false; // BongLodge itself is placing the window
		}
		const HMONITOR TargetMon = ::MonitorFromRect(&T->Rect, MONITOR_DEFAULTTONULL);
		const HMONITOR NewMon = ::MonitorFromRect(&New, MONITOR_DEFAULTTONULL);
		if (TargetMon == nullptr)
		{
			return false;
		}
		const auto Near = [](LONG A, LONG B) { return FMath::Abs((int32)A - (int32)B) <= 2; };
		if (T->bMaximized)
		{
			// allow only "maximized on the target monitor" (rect covers that monitor's work area)
			MONITORINFO MI; MI.cbSize = sizeof(MI);
			if (NewMon == TargetMon && ::GetMonitorInfo(TargetMon, &MI)
				&& New.left <= MI.rcWork.left + 12 && New.top <= MI.rcWork.top + 12 && New.right >= MI.rcWork.right - 12 && New.bottom >= MI.rcWork.bottom - 12)
			{
				return false;
			}
		}
		else if (NewMon == TargetMon && Near(New.left, T->Rect.left) && Near(New.top, T->Rect.top) && Near(New.right, T->Rect.right) && Near(New.bottom, T->Rect.bottom))
		{
			return false; // (almost) exactly the target rect
		}
		UE_LOG(LogBongLodgeGuard, Log, TEXT("Blocked startup move of window to (%d,%d %dx%d)"), Pos->x, Pos->y, W, H);
		Pos->flags |= SWP_NOMOVE | SWP_NOSIZE;
		return false; // let default processing continue with the neutralised WINDOWPOS
	}
};
#else
class FBongLodgeMessageGuard {};
#endif
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogBongLodge, Log, All);

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
	StartupGraceUntil = TNumericLimits<double>::Max(); // until the main frame exists (+ a few seconds)
	PrepareConfigBeforeLayoutLoad();
	UE_LOG(LogBongLodge, Log, TEXT("BongLodge started, %d saved placements from %s"), Placements.Num(), *BongLodgeFilePath());

	FSlateApplication& App = FSlateApplication::Get();
	PreTickHandle = App.OnPreTick().AddRaw(this, &FBongLodgeModule::OnPreTick);
#if PLATFORM_WINDOWS
	if (TSharedPtr<GenericApplication> PlatformApp = App.GetPlatformApplication())
	{
		MessageGuard = MakeUnique<FBongLodgeMessageGuard>();
		static_cast<FWindowsApplication*>(PlatformApp.Get())->AddMessageHandler(*MessageGuard);
	}
#endif
	DpiHandle = App.OnSystemSignalsDPIChanged().AddRaw(this, &FBongLodgeModule::OnSystemDPIChanged);
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
		FSlateApplication::Get().OnSystemSignalsDPIChanged().Remove(DpiHandle);
#if PLATFORM_WINDOWS
		if (MessageGuard.IsValid())
		{
			if (TSharedPtr<GenericApplication> PlatformApp = FSlateApplication::Get().GetPlatformApplication())
			{
				static_cast<FWindowsApplication*>(PlatformApp.Get())->RemoveMessageHandler(*MessageGuard);
			}
		}
#endif
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

	// Normal state: use the real current rect. (GetRestoredDimensions returns rcNormalPosition, which for
	// an Aero-snapped window, e.g. snapped to the right half, is the PRE-snap rect, not what is on screen.)
	// Maximized/minimized: use the restored (normal) rect so un-maximizing later gives the right size.
	int32 X = 0, Y = 0, W = 0, H = 0;
	if ((Out.bMaximized || Out.bMinimized) && Native->GetRestoredDimensions(X, Y, W, H) && W > 0 && H > 0)
	{
		Out.X = X; Out.Y = Y; Out.W = W; Out.H = H;
	}
	else
	{
		const FVector2D Pos = Window->GetPositionInScreen();
		const FVector2D Size = Window->GetSizeInScreen();
		Out.X = FMath::RoundToInt(Pos.X); Out.Y = FMath::RoundToInt(Pos.Y);
		Out.W = FMath::RoundToInt(Size.X); Out.H = FMath::RoundToInt(Size.Y);
#if PLATFORM_WINDOWS
		// Slate's cached position can be stale/mis-scaled after a move done outside Slate
		// (observed: window at x=-1920 reported as -960). Ask Windows for the real rect.
		RECT R;
		// Query in per-monitor-v2 context so the rect is in true physical virtual-screen coordinates.
		const DPI_AWARENESS_CONTEXT PrevCtx = ::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		const BOOL bGot = ::GetWindowRect((HWND)Native->GetOSWindowHandle(), &R);
		if (PrevCtx) { ::SetThreadDpiAwarenessContext(PrevCtx); }
		UE_LOG(LogBongLodge, Verbose, TEXT("Capture slate=(%d,%d) win32=(%d,%d)"), Out.X, Out.Y, (int32)R.left, (int32)R.top);
		if (bGot && R.right > R.left)
		{
			Out.X = R.left; Out.Y = R.top; Out.W = R.right - R.left; Out.H = R.bottom - R.top;
		}
#endif
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
	for (const FMonitorInfo& M : Metrics.MonitorInfo)
	{
		UE_LOG(LogBongLodge, Log, TEXT("Monitor %s primary=%d WA=(%d,%d)-(%d,%d)"), *M.Name, M.bIsPrimary, M.WorkArea.Left, M.WorkArea.Top, M.WorkArea.Right, M.WorkArea.Bottom);
	}
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

static int32 BongLodgeMonitorIndex(int32 X, int32 Y, int32 W, int32 H)
{
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);
	const int32 Cx = X + W / 2, Cy = Y + H / 2;
	for (int32 i = 0; i < Metrics.MonitorInfo.Num(); ++i)
	{
		const FPlatformRect& R = Metrics.MonitorInfo[i].DisplayRect;
		if (Cx >= R.Left && Cx < R.Right && Cy >= R.Top && Cy < R.Bottom) { return i; }
	}
	return INDEX_NONE;
}

void FBongLodgeModule::ApplyFitted(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& P)
{
	if (Window->IsWindowMaximized() || Window->IsWindowMinimized())
	{
		Window->Restore();
	}
	// Physical virtual-desktop pixels, same space as SWindow::GetPositionInScreen.
#if PLATFORM_WINDOWS
	TGuardValue<bool> OwnChange(FBongLodgeMessageGuard::bOwnChange, true);
#endif
	Window->ReshapeWindow(FVector2D(P.X, P.Y), FVector2D(P.W, P.H));
	// Moving across monitors with different DPI makes Windows rescale the window (WM_DPICHANGED),
	// so set the exact rect once more now that it is on the target monitor.
	const FVector2D NewSize = Window->GetSizeInScreen();
	if (FMath::Abs(NewSize.X - P.W) > 2 || FMath::Abs(NewSize.Y - P.H) > 2)
	{
		Window->ReshapeWindow(FVector2D(P.X, P.Y), FVector2D(P.W, P.H));
	}
	if (P.bMaximized)
	{
		Window->Maximize(); // maximizes on the monitor the restored rect is on
	}
	else if (P.bMinimized)
	{
		Window->Minimize();
	}
}

void FBongLodgeModule::Apply(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& InP, bool bStartEnforce)
{
	FBongLodgePlacement P = InP;
	FitToMonitors(P);
	UE_LOG(LogBongLodge, Log, TEXT("Apply: saved (%d,%d %dx%d max=%d min=%d) -> fitted (%d,%d %dx%d)"), InP.X, InP.Y, InP.W, InP.H, InP.bMaximized, InP.bMinimized, P.X, P.Y, P.W, P.H);
	FBongLodgePlacement Cur;
	bool bAlreadyThere = false;
	if (Capture(Window, Cur) && Cur.bMaximized == P.bMaximized && Cur.bMinimized == P.bMinimized)
	{
		if (P.bMaximized || P.bMinimized)
		{
			// already maximized/minimized: fine if it is on the same monitor as the target
			const FVector2D WPos = Window->GetPositionInScreen();
			const FVector2D WSize = Window->GetSizeInScreen();
			bAlreadyThere = BongLodgeMonitorIndex((int32)WPos.X, (int32)WPos.Y, (int32)WSize.X, (int32)WSize.Y) == BongLodgeMonitorIndex(P.X, P.Y, P.W, P.H);
		}
		else
		{
			bAlreadyThere = FMath::Abs(Cur.X - P.X) <= 2 && FMath::Abs(Cur.Y - P.Y) <= 2 && FMath::Abs(Cur.W - P.W) <= 1 && FMath::Abs(Cur.H - P.H) <= 1;
		}
	}
	if (bAlreadyThere)
	{
		UE_LOG(LogBongLodge, Log, TEXT("Already at target, no reshape needed"));
	}
	else
	{
		ApplyFitted(Window, P);
	}
	if (bStartEnforce)
	{
		Enforce(Window, P);
	}
}

void FBongLodgeModule::Enforce(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& Fitted)
{
	GuardWindow(Window, Fitted);
	FEnforce E;
	E.Window = Window;
	E.Target = Fitted;
	E.Until = FPlatformTime::Seconds() + 6.0;
	E.StartTime = FPlatformTime::Seconds();
	Enforced.Add(E);
}

void FBongLodgeModule::GuardWindow(const TSharedRef<SWindow>& Window, const FBongLodgePlacement& P)
{
#if PLATFORM_WINDOWS
	TSharedPtr<FGenericWindow> Native = Window->GetNativeWindow();
	if (!MessageGuard.IsValid() || !Native.IsValid())
	{
		return;
	}
	FBongLodgeMessageGuard::FTarget T;
	T.Rect = { P.X, P.Y, P.X + P.W, P.Y + P.H };
	T.bMaximized = P.bMaximized;
	T.Until = FPlatformTime::Seconds() + 8.0;
	MessageGuard->Targets.Add((HWND)Native->GetOSWindowHandle(), T);
#endif
}

bool FBongLodgeModule::IsEnforced(const SWindow* Window) const
{
	for (const FEnforce& E : Enforced)
	{
		if (E.Window.Pin().Get() == Window) { return true; }
	}
	return false;
}

// ---------------------------------------------------------------- events

void FBongLodgeModule::OnMainFrameCreated(TSharedPtr<SWindow> Root, bool bNewProjectWindow)
{
	StartupGraceUntil = FPlatformTime::Seconds() + 8.0;
	if (!Root.IsValid() || bNewProjectWindow)
	{
		return;
	}
	UE_LOG(LogBongLodge, Log, TEXT("MainFrame created, root at (%.0f,%.0f) visible=%d"), Root->GetPositionInScreen().X, Root->GetPositionInScreen().Y, Root->IsVisible());
	if (const FBongLodgePlacement* P = Placements.Find(BONGLODGE_ROOT_KEY))
	{
		Apply(Root.ToSharedRef(), *P);
	}
	Handled.Add(Root);
	KeyCache.Add(Root.Get(), BONGLODGE_ROOT_KEY);

	// Floating tab windows restored from the layout already exist now: place + guard them
	// before the first tick, instead of waiting for OnPreTick.
	for (const TSharedRef<SWindow>& Child : Root->GetChildWindows())
	{
		if (!Handled.Contains(Child))
		{
			HandleNewWindow(Child);
		}
	}
}

// Windows sometimes reports the maximized rect as the "restored" rect (window created maximized and
// never restored). Keep the previously known normal rect in that case.
static void BongLodgeKeepNormalRect(const TSharedRef<SWindow>& Window, FBongLodgePlacement& Cur, const FBongLodgePlacement* Old)
{
	if (!Cur.bMaximized || !Old)
	{
		return;
	}
	const FVector2D Size = Window->GetSizeInScreen();
	const bool bLooksLikeMaxRect = Cur.W >= Size.X - 24 && Cur.H >= Size.Y - 24;
	// the restored rect of a window that stayed maximized drifts by a few border pixels per launch
	const bool bJustBorderDrift = FMath::Abs(Cur.X - Old->X) <= 24 && FMath::Abs(Cur.Y - Old->Y) <= 24
		&& FMath::Abs(Cur.W - Old->W) <= 24 && FMath::Abs(Cur.H - Old->H) <= 24;
	if (bLooksLikeMaxRect || bJustBorderDrift)
	{
		Cur.X = Old->X; Cur.Y = Old->Y; Cur.W = Old->W; Cur.H = Old->H; Cur.Dpi = Old->Dpi;
	}
}

void FBongLodgeModule::HandleNewWindow(const TSharedRef<SWindow>& Window)
{
	const FString Key = GetWindowKey(Window);
	if (Key.IsEmpty())
	{
		return; // retry next tick (tabs may not be populated yet); cheap
	}
	Handled.Add(Window);
	KeyCache.Add(&Window.Get(), Key);
	if (const FBongLodgePlacement* P = Placements.Find(Key))
	{
		Apply(Window, *P);
	}
}

void FBongLodgeModule::OnPreTick(float Dt)
{
	for (const TWeakPtr<SWindow>& Weak : PendingDpiFix)
	{
		TSharedPtr<SWindow> W = Weak.Pin();
		TSharedPtr<FGenericWindow> Native = W.IsValid() ? W->GetNativeWindow() : nullptr;
		if (!Native.IsValid()) { continue; }
		Native->SetManualManageDPIChanges(false);
		const FVector2D Pos = W->GetPositionInScreen();
		const FVector2D Size = W->GetSizeInScreen();
		const float Actual = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(Pos.X + Size.X * 0.5f, Pos.Y + Size.Y * 0.5f);
		if (!FMath::IsNearlyEqual(Actual, Native->GetDPIScaleFactor(), 0.01f))
		{
			UE_LOG(LogBongLodge, Log, TEXT("DPI fix-up: %.2f -> %.2f"), Native->GetDPIScaleFactor(), Actual);
			Native->SetDPIScaleFactor(Actual);
			FSlateApplication::Get().HandleDPIScaleChanged(Native.ToSharedRef());
		}
	}
	PendingDpiFix.Reset();

	const double NowE = FPlatformTime::Seconds();
	for (int32 i = Enforced.Num() - 1; i >= 0; --i)
	{
		FEnforce& E = Enforced[i];
		TSharedPtr<SWindow> W = E.Window.Pin();
		if (!W.IsValid() || NowE > E.Until || E.Corrections >= 5)
		{
			Enforced.RemoveAtSwap(i);
			continue;
		}
		// The user touched the editor (mouse/keyboard): stop fighting, they are in control now.
		if (FSlateApplication::Get().GetLastUserInteractionTime() > E.StartTime)
		{
			Enforced.RemoveAtSwap(i);
			continue;
		}
		FBongLodgePlacement Cur;
		if (!Capture(W.ToSharedRef(), Cur)) { continue; }
		bool bOff;
		if (E.Target.bMaximized || E.Target.bMinimized)
		{
			// for maximized/minimized only the state and the monitor matter
			const FVector2D WPos = W->GetPositionInScreen();
			const FVector2D WSize = W->GetSizeInScreen();
			bOff = Cur.bMaximized != E.Target.bMaximized || Cur.bMinimized != E.Target.bMinimized
				|| (E.Target.bMaximized && BongLodgeMonitorIndex((int32)WPos.X, (int32)WPos.Y, (int32)WSize.X, (int32)WSize.Y) != BongLodgeMonitorIndex(E.Target.X, E.Target.Y, E.Target.W, E.Target.H));
		}
		else
		{
			bOff = Cur.bMaximized || Cur.bMinimized
				|| FMath::Abs(Cur.X - E.Target.X) > 8 || FMath::Abs(Cur.Y - E.Target.Y) > 8
				|| FMath::Abs(Cur.W - E.Target.W) > 8 || FMath::Abs(Cur.H - E.Target.H) > 8;
		}
		if (bOff)
		{
			UE_LOG(LogBongLodge, Log, TEXT("Window was moved by someone else to (%d,%d max=%d) during startup, re-applying (%d,%d)"), Cur.X, Cur.Y, Cur.bMaximized, E.Target.X, E.Target.Y);
			const FVector2D WPos = W->GetPositionInScreen();
			const FVector2D WSize = W->GetSizeInScreen();
			if (E.Target.bMaximized && !Cur.bMinimized
				&& BongLodgeMonitorIndex((int32)WPos.X, (int32)WPos.Y, (int32)WSize.X, (int32)WSize.Y) == BongLodgeMonitorIndex(E.Target.X, E.Target.Y, E.Target.W, E.Target.H))
			{
				// only the maximized flag got lost, the window is on the right monitor: just re-maximize
#if PLATFORM_WINDOWS
				TGuardValue<bool> OwnChange(FBongLodgeMessageGuard::bOwnChange, true);
#endif
				W->Maximize();
			}
			else
			{
				ApplyFitted(W.ToSharedRef(), E.Target);
			}
			++E.Corrections;
		}
	}

	if (FPlatformTime::Seconds() < StartupGraceUntil && UE_LOG_ACTIVE(LogBongLodge, Verbose))
	{
		for (const TSharedRef<SWindow>& TW : FSlateApplication::Get().GetTopLevelWindows())
		{
			if (TW->GetNativeWindow().IsValid() && TW->IsRegularWindow())
			{
				UE_LOG(LogBongLodge, Verbose, TEXT("trace f=%llu '%s' pos=(%.0f,%.0f)"), (unsigned long long)GFrameCounter, *TW->GetTitle().ToString(), TW->GetPositionInScreen().X, TW->GetPositionInScreen().Y);
			}
		}
	}

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
		if (!Handled.Contains(Window))
		{
			HandleNewWindow(Window);
			continue;
		}
		if (bCaptureNow)
		{
			if (IsEnforced(&Window.Get()))
			{
				continue; // don't record startup moves
			}
			const FString* Key = KeyCache.Find(&Window.Get());
			FBongLodgePlacement Cur;
			if (Key && Capture(Window, Cur))
			{
				BongLodgeKeepNormalRect(Window, Cur, Placements.Find(*Key));
				FBongLodgePlacement& Old = Placements.FindOrAdd(*Key);
				if (Old.X != Cur.X || Old.Y != Cur.Y)
				{
					UE_LOG(LogBongLodge, Log, TEXT("%s moved to (%d,%d %dx%d max=%d) slate=(%.0f,%.0f)"), **Key, Cur.X, Cur.Y, Cur.W, Cur.H, Cur.bMaximized, Window->GetPositionInScreen().X, Window->GetPositionInScreen().Y);
				}
				if (Old.X != Cur.X || Old.Y != Cur.Y || Old.W != Cur.W || Old.H != Cur.H || Old.bMaximized != Cur.bMaximized || Old.bMinimized != Cur.bMinimized)
				{
					Old = Cur;
					TouchedKeys.Add(*Key);
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
			BongLodgeKeepNormalRect(Ref, Cur, Placements.Find(*Key));
			Placements.Add(*Key, Cur);
			TouchedKeys.Add(*Key);
			Save();
		}
		KeyCache.Remove(&Window);
	}
	for (auto It = Handled.CreateIterator(); It; ++It)
	{
		if (!It->IsValid() || It->Pin().Get() == &Window) { It.RemoveCurrent(); }
	}
}

// ---------------------------------------------------------------- startup DPI message guard

void FBongLodgeModule::OnSystemDPIChanged(TSharedRef<SWindow> Window)
{
	if (FPlatformTime::Seconds() > StartupGraceUntil)
	{
		return;
	}
	TSharedPtr<FGenericWindow> Native = Window->GetNativeWindow();
	if (!Native.IsValid() || Native->IsManualManageDPIChanges())
	{
		return;
	}
	// FWindowsApplication checks this flag right after this delegate and then skips the
	// SetWindowPos(stale suggested rect). We restore the flag and the DPI scale next tick.
	Native->SetManualManageDPIChanges(true);
	PendingDpiFix.AddUnique(Window);
	UE_LOG(LogBongLodge, Log, TEXT("Suppressed startup WM_DPICHANGED reposition for '%s' at (%.0f,%.0f)"),
		*Window->GetTitle().ToString(), Window->GetPositionInScreen().X, Window->GetPositionInScreen().Y);
}

// ---------------------------------------------------------------- config pre-load

static bool BongLodgeRectVisible(const FDisplayMetrics& M, int32 X, int32 Y, int32 W, int32 H)
{
	for (const FMonitorInfo& Mon : M.MonitorInfo)
	{
		if (BongLodgeIntersects(Mon.WorkArea, X, Y, W, H, 100)) { return true; }
	}
	return false;
}

/** Floating areas in a saved layout: overwrite position/size/maximized with BongLodge's saved
 *  placement for the same set of opened tabs, so the engine creates the window exactly there
 *  (and its own SW_PARENTOPENING placement cache agrees with us). Values are DPI-independent. */
FString FBongLodgeModule::RewriteAreasFromSaved(const FString& Layout) const
{
	FString Text = Layout;
	int32 Search = 0;
	const FString AreaTag = TEXT("\"Type\": \"Area\"");
	while (true)
	{
		const int32 TagPos = Text.Find(AreaTag, ESearchCase::CaseSensitive, ESearchDir::FromStart, Search);
		if (TagPos == INDEX_NONE) { break; }
		const int32 Start = Text.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromEnd, TagPos);
		if (Start == INDEX_NONE) { Search = TagPos + 1; continue; }
		int32 Depth = 0, End = INDEX_NONE;
		for (int32 i = Start; i < Text.Len(); ++i)
		{
			if (Text[i] == TEXT('(')) { ++Depth; }
			else if (Text[i] == TEXT(')')) { if (--Depth == 0) { End = i; break; } }
		}
		if (End == INDEX_NONE) { break; }
		FString Area = Text.Mid(Start, End - Start + 1);
		Search = TagPos + 1;
		if (!Area.Contains(TEXT("Placement_Specified")) || !Area.Contains(TEXT("WindowPosition_X")))
		{
			continue;
		}
		TArray<FString> Ids;
		{
			const FRegexPattern TabPattern(TEXT("\"TabId\": \"([^\"]+)\",\"TabState\": \"OpenedTab\""));
			FRegexMatcher M(TabPattern, Area);
			while (M.FindNext()) { Ids.AddUnique(M.GetCaptureGroup(1)); }
		}
		if (Ids.Num() == 0) { continue; }
		Ids.Sort();
		const FBongLodgePlacement* Saved = Placements.Find(TEXT("Tabs:") + FString::Join(Ids, TEXT("|")));
		if (!Saved) { continue; }
		FBongLodgePlacement P = *Saved;
		FitToMonitors(P);
		const float Dpi = FMath::Max(0.5f, FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(P.X + P.W * 0.5f, P.Y + P.H * 0.5f));
		FString NewArea = Area;
		const FRegexPattern RectPattern(TEXT("\"WindowPosition_X\": [-0-9.e+]+,\"WindowPosition_Y\": [-0-9.e+]+,\"WindowSize_X\": [-0-9.e+]+,\"WindowSize_Y\": [-0-9.e+]+,\"bIsMaximized\": (true|false)"));
		FRegexMatcher RM(RectPattern, Area);
		if (!RM.FindNext()) { continue; }
		const FString Replacement = FString::Printf(TEXT("\"WindowPosition_X\": %.3f,\"WindowPosition_Y\": %.3f,\"WindowSize_X\": %.3f,\"WindowSize_Y\": %.3f,\"bIsMaximized\": %s"),
			P.X / Dpi, P.Y / Dpi, P.W / Dpi, P.H / Dpi, P.bMaximized ? TEXT("true") : TEXT("false"));
		NewArea = Area.Left(RM.GetMatchBeginning()) + Replacement + Area.Mid(RM.GetMatchEnding());
		Text = Text.Left(Start) + NewArea + Text.Mid(End + 1);
		Search = Start + NewArea.Len();
		UE_LOG(LogBongLodge, Log, TEXT("Layout area [%s] set to saved (%d,%d %dx%d max=%d)"), *FString::Join(Ids, TEXT("|")), P.X, P.Y, P.W, P.H, P.bMaximized);
	}
	return Text;
}

void FBongLodgeModule::PrepareConfigBeforeLayoutLoad()
{
	if (!GConfig)
	{
		return;
	}
	// 1) Root window: MainFrame reads [RootWindow] ScreenPosition/WindowSize/InitiallyMaximized
	//    (Editor/MainFrame/Private/Frame/RootWindowLocation.h) in DPI-independent units;
	//    SWindow::Construct multiplies the position by the DPI at that point.
	if (const FBongLodgePlacement* Saved = Placements.Find(BONGLODGE_ROOT_KEY))
	{
		FBongLodgePlacement P = *Saved;
		FitToMonitors(P);
		const float Dpi = FMath::Max(0.5f, FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(P.X + P.W * 0.5f, P.Y + P.H * 0.5f));
		const FVector2D Pos(P.X / Dpi, P.Y / Dpi);
		const FVector2D Size(P.W / Dpi, P.H / Dpi);
		GConfig->SetString(TEXT("RootWindow"), TEXT("ScreenPosition"), *Pos.ToString(), GEditorPerProjectIni);
		GConfig->SetString(TEXT("RootWindow"), TEXT("WindowSize"), *Size.ToString(), GEditorPerProjectIni);
		GConfig->SetBool(TEXT("RootWindow"), TEXT("InitiallyMaximized"), P.bMaximized, GEditorPerProjectIni);
		UE_LOG(LogBongLodge, Log, TEXT("Pre-set [RootWindow] ScreenPosition=%s WindowSize=%s Max=%d (dpi %.2f)"), *Pos.ToString(), *Size.ToString(), P.bMaximized, Dpi);
	}

	// 2) Floating tab areas in saved layouts (Placement_Specified, unscaled position/size):
	//    if an area would open on a monitor that is not connected, move it to the primary.
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);
	if (Metrics.MonitorInfo.Num() == 0)
	{
		return;
	}
	const FPlatformRect& PWA = Metrics.PrimaryDisplayWorkAreaRect;
	const float PrimaryDpi = FMath::Max(0.5f, FPlatformApplicationMisc::GetDPIScaleFactorAtPoint((float)PWA.Left + 10.f, (float)PWA.Top + 10.f));

	FString SectionName;
	GConfig->GetString(TEXT("Slate"), TEXT("EditorLayoutsSectionName"), SectionName, GEditorIni);
	SectionName.TrimStartAndEndInline();
	if (SectionName.IsEmpty()) { SectionName = TEXT("EditorLayouts"); }

	const FConfigSection* Section = GConfig->GetSection(*SectionName, false, GEditorLayoutIni);
	if (!Section)
	{
		return;
	}
	TArray<TPair<FString, FString>> Updates;
	for (const auto& Pair : *Section)
	{
		const FString Original = RewriteAreasFromSaved(Pair.Value.GetValue());
		const bool bRewritten = !Original.Equals(Pair.Value.GetValue(), ESearchCase::CaseSensitive);
		const FRegexPattern Pattern(TEXT("\"WindowPosition_X\": ([-0-9.e+]+),\"WindowPosition_Y\": ([-0-9.e+]+),\"WindowSize_X\": ([-0-9.e+]+),\"WindowSize_Y\": ([-0-9.e+]+)"));
		FRegexMatcher Matcher(Pattern, Original);
		int32 Offset = 0;
		FString Out;
		bool bChanged = false;
		while (Matcher.FindNext())
		{
			const double UX = FCString::Atod(*Matcher.GetCaptureGroup(1));
			const double UY = FCString::Atod(*Matcher.GetCaptureGroup(2));
			const double UW = FCString::Atod(*Matcher.GetCaptureGroup(3));
			const double UH = FCString::Atod(*Matcher.GetCaptureGroup(4));
			const float Dpi = FMath::Max(0.5f, FPlatformApplicationMisc::GetDPIScaleFactorAtPoint((float)UX, (float)UY));
			const int32 X = FMath::RoundToInt(UX * Dpi), Y = FMath::RoundToInt(UY * Dpi);
			const int32 W = FMath::RoundToInt(UW * Dpi), H = FMath::RoundToInt(UH * Dpi);
			if (BongLodgeRectVisible(Metrics, X, Y, W, H))
			{
				continue;
			}
			const double NW = FMath::Min(UW, (PWA.Right - PWA.Left) / (double)PrimaryDpi);
			const double NH = FMath::Min(UH, (PWA.Bottom - PWA.Top) / (double)PrimaryDpi);
			const double NX = PWA.Left / (double)PrimaryDpi + ((PWA.Right - PWA.Left) / (double)PrimaryDpi - NW) * 0.5;
			const double NY = PWA.Top / (double)PrimaryDpi + ((PWA.Bottom - PWA.Top) / (double)PrimaryDpi - NH) * 0.5;
			Out += Original.Mid(Offset, Matcher.GetMatchBeginning() - Offset);
			Out += FString::Printf(TEXT("\"WindowPosition_X\": %.3f,\"WindowPosition_Y\": %.3f,\"WindowSize_X\": %.3f,\"WindowSize_Y\": %.3f"), NX, NY, NW, NH);
			Offset = Matcher.GetMatchEnding();
			bChanged = true;
			UE_LOG(LogBongLodge, Log, TEXT("Layout %s: off-screen area (%d,%d %dx%d) moved to primary"), *Pair.Key.ToString(), X, Y, W, H);
		}
		if (bChanged)
		{
			Out += Original.Mid(Offset);
			Updates.Add(TPair<FString, FString>(Pair.Key.ToString(), Out));
		}
		else if (bRewritten)
		{
			Updates.Add(TPair<FString, FString>(Pair.Key.ToString(), Original));
		}
	}
	for (const auto& U : Updates)
	{
		// in-memory only; the editor writes EditorLayout.ini itself on shutdown
		GConfig->SetString(*SectionName, *U.Key, *U.Value, GEditorLayoutIni);
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
	// Another editor instance may have written the file meanwhile: keep its entries and only
	// overwrite the windows this instance actually moved/closed.
	{
		TMap<FString, FBongLodgePlacement> Mine = MoveTemp(Placements);
		Placements.Reset();
		Load();
		for (const auto& Pair : Mine)
		{
			if (TouchedKeys.Contains(Pair.Key) || !Placements.Contains(Pair.Key))
			{
				Placements.Add(Pair.Key, Pair.Value);
			}
		}
	}
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

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

IMPLEMENT_MODULE(FBongLodgeModule, BongLodge)
