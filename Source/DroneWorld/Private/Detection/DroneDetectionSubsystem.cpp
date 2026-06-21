#include "Detection/DroneDetectionSubsystem.h"

#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeRunSync.h"   // FTensorBindingCPU
#include "NNEStatus.h"
#include "NNETypes.h"
#include "Detection/DroneDetectionViewExtension.h"
#include "Detection/SDroneDetectionOverlay.h"
#include "Detection/DroneDetectionWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "SceneViewExtension.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SWidget.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDroneDetection, Log, All);

bool UDroneDetectionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDroneDetectionSubsystem::Deinitialize()
{
	StopDetection();
	Super::Deinitialize();
}

TStatId UDroneDetectionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDroneDetectionSubsystem, STATGROUP_Tickables);
}

void UDroneDetectionSubsystem::StartDetection(UNNEModelData* InModel, const FDroneDetectionConfig& InConfig, TSubclassOf<UUserWidget> InHudClass)
{
	if (bReady) { StopDetection(); }   // restart cleanly if another detector re-starts us

	Config = InConfig;
	HudWidgetClass = InHudClass;
	Tracker.Reset();

	if (!InModel)
	{
		UE_LOG(LogDroneDetection, Error,
			TEXT("No Model attached. Assign an NNEModelData (.uasset) on the BP_DroneDetection actor; detection disabled."));
		bReady = false;
		return;
	}

	bReady = SetupModel(InModel);
	if (!bReady) { return; }

	// Always capture the live scene backbuffer. A video level just adds an ADroneVideoScreen that plays the clip
	// full-screen in the world, so it is captured exactly the same way.
	ViewExtension = FSceneViewExtensions::NewExtension<FDroneDetectionViewExtension>();
	ViewExtension->SetEnabled(true);
	UE_LOG(LogDroneDetection, Log, TEXT("Drone detection ready (scene capture)."));

	SetupVideoPostProcess();   // play a clip into the level's M_PostProcessVideoPlayer volume, if present
}

void UDroneDetectionSubsystem::StopDetection()
{
	bReady = false;
	if (HudWidget)
	{
		HudWidget->RemoveFromParent();
		HudWidget = nullptr;
	}
	if (SlateOverlay.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(SlateOverlay.ToSharedRef());
		}
		SlateOverlay.Reset();
	}
	if (ViewExtension.IsValid())
	{
		ViewExtension->SetEnabled(false);
		ViewExtension.Reset();
	}
	if (MediaPlayer) { MediaPlayer->Close(); }
	MediaPlayer = nullptr;
	MediaTexture = nullptr;
	MediaSource = nullptr;
	VideoMID = nullptr;
	bVideoActive = false;
	ModelInstance.Reset();
	FScopeLock Lock(&FrameLock);
	LatestFrame.Empty();
	bHaveFrame = false;
}

bool UDroneDetectionSubsystem::SetupModel(UNNEModelData* InModel)
{
	using namespace UE::NNE;

	TWeakInterfacePtr<INNERuntimeGPU> Runtime = GetRuntime<INNERuntimeGPU>(TEXT("NNERuntimeTRT"));
	if (!Runtime.IsValid())
	{
		UE_LOG(LogDroneDetection, Error, TEXT("Runtime 'NNERuntimeTRT' not available. Available: %s"),
			*FString::Join(GetAllRuntimeNames(), TEXT(", ")));
		return false;
	}
	if (Runtime->CanCreateModelGPU(InModel) != EResultStatus::Ok)
	{
		UE_LOG(LogDroneDetection, Error, TEXT("NNERuntimeTRT cannot create a model from this data (needs a cooked TRT engine)."));
		return false;
	}

	UE_LOG(LogDroneDetection, Log, TEXT("Building/loading TensorRT engine (first run can take minutes)..."));
	TSharedPtr<IModelGPU> Model = Runtime->CreateModelGPU(InModel);
	if (!Model) { UE_LOG(LogDroneDetection, Error, TEXT("CreateModelGPU failed.")); return false; }

	ModelInstance = Model->CreateModelInstanceGPU();
	if (!ModelInstance) { UE_LOG(LogDroneDetection, Error, TEXT("CreateModelInstanceGPU failed.")); return false; }

	TArray<FTensorShape> InShapes = { FTensorShape::Make({ 1u, 3u, (uint32)InputSize, (uint32)InputSize }) };
	if (ModelInstance->SetInputTensorShapes(InShapes) != EResultStatus::Ok)
	{
		UE_LOG(LogDroneDetection, Error, TEXT("SetInputTensorShapes failed."));
		return false;
	}

	InputData.SetNumUninitialized(3 * InputSize * InputSize);
	DetsOut.SetNumUninitialized(NumQueries * 4);
	LabelsOut.SetNumUninitialized(NumQueries * NumClassesP1);
	return true;
}

