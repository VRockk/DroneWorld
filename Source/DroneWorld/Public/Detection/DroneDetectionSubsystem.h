#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Detection/DroneTrack.h"
#include "NNERuntimeGPU.h"
#include "Templates/SubclassOf.h"
#include "DroneDetectionSubsystem.generated.h"

class UNNEModelData;
class UUserWidget;
class UDroneDetectionWidget;
class UMediaPlayer;
class UMediaTexture;
class UFileMediaSource;
class UMaterialInstanceDynamic;
class FDroneDetectionViewExtension;
class SDroneDetectionOverlay;

// Two-stage detection state, driven by the confidence levels below. Drives both the HUD banner and the
// dynamic camera: Searching = nothing above the yellow level (idle search sweep); Detecting = above yellow,
// below red (unsure — center + zoom in to get a better look); Detected = above red (confident — lock on,
// zoom further, track faster).
UENUM(BlueprintType)
enum class EDroneDetectionState : uint8
{
	Searching,
	Detecting,
	Detected
};

// Tunable detection/tracking parameters. Authored on the bootstrap actor (Blueprint) and editable live
// from the on-screen config panel; the panel reads these on startup and writes them back on change.
USTRUCT(BlueprintType)
struct FDroneDetectionConfig
{
	GENERATED_BODY()

	// --- Config: detection + tracking behavior ---

	// How confident the model must be to confirm a drone. Higher = fewer false alarms (more conservative);
	// lower = catches fainter / lower-confidence drones. This is the main "how sure must it be" dial.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ConfThreshold = 0.25f;

	// Lower keep-alive threshold. Detections above this but below Confidence can't START a track, but they keep
	// an existing one alive through brief confidence dips. Lower = tracks survive longer through rough patches.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DetectThreshold = 0.10f;

	// "Unsure" gate. A drone-class detection at/above this (but below the red level) puts the system in the
	// yellow "Detecting object..." state: the camera starts centering on it and zooming in for a better look.
	// Set low so faint / distant objects trigger a closer look.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ConfidenceYellowLevel = 0.05f;

	// "Sure" gate. At/above this the system is confident it's a drone: red "Drone detected!", the camera locks
	// on, zooms in further and tracks faster. Keep above the yellow level.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ConfidenceRedLevel = 0.4f;

	// Detections per second. 0 = run as fast as the GPU allows (uncapped). Set a value to cap the rate (saves GPU).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float InferenceHz = 0.f;

	// Consecutive detections required before a track is confirmed and drawn. Higher = steadier (fewer flickers)
	// but slower to first appear.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "1"))
	int32 MinHits = 3;

	// Missed inferences before a track is deleted entirely. Higher = remembers a drone longer after it disappears.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "1"))
	int32 MaxAge = 30;

	// Minimum box overlap (0-1) needed to link a new detection to an existing track. Higher = stricter matching;
	// lower = more forgiving of fast movement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IoUMatch = 0.2f;

	// How far ahead to draw the box along the drone's motion, to cancel capture + inference lag, in detection
	// frames. 0 = sit on the last detection (box trails a moving drone); ~1.5 keeps it on the drone; too high
	// overshoots on sudden direction changes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float MotionLead = 1.5f;

	// While investigating a possible drone (yellow), how many seconds to keep looking before giving up if it never
	// reaches the red "sure" level. Stops the camera from staring at an ambiguous object forever.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.5", ClampMax = "15.0"))
	float InvestigateTimeout = 3.0f;

	// After investigating something that turned out NOT to be a drone, ignore weak (yellow-level) signals for this
	// many seconds and keep searching, so the camera sweeps past it instead of immediately zooming back in. Stops
	// the zoom-in / zoom-out oscillation on false alarms. A confident (red-level) detection always breaks through.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float RejectCooldown = 4.0f;

	// --- Model ---

	// Number of object classes the model was trained on (this model = 4: airplane/bird/drone/helicopter).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Model", meta = (ClampMin = "1", DisplayPriority = "1"))
	int32 ModelClassCount = 4;

	// Which trained class counts as "drone" — only this class fires the alarm. 4-object model = airplane 0 /
	// bird 1 / drone 2 / helicopter 3, so 2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Model", meta = (ClampMin = "0", DisplayPriority = "2"))
	int32 DroneClassIndex = 2;

	// --- Visual: how detections are drawn (live) ---

	// Detection box outline thickness, in pixels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (ClampMin = "0.5", ClampMax = "12.0", DisplayPriority = "1"))
	float BoxThickness = 2.5f;

	// Grows (+) or shrinks (-) the drawn box around the detection, as a fraction of its size (0 = exact fit,
	// 0.2 = 20% larger). Handy to make small drones easier to see.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (ClampMin = "-0.5", ClampMax = "2.0", DisplayPriority = "2"))
	float BoxSizeOffset = 0.18f;

	// Detection box outline color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (DisplayPriority = "3"))
	FLinearColor BoxColor = FLinearColor(1.f, 0.f, 0.f, 1.f);

	// Point size of the confidence-score text drawn next to each box.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (ClampMin = "8", ClampMax = "48", DisplayPriority = "4"))
	int32 ScoreFontSize = 22;

	// Confidence-score text color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (DisplayPriority = "5"))
	FLinearColor ScoreColor = FLinearColor(1.f, 0.95f, 0.2f, 1.f);

	// Keep DRAWING a confirmed track for up to this many missed inferences, so the box coasts through short gaps
	// instead of blinking out (separate from Max Age, which controls deletion).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (ClampMin = "0", DisplayPriority = "6"))
	int32 TrackDisplayCoast = 5;

	// Trajectory trail line color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Visual", meta = (DisplayPriority = "7"))
	FLinearColor TrailColor = FLinearColor(1.f, 0.4f, 0.4f, 0.6f);
};

