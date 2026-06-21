#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Detection/DroneDetectionSubsystem.h"   // FDroneDetectionConfig
#include "Templates/SubclassOf.h"
#include "DroneDetector.generated.h"

class UNNEModelData;
class UDroneDetectionWidget;
class UCineCameraComponent;

// Thin, Blueprint-able bootstrap for the detection subsystem. Place a BP subclass (BP_DroneDetection) in
// the level and attach the model. The model is REQUIRED and has no default — if unset, the subsystem logs
// an error and stays idle (no crash). Config defaults here feed the on-screen panel on startup.
UCLASS()
class DRONEWORLD_API ADroneDetector : public AActor
{
	GENERATED_BODY()

public:
	ADroneDetector();

	// Attached in the Blueprint. No default, no hardcoded path.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Model",
		meta = (DisplayName = "Model (NNEModelData asset)", DisplayPriority = "0"))
	TObjectPtr<UNNEModelData> Model = nullptr;

	// Detection / Model / Visual settings — shown inline (grouped below). Hover any field for what it does.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ShowOnlyInnerProperties))
	FDroneDetectionConfig Config;

	// HUD to display (detection boxes + on-screen config panel). Assign WBP_DroneDetection here. If left empty,
	// a built-in overlay is used. The HUD's look (colors, box thickness, font, box size) comes from Visual below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (DisplayPriority = "0"))
	TSubclassOf<UDroneDetectionWidget> HudWidgetClass;

	// --- Camera (pan / tilt / zoom). Add a Cine Camera component to this Blueprint and it becomes the main view;
	//     the detector drives it: a slow eased search sweep while idle, centering + zoom-in when it sees something
	//     (yellow), locking + deeper zoom + faster tracking when it's sure (red), easing back home when it loses it. ---

	// Idle / search focal length (wide angle), in mm. The view zooms out to this when nothing is detected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "4.0", ClampMax = "100.0", DisplayPriority = "0"))
	float WideFocalLength = 15.f;

	// Maximum zoom (telephoto cap), in mm. The view never zooms past this, however far the object is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "50.0", ClampMax = "1000.0", DisplayPriority = "1"))
	float MaxFocalLength = 300.f;

	// Search sweep half-angle, in degrees. While idle the view pans this far left and right of its start heading.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "0.0", ClampMax = "90.0", DisplayPriority = "2"))
	float SearchYawRange = 45.f;

	// Seconds for one full search sweep (centre -> right -> left -> centre). Larger = slower, calmer pan.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "1.0", ClampMax = "60.0", DisplayPriority = "3"))
	float SearchPeriod = 8.f;

	// How quickly the view eases onto a target (spring rate). Higher = snappier; locked-on (red) tracks faster.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "0.2", ClampMax = "12.0", DisplayPriority = "4"))
	float TrackSpeed = 3.f;

	// How quickly the focal length eases to its target (zoom spring rate). Higher = faster zoom.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "0.2", ClampMax = "12.0", DisplayPriority = "5"))
	float ZoomSpeed = 2.5f;

	// Zoom (focal length, mm) while investigating a possible drone (yellow): a moderate zoom-in to get a better look.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "15.0", ClampMax = "1000.0", DisplayPriority = "6"))
	float DetectingFocalLength = 50.f;

	// Zoom (focal length, mm) once a drone is confirmed (red): a bit tighter than the investigate zoom. Bounded by
	// Max Focal Length — keep it modest so it doesn't zoom all the way in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Camera", meta = (ClampMin = "15.0", ClampMax = "1000.0", DisplayPriority = "7"))
	float DetectedFocalLength = 100.f;

	// Live setters for the on-screen config panel — push changes to the running subsystem.
	UFUNCTION(BlueprintCallable, Category = "Detection") void SetConfThreshold(float V);
	UFUNCTION(BlueprintCallable, Category = "Detection") void SetDetectThreshold(float V);
	UFUNCTION(BlueprintCallable, Category = "Detection") void SetInferenceHz(float V);
	UFUNCTION(BlueprintCallable, Category = "Detection") void SetIoUMatch(float V);
	UFUNCTION(BlueprintCallable, Category = "Detection") void SetMinHits(int32 V);
	UFUNCTION(BlueprintCallable, Category = "Detection") void SetMaxAge(int32 V);
	UFUNCTION(BlueprintCallable, Category = "Detection") void SetTrackDisplayCoast(int32 V);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UDroneDetectionSubsystem* Sub() const;
	void PushConfig();
	void UpdateCamera(float Dt);

	// The Cine Camera found on this actor (drives the view). Null = no camera control (detection still runs).
	UPROPERTY(Transient) TObjectPtr<UCineCameraComponent> CineCamera = nullptr;
	FRotator BaseRot = FRotator::ZeroRotator;   // start pose, captured at BeginPlay (search sweeps around this)
	float CamYaw = 0.f, CamPitch = 0.f;          // current commanded world angles
	float YawVel = 0.f, PitchVel = 0.f;          // spring velocities
	float CurFocal = 15.f, FocalVel = 0.f;       // current focal length + spring velocity
	float SearchPhase = 0.f;                     // search-sweep oscillator phase
	bool bViewTargetSet = false;
	EDroneDetectionState PrevState = EDroneDetectionState::Searching;
};