void UDroneDetectionSubsystem::IngestFrame(int32 InW, int32 InH, TArray<FColor>&& InPixels)
{
	FScopeLock Lock(&FrameLock);
	LatestFrame = MoveTemp(InPixels);
	FrameW = InW;
	FrameH = InH;
	bHaveFrame = true;
}

void UDroneDetectionSubsystem::Tick(float DeltaTime)
{
	if (!bReady) { return; }

	LowSignalSecs += DeltaTime;   // time-based hold for the detection state (reset on each fresh signal)
	DetectingSecs = (DetState == EDroneDetectionState::Detecting) ? DetectingSecs + DeltaTime : 0.f;
	RejectCooldownSecs = FMath::Max(0.f, RejectCooldownSecs - DeltaTime);

	// Add the HUD once the game viewport exists (it isn't ready at BeginPlay).
	if (!HudWidget && !SlateOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

		// If a HUD Blueprint is assigned, wait for a player controller so CreateWidget has a valid owner
		// (creating against the bare world before the local player exists silently returns null). No Blueprint
		// assigned -> use the built-in Slate overlay straight away.
		if (!HudWidgetClass || PC)
		{
			if (HudWidgetClass && PC)
			{
				if (UDroneDetectionWidget* W = CreateWidget<UDroneDetectionWidget>(PC, HudWidgetClass))
				{
					W->SetSubsystem(this);
					W->AddToViewport(100);
					HudWidget = W;
				}
			}
			if (!HudWidget)   // no Blueprint assigned (or creation failed): use the built-in Slate overlay
			{
				SlateOverlay = SNew(SDroneDetectionOverlay).Subsystem(this);
				GEngine->GameViewport->AddViewportWidgetContent(SlateOverlay.ToSharedRef(), 100);
			}
			UE_LOG(LogDroneDetection, Log, TEXT("Detection HUD added to viewport (%s)."),
				HudWidget ? TEXT("widget blueprint") : TEXT("built-in overlay"));
		}
	}

	// Optional rate cap: InferenceHz <= 0 runs as fast as frames arrive (capture/inference bound).
	TimeSinceInfer += DeltaTime;
	if (Config.InferenceHz > 0.f && TimeSinceInfer < 1.f / Config.InferenceHz) { return; }

	if (!ViewExtension.IsValid()) { return; }
	TArray<FColor> Frame;
	if (!ViewExtension->FetchFrame(Frame)) { return; }   // no new captured frame yet
	IngestFrame(InputSize, InputSize, MoveTemp(Frame));

	TimeSinceInfer = 0.f;
	RunInferenceOnLatest();
}

