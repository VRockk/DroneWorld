#include "DronePawn.h"
#include "DroneMovementComponent.h"
#include "Input/DroneInputShaping.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"

ADronePawn::ADronePawn()
{
	// Tick to slew the gimbal tilt: a held D-pad press integrates into the pitch each frame.
	PrimaryActorTick.bCanEverTick = true;

	CollisionRoot = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionRoot"));
	CollisionRoot->InitSphereRadius(20.f);
	CollisionRoot->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = CollisionRoot;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(CollisionRoot);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GimbalMount = CreateDefaultSubobject<USceneComponent>(TEXT("GimbalMount"));
	GimbalMount->SetupAttachment(CollisionRoot);

	OnboardCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("OnboardCamera"));
	OnboardCamera->SetupAttachment(GimbalMount);

	FeedCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("FeedCapture"));
	FeedCapture->SetupAttachment(GimbalMount);

	FeedCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDRNoAlpha;

	// Stay idle until a controller assigns a render target,
	// so a flatscreen or AI drone pays nothing for capture.
	FeedCapture->bCaptureEveryFrame = false;
	FeedCapture->bCaptureOnMovement = false;

	// Persist the capture's rendering state so Temporal AA and motion vectors accumulate across frames.
	// Without it the Feed shimmers and aliases instead of reading like the onboard camera's own image.
	FeedCapture->bAlwaysPersistRenderingState = true;

	DroneMovement = CreateDefaultSubobject<UDroneMovementComponent>(TEXT("DroneMovement"));

	// One looping rotor sound per corner, offset out to the airframe so spatialized audio pans the whine
	// between them and banking swings it across the drone. The spacing here is a small-quad default; a
	// larger drone widens it in Blueprint. The looping sound and attenuation are assigned per pawn. They
	// auto-activate so the loop is always running; the per-tick volume (zero while disarmed) is what makes
	// them heard or silent, rather than starting and stopping the voices.
	const float CornerSpacing = 15.f;
	auto MakeRotorAudio = [this](const TCHAR* Name, const FVector& Offset) -> UAudioComponent*
	{
		UAudioComponent* Audio = CreateDefaultSubobject<UAudioComponent>(Name);
		Audio->SetupAttachment(CollisionRoot);
		Audio->SetRelativeLocation(Offset);
		Audio->bAutoActivate = true;
		Audio->VolumeMultiplier = 0.f;   // silent until the first tick voices it, so a disarmed spawn makes no sound
		return Audio;
	};
	RotorAudioFrontLeft = MakeRotorAudio(TEXT("RotorAudioFrontLeft"), FVector(CornerSpacing, -CornerSpacing, 0.f));
	RotorAudioFrontRight = MakeRotorAudio(TEXT("RotorAudioFrontRight"), FVector(CornerSpacing, CornerSpacing, 0.f));
	RotorAudioRearLeft = MakeRotorAudio(TEXT("RotorAudioRearLeft"), FVector(-CornerSpacing, -CornerSpacing, 0.f));
	RotorAudioRearRight = MakeRotorAudio(TEXT("RotorAudioRearRight"), FVector(-CornerSpacing, CornerSpacing, 0.f));

	// A placed drone is flyable on Play without a GameMode by auto-possessing the first local player.
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ADronePawn::BeginPlay()
{
	Super::BeginPlay();

	// The movement component applied its preset during Super (component BeginPlay runs first), so the
	// gimbal config is in place: seed the tilt at its default and point the camera there before the pilot
	// touches the D-pad.
	if (DroneMovement)
	{
		const FGimbalConfig& Config = DroneMovement->Gimbal;
		GimbalTiltDegrees = FMath::Clamp(Config.DefaultTiltDegrees, Config.MinTiltDegrees, Config.MaxTiltDegrees);

		// A crash must disarm the drone, so subscribe to the movement component's crash event and cut the
		// arm state when it fires (see OnDroneCrashed).
		DroneMovement->OnCrashed.AddDynamic(this, &ADronePawn::OnDroneCrashed);
	}
	UpdateGimbal(0.f);
}

void ADronePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateGimbal(DeltaSeconds);
	UpdateMotorAudio();
}

void ADronePawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Move the collision body; the integrator's swept moves act on the pawn root.
	if (DroneMovement)
	{
		DroneMovement->SetUpdatedComponent(CollisionRoot);
	}

	// The Feed must show the onboard camera's image, not a separately-tuned second render: share its
	// field of view and post-processing. The capture already shares the gimbal mount, so it shares the
	// transform too, and the captured render target becomes the camera's image fed to the screen.
	if (FeedCapture && OnboardCamera)
	{
		FeedCapture->FOVAngle = OnboardCamera->FieldOfView;
		FeedCapture->PostProcessSettings = OnboardCamera->PostProcessSettings;
		FeedCapture->PostProcessBlendWeight = OnboardCamera->PostProcessBlendWeight;
	}
}

void ADronePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (FlightMappingContext)
			{
				Subsystem->AddMappingContext(FlightMappingContext, 0);
			}
		}
	}

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Bind both Triggered and Completed so releasing a stick pushes its zeroed value through.
		if (ThrottleYawAction)
		{
			Input->BindAction(ThrottleYawAction, ETriggerEvent::Triggered, this, &ADronePawn::OnThrottleYaw);
			Input->BindAction(ThrottleYawAction, ETriggerEvent::Completed, this, &ADronePawn::OnThrottleYaw);
		}
		if (PitchRollAction)
		{
			Input->BindAction(PitchRollAction, ETriggerEvent::Triggered, this, &ADronePawn::OnPitchRoll);
			Input->BindAction(PitchRollAction, ETriggerEvent::Completed, this, &ADronePawn::OnPitchRoll);
		}
		// Started fires once per press, so each tap flips the hover toggle rather than spamming it.
		if (HoverToggleAction)
		{
			Input->BindAction(HoverToggleAction, ETriggerEvent::Started, this, &ADronePawn::OnToggleHover);
		}
		// Same as hover: Started fires once per press, so each tap flips the arm state rather than spamming it.
		if (ArmToggleAction)
		{
			Input->BindAction(ArmToggleAction, ETriggerEvent::Started, this, &ADronePawn::OnToggleArm);
		}
		// Triggered keeps the held direction set while the D-pad is down; Completed clears it on
		// release. The tick integrates that direction into the tilt, so holding sweeps the camera.
		if (GimbalTiltAction)
		{
			Input->BindAction(GimbalTiltAction, ETriggerEvent::Triggered, this, &ADronePawn::OnGimbalTilt);
			Input->BindAction(GimbalTiltAction, ETriggerEvent::Completed, this, &ADronePawn::OnGimbalTilt);
		}
	}
}

void ADronePawn::OnThrottleYaw(const FInputActionValue& Value)
{
	ThrottleYawStick = Value.Get<FVector2D>();
	PushControlIntent();
}

void ADronePawn::OnPitchRoll(const FInputActionValue& Value)
{
	PitchRollStick = Value.Get<FVector2D>();
	PushControlIntent();
}

void ADronePawn::OnToggleHover(const FInputActionValue& Value)
{
	bHoverEngaged = !bHoverEngaged;
	PushControlIntent();
}

void ADronePawn::OnToggleArm(const FInputActionValue& Value)
{
	bArmed = !bArmed;
	PushControlIntent();
}

void ADronePawn::OnDroneCrashed(APawn* CrashedPawn)
{
	// The crash already cut the motors; latch it into the arm state so the drone stays disarmed through
	// the respawn. The pilot must deliberately re-arm before the recovered drone flies again, rather than
	// it springing back to life on the first stick nudge.
	bArmed = false;
	PushControlIntent();
}

