#include "DronePawn.h"
#include "DroneMovementComponent.h"
#include "Input/DroneInputShaping.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"

ADronePawn::ADronePawn()
{
	PrimaryActorTick.bCanEverTick = false;

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

	DroneMovement = CreateDefaultSubobject<UDroneMovementComponent>(TEXT("DroneMovement"));

	// A placed drone is flyable on Play without a GameMode by auto-possessing the first local player.
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ADronePawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Move the collision body; the integrator's swept moves act on the pawn root.
	if (DroneMovement)
	{
		DroneMovement->SetUpdatedComponent(CollisionRoot);
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

void ADronePawn::PushControlIntent()
{
	if (!DroneMovement)
	{
		return;
	}

	FDroneControlIntent Intent = DroneInput::MapMode2(
		ThrottleYawStick.X, ThrottleYawStick.Y, PitchRollStick.X, PitchRollStick.Y);

	// Throttle passes through raw; the rotational sticks get deadzone + expo for a calmer center.
	Intent.Yaw = DroneInput::ShapeAxis(Intent.Yaw, StickDeadzone, StickExpo);
	Intent.Pitch = DroneInput::ShapeAxis(Intent.Pitch, StickDeadzone, StickExpo);
	Intent.Roll = DroneInput::ShapeAxis(Intent.Roll, StickDeadzone, StickExpo);

	// Carry the latched hover toggle alongside the sticks, since SetControlIntent replaces the whole
	// intent each push.
	Intent.bHoverEngaged = bHoverEngaged;

	DroneMovement->SetControlIntent(Intent);
}