void UDroneDetectionSubsystem::RunInferenceOnLatest()
{
	using namespace UE::NNE;

	const int32 N = InputSize * InputSize;

	// Snapshot the latest frame under the lock, then process outside it.
	TArray<FColor> Frame;
	{
		FScopeLock Lock(&FrameLock);
		if (!bHaveFrame || LatestFrame.Num() < N) { return; }
		Frame = LatestFrame;   // copy
	}

	// Preprocess: /255 -> ImageNet normalize -> CHW (RGB).
	static const float Mean[3] = { 0.485f, 0.456f, 0.406f };
	static const float Std[3]  = { 0.229f, 0.224f, 0.225f };
	float* In = InputData.GetData();
	for (int32 i = 0; i < N; ++i)
	{
		const FColor& P = Frame[i];
		In[0 * N + i] = ((P.R / 255.f) - Mean[0]) / Std[0];
		In[1 * N + i] = ((P.G / 255.f) - Mean[1]) / Std[1];
		In[2 * N + i] = ((P.B / 255.f) - Mean[2]) / Std[2];
	}

	TArray<FTensorBindingCPU> Inputs;
	Inputs.Add({ (void*)InputData.GetData(), (uint64)InputData.Num() * sizeof(float) });
	TArray<FTensorBindingCPU> Outputs;
	Outputs.SetNum(2);
	Outputs[0] = { (void*)DetsOut.GetData(),   (uint64)DetsOut.Num()   * sizeof(float) };
	Outputs[1] = { (void*)LabelsOut.GetData(), (uint64)LabelsOut.Num() * sizeof(float) };

	const double T0 = FPlatformTime::Seconds();
	if (ModelInstance->RunSync(Inputs, Outputs) != EResultStatus::Ok) { return; }
	LastInferenceMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);

	// Decode RF-DETR -> drone / no-drone: emit only when the top trained class is the drone class.
	const int32 ClassCount = FMath::Clamp(Config.ModelClassCount, 1, NumClassesP1 - 1);
	Detections.Reset();
	float RawBestScore = 0.f, RawBestCx = 0.5f, RawBestCy = 0.5f, RawBestFill = 0.f;
	for (int32 q = 0; q < NumQueries; ++q)
	{
		const float* L = &LabelsOut[q * NumClassesP1];
		int32 BestClass = 0;
		float BestLogit = -FLT_MAX;
		for (int32 c = 0; c < ClassCount; ++c)
		{
			if (L[c] > BestLogit) { BestLogit = L[c]; BestClass = c; }
		}
		if (BestClass != Config.DroneClassIndex) { continue; }
		const float Score = 1.f / (1.f + FMath::Exp(-FMath::Clamp(BestLogit, -88.f, 88.f)));

		const float* B = &DetsOut[q * 4];
		const float Cx = B[0], Cy = B[1], W = B[2], H = B[3];   // normalized cxcywh

		// Strongest drone-class score this frame, BEFORE the tracker's detect gate, so the camera + banner can
		// react to faint objects (down to the yellow level) that are still too weak to start a track.
		if (Score > RawBestScore) { RawBestScore = Score; RawBestCx = Cx; RawBestCy = Cy; RawBestFill = FMath::Max(W, H); }

		if (Score < Config.DetectThreshold) { continue; }
		FDroneDetection D;
		D.X1 = Cx - W * 0.5f; D.Y1 = Cy - H * 0.5f;
		D.X2 = Cx + W * 0.5f; D.Y2 = Cy + H * 0.5f;
		D.Score = Score;
		Detections.Add(D);
	}

	Tracker.HighThr = Config.ConfThreshold;
	Tracker.IoUMatch = Config.IoUMatch;
	Tracker.MinHits = Config.MinHits;
	Tracker.MaxAge = Config.MaxAge;
	Tracker.MotionLead = Config.MotionLead;
	Tracker.Update(Detections);

	// Pick the steadiest target for the camera: the strongest confirmed/coasting track if any, else the best
	// raw detection. The yellow/red state follows the stronger of the two scores.
	int32 Confirmed = 0;
	float TrackBestScore = 0.f; bool bHaveTrack = false; FVector2D TrackCenter(0.5f, 0.5f); float TrackFill = 0.f;
	for (const FDroneTrack& T : Tracker.Tracks)
	{
		if (!T.bConfirmed || T.TimeSinceUpdate > Config.TrackDisplayCoast) { continue; }
		++Confirmed;
		if (!bHaveTrack || T.Score > TrackBestScore)
		{
			bHaveTrack = true; TrackBestScore = T.Score;
			TrackCenter = FVector2D(0.5f * (T.DispX1 + T.DispX2), 0.5f * (T.DispY1 + T.DispY2));
			TrackFill = FMath::Max(T.DispX2 - T.DispX1, T.DispY2 - T.DispY1);
		}
	}
	UpdateDetectionState(FMath::Max(RawBestScore, TrackBestScore),
		bHaveTrack ? TrackCenter : FVector2D(RawBestCx, RawBestCy),
		bHaveTrack ? TrackFill : RawBestFill);

	if (++InferCount % 15 == 0 || Confirmed > 0)
	{
		UE_LOG(LogDroneDetection, Display, TEXT("infer #%d: raw drone dets=%d, confirmed tracks=%d, %.1f ms"),
			InferCount, Detections.Num(), Confirmed, LastInferenceMs);
	}
}

