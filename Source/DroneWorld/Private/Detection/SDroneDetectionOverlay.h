#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateBrush.h"

class UDroneDetectionSubsystem;

// On-screen overlay drawn over the observer's live view: detection boxes + a DRONE / NO-DRONE banner, plus a
// small config panel whose sliders read the running config on startup and push changes back live.
class SDroneDetectionOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDroneDetectionOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UDroneDetectionSubsystem>, Subsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	TSharedRef<class SWidget> MakeConfigPanel();

	TWeakObjectPtr<UDroneDetectionSubsystem> Subsystem;
	FSlateBrush BackgroundBrush;   // video-mode full-screen background (references the MediaTexture)
};
