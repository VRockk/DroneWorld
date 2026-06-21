#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneDetectionWidget.generated.h"

class UDroneDetectionSubsystem;
class UTextBlock;
class UEditableTextBox;
class UComboBoxString;
class UBorder;
class UButton;
class UHorizontalBox;

// The detection HUD, as a real Widget Blueprint (parent of WBP_DroneDetection). The dynamic detection boxes +
// trajectory trail are drawn in NativePaint (variable count/position). The video itself is NOT here — it plays in
// the world (ADroneVideoScreen) and is captured by the SceneViewExtension, so this is HUD-only. Everything below
// is a normal UMG widget you can add / move / restyle in the designer — name it to match, tick "Is Variable":
//   StatusText  (TextBlock)      "DRONE DETECTED" / "NO DRONE"
//   ToggleButton(Button)         show / hide the config panel
//   PanelBorder (Border)         config-panel background (C++ tints it dark)
//   VideoCombo  (ComboBoxString) pick the Content/Movies clip the world video screen plays, at runtime
//   ConfidenceText / RecoveryText / InferenceHzText / MinHitsText / MaxAgeText / IoUText / MotionLeadText (EditableTextBox)
UCLASS()
class DRONEWORLD_API UDroneDetectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSubsystem(UDroneDetectionSubsystem* InSubsystem);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UButton> ToggleButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UBorder> PanelBorder;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UComboBoxString> VideoCombo;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UHorizontalBox> Row_Video;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UEditableTextBox> ConfidenceText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UEditableTextBox> RecoveryText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UEditableTextBox> InferenceHzText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UEditableTextBox> MinHitsText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UEditableTextBox> MaxAgeText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UEditableTextBox> IoUText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drone") TObjectPtr<UEditableTextBox> MotionLeadText;

	UFUNCTION() void OnConfidenceCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnRecoveryCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnInferenceHzCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnMinHitsCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnMaxAgeCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnIoUCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnMotionLeadCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnVideoSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void OnToggleClicked();

private:
	bool bPanelShown = true;
	TWeakObjectPtr<UDroneDetectionSubsystem> Subsystem;
};