void UDroneDetectionSubsystem::UpdateDetectionState(float SignalScore, const FVector2D& Center, float Fill)
{
	const float Y = Config.ConfidenceYellowLevel;
	const float R = FMath::Max(Config.ConfidenceRedLevel, Y);   // red can't be below yellow
	const float HoldSecs = 0.6f;   // keep the lock through brief signal dips before resuming the search

	// Smooth the per-frame signal so one noisy query can't flip the banner / jerk the camera.
	StateScoreSmoothed = 0.6f * StateScoreSmoothed + 0.4f * SignalScore;

	if (SignalScore >= Y)
	{
		LowSignalSecs = 0.f;
		const FVector2D Clamped(FMath::Clamp(Center.X, 0.f, 1.f), FMath::Clamp(Center.Y, 0.f, 1.f));
		StateTargetCenter = FMath::Lerp(StateTargetCenter, Clamped, 0.5f);
		StateTargetFill = FMath::Lerp(StateTargetFill, Fill, 0.5f);
	}

	const float S = StateScoreSmoothed;
	const bool bCooling = RejectCooldownSecs > 0.f;
	const bool bInvestigateExpired = (DetState == EDroneDetectionState::Detecting) && (DetectingSecs > Config.InvestigateTimeout);

	EDroneDetectionState Next = DetState;
	if (S >= R)
	{
		Next = EDroneDetectionState::Detected;        // confident drone: engage even mid-cooldown
	}
	else if (S >= Y && !bCooling && !bInvestigateExpired)
	{
		Next = EDroneDetectionState::Detecting;       // weak candidate: investigate (unless cooling / already gave up)
	}
	else
	{
		// Not confident enough, or blocked by the cooldown / investigate-timeout. Drop to Searching when the
		// signal has been lost long enough, or immediately if we're cooling / have given up investigating;
		// otherwise coast through a brief dip.
		const bool bDrop = (LowSignalSecs > HoldSecs) || bCooling || bInvestigateExpired;
		Next = bDrop ? EDroneDetectionState::Searching : DetState;
	}

	// False-alarm cooldown bookkeeping: if an investigation ends without ever reaching red, it wasn't a drone —
	// suppress weak re-triggers for a while so the search sweep moves past it (a confident drone clears this).
	if (Next == EDroneDetectionState::Detected) { bReachedRedThisEpisode = true; RejectCooldownSecs = 0.f; }
	if (DetState == EDroneDetectionState::Searching && Next == EDroneDetectionState::Detecting) { bReachedRedThisEpisode = false; }
	if (DetState != EDroneDetectionState::Searching && Next == EDroneDetectionState::Searching && !bReachedRedThisEpisode)
	{
		RejectCooldownSecs = Config.RejectCooldown;
	}
	DetState = Next;
}

FString UDroneDetectionSubsystem::GetBannerText() const
{
	switch (DetState)
	{
	case EDroneDetectionState::Detecting: return TEXT("Detecting object...");
	case EDroneDetectionState::Detected:  return TEXT("Drone detected!");
	default:
	{
		const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const int32 Dots = (int32)T % 3 + 1;   // 1..3 dots, one per second, to show it's working
		return FString(TEXT("Searching for drones")) + FString::ChrN(Dots, TEXT('.'));
	}
	}
}