// Owns the vision-only drone detector: backbuffer capture (SceneViewExtension), NNE/TensorRT inference,
// the OC-SORT + ByteTrack tracker, and the on-screen overlay. Idle until StartDetection is called with a
// model; logs an error and stays idle if no model is attached.
UCLASS()
class DRONEWORLD_API UDroneDetectionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	// FTickableGameObject (via UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bReady; }

	// Bootstrap entry. InModel is REQUIRED — if null, logs an error and stays idle (no detection, no crash).
	// Always captures the live scene backbuffer (SceneViewExtension). To detect on a video, place an
	// ADroneVideoScreen in the level — it plays the clip full-screen in the world, captured the same way.
	void StartDetection(UNNEModelData* InModel, const FDroneDetectionConfig& InConfig, TSubclassOf<UUserWidget> InHudClass);
	void StopDetection();

	void SetConfig(const FDroneDetectionConfig& InConfig) { Config = InConfig; }
	const FDroneDetectionConfig& GetConfig() const { return Config; }

	// Latest captured frame from the SceneViewExtension (called on the game thread after GPU readback).
	void IngestFrame(int32 InW, int32 InH, TArray<FColor>&& InPixels);

	// For the overlay.
	const TArray<FDroneTrack>& GetTracks() const { return Tracker.Tracks; }
	int32 GetTrackDisplayCoast() const { return Config.TrackDisplayCoast; }
	int32 GetRawDetectionCount() const { return Detections.Num(); }
	bool IsReady() const { return bReady; }
	float GetLastInferenceMs() const { return LastInferenceMs; }

	// Two-threshold state for the HUD banner and the dynamic camera.
	EDroneDetectionState GetDetectionState() const { return DetState; }
	bool HasTarget() const { return DetState != EDroneDetectionState::Searching; }
	FVector2D GetTargetCenterNorm() const { return StateTargetCenter; }   // [0..1] screen-space, valid when HasTarget()
	float GetTargetFillNorm() const { return StateTargetFill; }           // target box size as a fraction of the frame
	float GetSignalScore() const { return StateScoreSmoothed; }
	FString GetBannerText() const;        // "Searching for drones" (+animated dots) / "Detecting object..." / "Drone detected!"
	FLinearColor GetBannerColor() const;  // white / yellow / red

	// Video-via-post-process: active only if the level has a Post Process Volume whose material is
	// M_PostProcessVideoPlayer. When active, the detector plays a Content/Movies clip into that material's
	// "VideoFrame" texture param, so the SVE captures it exactly like the live scene. When inactive the HUD
	// hides its video picker and no video plays.
	bool IsVideoActive() const { return bVideoActive; }
	TArray<FString> GetAvailableVideos() const;
	const FString& GetCurrentVideoFile() const { return VideoFile; }
	void SwitchVideo(const FString& InFile);

private:
	bool SetupModel(UNNEModelData* InModel);
	void RunInferenceOnLatest();
	void UpdateDetectionState(float SignalScore, const FVector2D& Center, float Fill);
	void SetupVideoPostProcess();
	void OpenVideo(const FString& PathOrName);

	TSharedPtr<UE::NNE::IModelInstanceGPU> ModelInstance;
	TSharedPtr<FDroneDetectionViewExtension, ESPMode::ThreadSafe> ViewExtension;
	UPROPERTY() TObjectPtr<UUserWidget> HudWidget;        // HUD Widget Blueprint instance (when one is assigned)
	TSharedPtr<SDroneDetectionOverlay> SlateOverlay;      // fallback overlay when no HUD Blueprint is assigned
	TSubclassOf<UUserWidget> HudWidgetClass;
	FDroneTracker Tracker;
	FDroneDetectionConfig Config;
	bool bReady = false;

	// Video (post-process material) — only active if the level has a PP volume using M_PostProcessVideoPlayer.
	bool bVideoActive = false;
	FString VideoFile;
	UPROPERTY() TObjectPtr<UMediaPlayer> MediaPlayer;
	UPROPERTY() TObjectPtr<UMediaTexture> MediaTexture;
	UPROPERTY() TObjectPtr<UFileMediaSource> MediaSource;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> VideoMID;

	// Latest captured frame (written by the capture path, read by inference).
	FCriticalSection FrameLock;
	TArray<FColor> LatestFrame;
	int32 FrameW = 0;
	int32 FrameH = 0;
	bool bHaveFrame = false;

	float TimeSinceInfer = 0.f;
	float LastInferenceMs = 0.f;
	int32 InferCount = 0;

	// Two-threshold detection state (drives the banner + the dynamic camera).
	EDroneDetectionState DetState = EDroneDetectionState::Searching;
	float StateScoreSmoothed = 0.f;
	FVector2D StateTargetCenter = FVector2D(0.5f, 0.5f);   // smoothed screen-space center of the current target
	float StateTargetFill = 0.f;                           // smoothed target box size (fraction of the frame)
	float LowSignalSecs = 0.f;                             // time since the signal was last above the yellow level
	float DetectingSecs = 0.f;                             // time continuously spent investigating (yellow)
	float RejectCooldownSecs = 0.f;                        // remaining false-alarm cooldown (suppresses yellow re-trigger)
	bool bReachedRedThisEpisode = false;                   // did the current detection episode ever reach red (sure)

	// RF-DETR contract.
	static constexpr int32 InputSize = 880;     // [1,3,880,880]
	static constexpr int32 NumQueries = 300;    // dets [1,300,4]
	static constexpr int32 NumClassesP1 = 91;   // labels [1,300,91]

	TArray<float> InputData;
	TArray<float> DetsOut;
	TArray<float> LabelsOut;
	TArray<FDroneDetection> Detections;
};
