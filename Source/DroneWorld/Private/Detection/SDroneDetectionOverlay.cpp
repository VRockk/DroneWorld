#include "Detection/SDroneDetectionOverlay.h"
#include "Detection/DroneDetectionSubsystem.h"

#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture.h"

#define LOCTEXT_NAMESPACE "DroneDetection"

namespace
{
	// One labeled spin-box row bound to a config field: reads live on display, writes live on change.
	// Tip shows on hover (over the whole row) to explain what the value does.
	TSharedRef<SWidget> MakeRow(const FString& Label, const FString& Tip, TWeakObjectPtr<UDroneDetectionSubsystem> Sub,
		float Min, float Max, TFunction<float(const FDroneDetectionConfig&)> Get,
		TFunction<void(FDroneDetectionConfig&, float)> Set)
	{
		return SNew(SHorizontalBox)
			.ToolTipText(FText::FromString(Tip))
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
			[ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FLinearColor::White) ]
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(Min).MaxValue(Max).MinSliderValue(Min).MaxSliderValue(Max)
				.Value_Lambda([Sub, Get]() { return Sub.IsValid() ? Get(Sub->GetConfig()) : 0.f; })
				.OnValueChanged_Lambda([Sub, Set](float V)
				{
					if (Sub.IsValid()) { FDroneDetectionConfig C = Sub->GetConfig(); Set(C, V); Sub->SetConfig(C); }
				})
			];
	}
}

void SDroneDetectionOverlay::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;

	// Boxes + banner are painted in OnPaint; the live scene (incl. any world video screen) shows through behind.
	TSharedRef<SOverlay> Root = SNew(SOverlay);

	Root->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(12.f)
	[
		SNew(SBox).WidthOverride(280.f)
		[ MakeConfigPanel() ]
	];

	ChildSlot [ Root ];
}

TSharedRef<SWidget> SDroneDetectionOverlay::MakeConfigPanel()
{
	TWeakObjectPtr<UDroneDetectionSubsystem> Sub = Subsystem;
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		.Padding(8.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ SNew(STextBlock).Text(LOCTEXT("Title", "Drone Detection")).ColorAndOpacity(FLinearColor(0.2f, 1.f, 0.2f)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ MakeRow(TEXT("Confidence"), TEXT("Confirmed-track gate. Higher = fewer false positives (more conservative); lower = catches fainter drones."), Sub, 0.f, 1.f,
				[](const FDroneDetectionConfig& C) { return C.ConfThreshold; },
				[](FDroneDetectionConfig& C, float V) { C.ConfThreshold = V; }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ MakeRow(TEXT("Recovery thr"), TEXT("ByteTrack low threshold. Recovers weak detections that fall below the confidence gate so a track survives brief dips."), Sub, 0.f, 1.f,
				[](const FDroneDetectionConfig& C) { return C.DetectThreshold; },
				[](FDroneDetectionConfig& C, float V) { C.DetectThreshold = V; }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ MakeRow(TEXT("Inference Hz (0=max)"), TEXT("Detections per second. 0 = run as fast as the GPU allows (uncapped). Raise to cap the rate."), Sub, 0.f, 120.f,
				[](const FDroneDetectionConfig& C) { return C.InferenceHz; },
				[](FDroneDetectionConfig& C, float V) { C.InferenceHz = V; }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ MakeRow(TEXT("IoU match"), TEXT("Box-overlap needed to link a detection to an existing track. Higher = stricter matching."), Sub, 0.f, 1.f,
				[](const FDroneDetectionConfig& C) { return C.IoUMatch; },
				[](FDroneDetectionConfig& C, float V) { C.IoUMatch = V; }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ MakeRow(TEXT("Min hits"), TEXT("How many detections in a row before a track is confirmed and drawn. Higher = steadier but slower to appear."), Sub, 1.f, 10.f,
				[](const FDroneDetectionConfig& C) { return (float)C.MinHits; },
				[](FDroneDetectionConfig& C, float V) { C.MinHits = FMath::RoundToInt(V); }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ MakeRow(TEXT("Motion lead"), TEXT("Draw the box this many frames ahead along the drone's motion to cancel lag. 0 = on the last detection (trails); higher = leads more (too high overshoots on turns)."), Sub, 0.f, 5.f,
				[](const FDroneDetectionConfig& C) { return C.MotionLead; },
				[](FDroneDetectionConfig& C, float V) { C.MotionLead = V; }) ]
		];
}

int32 SDroneDetectionOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Paint the config panel first.
	LayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	UDroneDetectionSubsystem* S = Subsystem.Get();
	if (!S) { return LayerId; }

	const FDroneDetectionConfig& C = S->GetConfig();
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const int32 Coast = S->GetTrackDisplayCoast();
	const float Thickness = FMath::Max(0.5f, C.BoxThickness);
	const float Pad = C.BoxSizeOffset * 0.5f;   // fraction added to each side
	const int32 FontPx = FMath::Max(8, C.ScoreFontSize);
	const FSlateFontInfo ScoreFont = FCoreStyle::GetDefaultFontStyle("Bold", FontPx);
	const FPaintGeometry PG = AllottedGeometry.ToPaintGeometry();

	for (const FDroneTrack& T : S->GetTracks())
	{
		if (!T.bConfirmed || T.TimeSinceUpdate > Coast) { continue; }
		float X1 = T.DispX1 * Size.X, Y1 = T.DispY1 * Size.Y, X2 = T.DispX2 * Size.X, Y2 = T.DispY2 * Size.Y;
		// Enlarge the drawn box a touch for visibility (BoxSizeOffset).
		const float ExpandX = (X2 - X1) * Pad, ExpandY = (Y2 - Y1) * Pad;
		X1 -= ExpandX; X2 += ExpandX; Y1 -= ExpandY; Y2 += ExpandY;
		// Trajectory trail (path the drone has taken).
		if (T.Trail.Num() > 1)
		{
			TArray<FVector2D> TrailPts;
			TrailPts.Reserve(T.Trail.Num());
			for (const FVector2D& P : T.Trail) { TrailPts.Add(FVector2D(P.X * Size.X, P.Y * Size.Y)); }
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PG, TrailPts, ESlateDrawEffect::None,
				C.TrailColor, true, FMath::Max(1.f, Thickness - 1.f));
		}
		const TArray<FVector2D> Pts = { {X1, Y1}, {X2, Y1}, {X2, Y2}, {X1, Y2}, {X1, Y1} };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PG, Pts, ESlateDrawEffect::None, C.BoxColor, true, Thickness);
		// Place the score clear of the box (above it, or below if there's no room) so it never overlaps.
		const float LabelH = (float)FontPx + 5.f;
		const float LabelY = (Y1 >= LabelH) ? (Y1 - LabelH) : (Y2 + 3.f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToOffsetPaintGeometry(FVector2f(X1, LabelY)),
			FString::Printf(TEXT("%.2f"), T.Score), ScoreFont, ESlateDrawEffect::None, C.ScoreColor);
	}

	// Searching (white, animated dots) / Detecting (yellow) / Drone detected (red).
	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
		AllottedGeometry.ToOffsetPaintGeometry(FVector2f(16.f, 16.f)),
		S->GetBannerText(), FCoreStyle::GetDefaultFontStyle("Bold", 24), ESlateDrawEffect::None,
		S->GetBannerColor());

	return LayerId + 3;
}

#undef LOCTEXT_NAMESPACE