FLinearColor UDroneDetectionSubsystem::GetBannerColor() const
{
	switch (DetState)
	{
	case EDroneDetectionState::Detecting: return FLinearColor(1.f, 0.85f, 0.1f);    // yellow
	case EDroneDetectionState::Detected:  return FLinearColor(1.f, 0.12f, 0.12f);   // red
	default:                              return FLinearColor::White;
	}
}

void UDroneDetectionSubsystem::SetupVideoPostProcess()
{
	bVideoActive = false;
	UWorld* W = GetWorld();
	if (!W) { return; }

	// Find a Post Process Volume whose blendable material is M_PostProcessVideoPlayer.
	for (TActorIterator<APostProcessVolume> It(W); It; ++It)
	{
		APostProcessVolume* PPV = *It;
		for (FWeightedBlendable& WB : PPV->Settings.WeightedBlendables.Array)
		{
			UMaterialInterface* Mat = Cast<UMaterialInterface>(WB.Object);
			if (!Mat) { continue; }
			const UMaterialInterface* Base = Mat->GetBaseMaterial();
			if (!Base || Base->GetName() != TEXT("M_PostProcessVideoPlayer")) { continue; }

			// Decode the clip into a MediaTexture and feed it to the material's "VideoFrame" param so the
			// post-process draws the video full-screen — captured by the SVE like the live scene.
			MediaPlayer = NewObject<UMediaPlayer>(this);
			MediaPlayer->PlayOnOpen = true;
			MediaPlayer->SetLooping(true);
			MediaTexture = NewObject<UMediaTexture>(this);
			MediaTexture->SetMediaPlayer(MediaPlayer);
			MediaTexture->SRGB = false;   // M_PostProcessVideoPlayer samples VideoFrame as LinearColor (not sRGB)
			MediaTexture->UpdateResource();

			VideoMID = UMaterialInstanceDynamic::Create(Mat, this);
			VideoMID->SetTextureParameterValue(TEXT("VideoFrame"), MediaTexture);
			WB.Object = VideoMID;
			bVideoActive = true;

			const TArray<FString> Vids = GetAvailableVideos();
			if (Vids.Num() > 0) { OpenVideo(Vids[0]); }
			UE_LOG(LogDroneDetection, Log, TEXT("Video post-process active (M_PostProcessVideoPlayer) on %s."), *PPV->GetName());
			return;
		}
	}
	UE_LOG(LogDroneDetection, Log, TEXT("No M_PostProcessVideoPlayer post-process volume found; video disabled, live scene only."));
}

void UDroneDetectionSubsystem::OpenVideo(const FString& PathOrName)
{
	if (!MediaPlayer || PathOrName.IsEmpty()) { return; }
	VideoFile = PathOrName;
	FString Resolved = PathOrName;
	if (FPaths::IsRelative(Resolved))
	{
		Resolved = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Movies"), PathOrName);
	}
	Resolved = FPaths::ConvertRelativePathToFull(Resolved);
	if (!MediaSource) { MediaSource = NewObject<UFileMediaSource>(this); }
	MediaSource->SetFilePath(Resolved);
	MediaPlayer->OpenSource(MediaSource);
}

TArray<FString> UDroneDetectionSubsystem::GetAvailableVideos() const
{
	TArray<FString> Out;
	const FString Pattern = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Movies"), TEXT("*.mp4"));
	IFileManager::Get().FindFiles(Out, *Pattern, true, false);
	Out.Sort();
	return Out;
}

void UDroneDetectionSubsystem::SwitchVideo(const FString& InFile)
{
	if (!bVideoActive) { return; }
	OpenVideo(InFile);
	UE_LOG(LogDroneDetection, Log, TEXT("Switched video to %s"), *InFile);
}

