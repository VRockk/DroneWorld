#include "Detection/DroneDetector.h"
#include "Detection/DroneDetectionWidget.h"
#include "CineCameraComponent.h"
#include "Components/ChildActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

namespace
{
	// Critically-damped implicit spring: eases X toward Target with natural ease-in and ease-out, no overshoot,
	// unconditionally stable. Freq is the spring rate (higher = faster). Carries V (velocity) between frames so
	// motion accelerates from rest and decelerates into the target like a real motorized camera mount.
	void Spring(float& X, float& V, float Target, float Dt, float Freq)
	{
		if (Dt <= 0.f) { return; }
		Dt = FMath::Min(Dt, 0.1f);   // clamp big frame hitches so the step stays well-behaved
		const float Omega = FMath::Max(0.01f, Freq);
		const float F = 1.f + 2.f * Dt * Omega;
		const float OO = Omega * Omega;
		const float HOO = Dt * OO;
		const float HHOO = Dt * HOO;
		const float Det = 1.f / (F + HHOO);
		const float NewX = (F * X + Dt * V + HHOO * Target) * Det;
		const float NewV = (V + HOO * (Target - X)) * Det;
		X = NewX; V = NewV;
	}

	// Same, but takes the shortest way around for an angle in degrees (handles 180/-180 wrap).
	void SpringAngle(float& X, float& V, float Target, float Dt, float Freq)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(X, Target);
		Spring(X, V, X + Delta, Dt, Freq);
	}
}

ADroneDetector::ADroneDetector()
{
	PrimaryActorTick.bCanEverTick = true;
}

UDroneDetectionSubsystem* ADroneDetector::Sub() const
{
	UWorld* W = GetWorld();
	return W ? W->GetSubsystem<UDroneDetectionSubsystem>() : nullptr;
}

void ADroneDetector::BeginPlay()
{
	Super::BeginPlay();
	if (UDroneDetectionSubsystem* S = Sub())
	{
		S->StartDetection(Model, Config, HudWidgetClass);
	}

	// Adopt the Cine Camera you added to BP_DroneDetection as the view, and remember its start pose as "home".
	// Works whether you add a Cine Camera *component* directly, or a Cine Camera *actor* via a Child Actor component.
	CineCamera = FindComponentByClass<UCineCameraComponent>();
	if (!CineCamera)
	{
		TArray<UChildActorComponent*> ChildActorComps;
		GetComponents(ChildActorComps);
		for (UChildActorComponent* CAC : ChildActorComps)
		{
			if (AActor* Child = CAC ? CAC->GetChildActor() : nullptr)
			{
				if (UCineCameraComponent* CC = Child->FindComponentByClass<UCineCameraComponent>()) { CineCamera = CC; break; }
			}
		}
	}
	if (CineCamera)
	{
		CineCamera->SetActive(true);
		BaseRot = CineCamera->GetComponentRotation();
		CamYaw = BaseRot.Yaw;
		CamPitch = BaseRot.Pitch;
		CurFocal = WideFocalLength;
		CineCamera->SetCurrentFocalLength(CurFocal);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ADroneDetector: no CineCameraComponent on this actor — add one to BP_DroneDetection to enable the dynamic camera."));
	}
}

void ADroneDetector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDroneDetectionSubsystem* S = Sub())
	{
		S->StopDetection();
	}
	Super::EndPlay(Reason);
}

void ADroneDetector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!CineCamera) { return; }

	// Make our Cine Camera the active view once a player controller exists (it usually isn't ready at BeginPlay).
	if (!bViewTargetSet)
	{
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			// Target the actor that actually owns the camera (this, or the child Cine Camera actor).
			AActor* CamOwner = CineCamera->GetOwner() ? CineCamera->GetOwner() : this;
			PC->SetViewTargetWithBlend(CamOwner, 0.f);
			bViewTargetSet = true;
		}
	}

	UpdateCamera(DeltaSeconds);
}

