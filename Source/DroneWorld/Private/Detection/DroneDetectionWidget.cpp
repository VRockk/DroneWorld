#include "Detection/DroneDetectionWidget.h"
#include "Detection/DroneDetectionSubsystem.h"
#include "Detection/DroneTrack.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"

void UDroneDetectionWidget::SetSubsystem(UDroneDetectionSubsystem* InSubsystem)
{
	Subsystem = InSubsystem;
}

void UDroneDetectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	const UDroneDetectionSubsystem* S = Subsystem.Get();
	const FDroneDetectionConfig C = S ? S->GetConfig() : FDroneDetectionConfig();

	if (PanelBorder) { PanelBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f)); }

	// Config text fields: show the current value, write back on commit.
	auto Init = [](UEditableTextBox* Box, float V) { if (Box) { Box->SetText(FText::FromString(FString::Printf(TEXT("%g"), V))); } };
	Init(ConfidenceText, C.ConfThreshold);
	Init(RecoveryText, C.DetectThreshold);
	Init(InferenceHzText, C.InferenceHz);
	Init(MinHitsText, (float)C.MinHits);
	Init(MaxAgeText, (float)C.MaxAge);
	Init(IoUText, C.IoUMatch);
	Init(MotionLeadText, C.MotionLead);

	if (ConfidenceText)  { ConfidenceText->OnTextCommitted.AddDynamic(this, &UDroneDetectionWidget::OnConfidenceCommitted); }
	if (RecoveryText)    { RecoveryText->OnTextCommitted.AddDynamic(this, &UDroneDetectionWidget::OnRecoveryCommitted); }
	if (InferenceHzText) { InferenceHzText->OnTextCommitted.AddDynamic(this, &UDroneDetectionWidget::OnInferenceHzCommitted); }
	if (MinHitsText)     { MinHitsText->OnTextCommitted.AddDynamic(this, &UDroneDetectionWidget::OnMinHitsCommitted); }
	if (MaxAgeText)      { MaxAgeText->OnTextCommitted.AddDynamic(this, &UDroneDetectionWidget::OnMaxAgeCommitted); }
	if (IoUText)         { IoUText->OnTextCommitted.AddDynamic(this, &UDroneDetectionWidget::OnIoUCommitted); }
	if (MotionLeadText)  { MotionLeadText->OnTextCommitted.AddDynamic(this, &UDroneDetectionWidget::OnMotionLeadCommitted); }

	if (ToggleButton) { ToggleButton->OnClicked.AddDynamic(this, &UDroneDetectionWidget::OnToggleClicked); }

	// Video picker: only shown when the level has an M_PostProcessVideoPlayer post-process volume driving video.
	if (S && S->IsVideoActive())
	{
		if (VideoCombo)
		{
			VideoCombo->ClearOptions();
			for (const FString& V : S->GetAvailableVideos()) { VideoCombo->AddOption(V); }
			const FString Cur = S->GetCurrentVideoFile();
			if (!Cur.IsEmpty()) { VideoCombo->SetSelectedOption(Cur); }
			VideoCombo->OnSelectionChanged.AddDynamic(this, &UDroneDetectionWidget::OnVideoSelected);
		}
	}
	else   // no video in this level -> hide the picker row entirely
	{
		if (Row_Video) { Row_Video->SetVisibility(ESlateVisibility::Collapsed); }
		else if (VideoCombo) { VideoCombo->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

void UDroneDetectionWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (StatusText)
	{
		if (const UDroneDetectionSubsystem* S = Subsystem.Get())
		{
			// Searching (white, animated dots) / Detecting (yellow) / Drone detected (red) — driven by the
			// two confidence levels in the subsystem.
			StatusText->SetText(FText::FromString(S->GetBannerText()));
			StatusText->SetColorAndOpacity(FSlateColor(S->GetBannerColor()));
		}
	}

	// Detection boxes/trail are redrawn every frame in NativePaint.
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UDroneDetectionWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const UDroneDetectionSubsystem* S = Subsystem.Get();
	if (!S) { return LayerId; }

	const FDroneDetectionConfig& C = S->GetConfig();
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const int32 Coast = S->GetTrackDisplayCoast();
	const float Thickness = FMath::Max(0.5f, C.BoxThickness);
	const float Pad = C.BoxSizeOffset * 0.5f;
	const int32 FontPx = FMath::Max(8, C.ScoreFontSize);
	const FSlateFontInfo ScoreFont = FCoreStyle::GetDefaultFontStyle("Bold", FontPx);
	const FPaintGeometry PG = AllottedGeometry.ToPaintGeometry();

	for (const FDroneTrack& T : S->GetTracks())
	{
		if (!T.bConfirmed || T.TimeSinceUpdate > Coast) { continue; }
		float X1 = T.DispX1 * Size.X, Y1 = T.DispY1 * Size.Y, X2 = T.DispX2 * Size.X, Y2 = T.DispY2 * Size.Y;
		const float ExpandX = (X2 - X1) * Pad, ExpandY = (Y2 - Y1) * Pad;
		X1 -= ExpandX; X2 += ExpandX; Y1 -= ExpandY; Y2 += ExpandY;

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

		// Score sits clear ABOVE the box (extra gap so it doesn't touch the outline), or below if there's no room.
		const float LabelH = (float)FontPx + 12.f;
		const float LabelY = (Y1 >= LabelH) ? (Y1 - LabelH) : (Y2 + 4.f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToOffsetPaintGeometry(FVector2f(X1, LabelY)),
			FString::Printf(TEXT("%.2f"), T.Score), ScoreFont, ESlateDrawEffect::None, C.ScoreColor);
	}

	return LayerId + 3;
}

void UDroneDetectionWidget::OnConfidenceCommitted(const FText& Text, ETextCommit::Type)
{
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { FDroneDetectionConfig C = S->GetConfig(); C.ConfThreshold = FMath::Clamp(FCString::Atof(*Text.ToString()), 0.f, 1.f); S->SetConfig(C); }
}
void UDroneDetectionWidget::OnRecoveryCommitted(const FText& Text, ETextCommit::Type)
{
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { FDroneDetectionConfig C = S->GetConfig(); C.DetectThreshold = FMath::Clamp(FCString::Atof(*Text.ToString()), 0.f, 1.f); S->SetConfig(C); }
}
void UDroneDetectionWidget::OnInferenceHzCommitted(const FText& Text, ETextCommit::Type)
{
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { FDroneDetectionConfig C = S->GetConfig(); C.InferenceHz = FMath::Clamp(FCString::Atof(*Text.ToString()), 0.f, 120.f); S->SetConfig(C); }
}
void UDroneDetectionWidget::OnMinHitsCommitted(const FText& Text, ETextCommit::Type)
{
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { FDroneDetectionConfig C = S->GetConfig(); C.MinHits = FMath::Max(1, FMath::RoundToInt(FCString::Atof(*Text.ToString()))); S->SetConfig(C); }
}
void UDroneDetectionWidget::OnMaxAgeCommitted(const FText& Text, ETextCommit::Type)
{
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { FDroneDetectionConfig C = S->GetConfig(); C.MaxAge = FMath::Max(1, FMath::RoundToInt(FCString::Atof(*Text.ToString()))); S->SetConfig(C); }
}
void UDroneDetectionWidget::OnIoUCommitted(const FText& Text, ETextCommit::Type)
{
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { FDroneDetectionConfig C = S->GetConfig(); C.IoUMatch = FMath::Clamp(FCString::Atof(*Text.ToString()), 0.f, 1.f); S->SetConfig(C); }
}
void UDroneDetectionWidget::OnMotionLeadCommitted(const FText& Text, ETextCommit::Type)
{
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { FDroneDetectionConfig C = S->GetConfig(); C.MotionLead = FMath::Clamp(FCString::Atof(*Text.ToString()), 0.f, 5.f); S->SetConfig(C); }
}
void UDroneDetectionWidget::OnVideoSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct) { return; }   // ignore the programmatic SetSelectedOption during init
	if (UDroneDetectionSubsystem* S = Subsystem.Get()) { S->SwitchVideo(SelectedItem); }
}
void UDroneDetectionWidget::OnToggleClicked()
{
	bPanelShown = !bPanelShown;
	if (PanelBorder) { PanelBorder->SetVisibility(bPanelShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}