void ADronePawn::OnGimbalTilt(const FInputActionValue& Value)
{
	// Record only the held direction; the tick integrates it into the tilt so a held press slews the
	// camera rather than snapping it to a stop.
	GimbalTiltDirection = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

float ADronePawn::StepGimbalTilt(const FGimbalConfig& Config, float CurrentDegrees, float Direction, float HeldSeconds, float DeltaSeconds)
{
	const float Dir = FMath::Clamp(Direction, -1.f, 1.f);
	if (FMath::IsNearlyZero(Dir))
	{
		// No key held: the tilt holds wherever the pilot left it.
		return FMath::Clamp(CurrentDegrees, Config.MinTiltDegrees, Config.MaxTiltDegrees);
	}

	// The slew rate starts at the base rate and ramps up the longer the key is held, capped so it does
	// not run away, so a tap nudges precisely while a long hold sweeps the range.
	const float Rate = FMath::Min(
		Config.TiltSlewRate + Config.TiltSlewAcceleration * FMath::Max(HeldSeconds, 0.f),
		Config.TiltMaxSlewRate);

	const float Next = CurrentDegrees + Dir * Rate * DeltaSeconds;
	return FMath::Clamp(Next, Config.MinTiltDegrees, Config.MaxTiltDegrees);
}

void ADronePawn::UpdateGimbal(float DeltaSeconds)
{
	if (!GimbalMount || !DroneMovement)
	{
		return;
	}

	// Accumulate hold time while a direction is held so the slew accelerates; reset it when the key is
	// released or the direction reverses, so each fresh press starts slow again.
	if (FMath::IsNearlyZero(GimbalTiltDirection))
	{
		GimbalHeldSeconds = 0.f;
	}
	else
	{
		if (GimbalTiltDirection * GimbalLastTiltDirection < 0.f)
		{
			GimbalHeldSeconds = 0.f;
		}

		// The gimbal config lives on the movement component (copied from the preset). The mount is fixed
		// to the airframe, so only its pitch is driven; the camera banks and bobs with the drone.
		GimbalTiltDegrees = StepGimbalTilt(
			DroneMovement->Gimbal, GimbalTiltDegrees, GimbalTiltDirection, GimbalHeldSeconds, DeltaSeconds);
		GimbalHeldSeconds += DeltaSeconds;
	}

	GimbalLastTiltDirection = GimbalTiltDirection;
	GimbalMount->SetRelativeRotation(FRotator(GimbalTiltDegrees, 0.f, 0.f));
}

void ADronePawn::PushControlIntent()
{
	if (!DroneMovement)
	{
		return;
	}

	FDroneControlIntent Intent = DroneInput::MapMode2(
		ThrottleYawStick.X, ThrottleYawStick.Y, PitchRollStick.X, PitchRollStick.Y);

	// Throttle passes through raw; the rotational sticks get this drone's rates - deadzone + expo for a
	// calmer center, scaled by the preset's sensitivity - so each drone responds with its own character.
	const FDroneRates& Rates = DroneMovement->Rates;
	Intent.Yaw = DroneInput::ShapeAxis(Intent.Yaw, Rates);
	Intent.Pitch = DroneInput::ShapeAxis(Intent.Pitch, Rates);
	Intent.Roll = DroneInput::ShapeAxis(Intent.Roll, Rates);

	// Carry the latched mode toggles alongside the sticks, since SetControlIntent replaces the whole
	// intent each push: hover overrides the base assist mode, and the arm gate decides whether the motors
	// respond to any of this at all.
	Intent.bHoverEngaged = bHoverEngaged;
	Intent.bArmed = bArmed;

	// Keep the pushed intent so the motor audio can read the live attitude demand and arm state each tick.
	LastIntent = Intent;

	DroneMovement->SetControlIntent(Intent);
}

void ADronePawn::UpdateMotorAudio()
{
	if (!DroneMovement)
	{
		return;
	}

	// Mix the realized (motor-lagged) throttle across the four corners by the live attitude demand, so the
	// sound spools up with the thrust and banking swings it between the rotors. Then voice each rotor from
	// its own level - silenced while disarmed - and set the volume and pitch on its looping sound.
	const FQuadRotorLevels Levels = DroneFlight::ComputeQuadMotorMix(
		DroneMovement->GetRealizedThrottle(), LastIntent.Roll, LastIntent.Pitch, LastIntent.Yaw, MotorAudio.RotorMixGain);

	const TPair<UAudioComponent*, float> Rotors[] = {
		{ RotorAudioFrontLeft, Levels.FrontLeft },
		{ RotorAudioFrontRight, Levels.FrontRight },
		{ RotorAudioRearLeft, Levels.RearLeft },
		{ RotorAudioRearRight, Levels.RearRight },
	};

	for (const TPair<UAudioComponent*, float>& Rotor : Rotors)
	{
		if (!Rotor.Key)
		{
			continue;
		}

		const FMotorAudioState Audio = DroneFlight::ComputeMotorAudio(MotorAudio, Rotor.Value, LastIntent.bArmed);
		Rotor.Key->SetVolumeMultiplier(Audio.Volume);
		Rotor.Key->SetPitchMultiplier(Audio.Pitch);
	}
}