void ADroneDetector::UpdateCamera(float Dt)
{
	UDroneDetectionSubsystem* S = Sub();
	const EDroneDetectionState St = (S && S->IsReady()) ? S->GetDetectionState() : EDroneDetectionState::Searching;

	// On losing the target, recenter the sweep so the view eases back home, then resumes panning.
	if (St == EDroneDetectionState::Searching && PrevState != EDroneDetectionState::Searching) { SearchPhase = 0.f; }
	PrevState = St;

	float TargetYaw = BaseRot.Yaw;
	float TargetPitch = BaseRot.Pitch;
	float TargetFocal = WideFocalLength;
	float RotFreq = TrackSpeed;

	if (St == EDroneDetectionState::Searching)
	{
		// Slow left/right pan; sin() gives natural ease-in / ease-out at the turnarounds. Zoom out to wide.
		SearchPhase += Dt * (2.f * PI / FMath::Max(0.5f, SearchPeriod));
		TargetYaw = BaseRot.Yaw + SearchYawRange * FMath::Sin(SearchPhase);
		TargetPitch = BaseRot.Pitch;
		TargetFocal = WideFocalLength;
	}
	else
	{
		// Closed-loop centering: convert the target's screen offset to a pan/tilt error using the live FOV.
		const FVector2D C = S->GetTargetCenterNorm();
		const float OffX = C.X - 0.5f;   // + = right of center
		const float OffY = C.Y - 0.5f;   // + = below center

		const float SensorW = FMath::Max(1.f, CineCamera->Filmback.SensorWidth);
		const float SensorH = FMath::Max(1.f, CineCamera->Filmback.SensorHeight);
		const float Focal = FMath::Max(1.f, CurFocal);
		const float HFov = FMath::RadiansToDegrees(2.f * FMath::Atan(SensorW / (2.f * Focal)));
		const float VFov = FMath::RadiansToDegrees(2.f * FMath::Atan(SensorH / (2.f * Focal)));

		TargetYaw = CamYaw + OffX * HFov;
		TargetPitch = CamPitch - OffY * VFov;   // screen-down => tilt down

		// Fixed, bounded zoom per state: moderate while investigating (yellow), a bit tighter once confirmed (red).
		// No fill-chasing — that drove distant drones straight to the max-focal cap every time.
		TargetFocal = (St == EDroneDetectionState::Detected) ? DetectedFocalLength : DetectingFocalLength;
		RotFreq = (St == EDroneDetectionState::Detected) ? TrackSpeed * 1.6f : TrackSpeed;
	}

	SpringAngle(CamYaw, YawVel, TargetYaw, Dt, RotFreq);
	SpringAngle(CamPitch, PitchVel, TargetPitch, Dt, RotFreq);
	Spring(CurFocal, FocalVel, TargetFocal, Dt, ZoomSpeed);
	CurFocal = FMath::Clamp(CurFocal, WideFocalLength, MaxFocalLength);
	CamPitch = FMath::Clamp(CamPitch, BaseRot.Pitch - 80.f, BaseRot.Pitch + 80.f);   // avoid flipping over the poles

	CineCamera->SetWorldRotation(FRotator(CamPitch, CamYaw, BaseRot.Roll));
	CineCamera->SetCurrentFocalLength(CurFocal);
}

void ADroneDetector::PushConfig()
{
	if (UDroneDetectionSubsystem* S = Sub()) { S->SetConfig(Config); }
}

void ADroneDetector::SetConfThreshold(float V)     { Config.ConfThreshold = FMath::Clamp(V, 0.f, 1.f); PushConfig(); }
void ADroneDetector::SetDetectThreshold(float V)   { Config.DetectThreshold = FMath::Clamp(V, 0.f, 1.f); PushConfig(); }
void ADroneDetector::SetInferenceHz(float V)       { Config.InferenceHz = FMath::Clamp(V, 0.f, 120.f); PushConfig(); }
void ADroneDetector::SetIoUMatch(float V)          { Config.IoUMatch = FMath::Clamp(V, 0.f, 1.f); PushConfig(); }
void ADroneDetector::SetMinHits(int32 V)           { Config.MinHits = FMath::Max(1, V); PushConfig(); }
void ADroneDetector::SetMaxAge(int32 V)            { Config.MaxAge = FMath::Max(1, V); PushConfig(); }
void ADroneDetector::SetTrackDisplayCoast(int32 V) { Config.TrackDisplayCoast = FMath::Max(0, V); PushConfig(); }
