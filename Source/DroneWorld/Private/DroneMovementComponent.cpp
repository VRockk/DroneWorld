#include "DroneMovementComponent.h"
#include "DronePreset.h"
#include "Flight/DroneFlightModel.h"
#include "Flight/QuadFlightModel.h"
#include "Flight/DroneAssist.h"
#include "Flight/DroneIntegrator.h"

UDroneMovementComponent::UDroneMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// A quadcopter with reasonable defaults so a freshly placed drone flies without further setup.
	FlightModel = CreateDefaultSubobject<UQuadFlightModel>(TEXT("FlightModel"));
}

void UDroneMovementComponent::ApplyPreset(const UDronePreset* InPreset)
{
	if (!InPreset || !InPreset->FlightModel)
	{
		return;
	}

	// Copy the preset's model into this component so each drone owns and tunes its own instance,
	// rather than sharing the asset's subobject across every drone built from the same preset.
	FlightModel = DuplicateObject<UDroneFlightModel>(InPreset->FlightModel, this);

	// Adopt the preset's assist behavior so the drone flies in its chosen mode with its leveling tuning.
	BaseAssistMode = InPreset->BaseAssistMode;
	LevelingStrength = InPreset->LevelingStrength;
}

void UDroneMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Preset)
	{
		ApplyPreset(Preset);
	}
}

void UDroneMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!UpdatedComponent || !FlightModel || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	// Read the current motion state from the component being moved.
	FDroneFlightState State;
	State.Location = UpdatedComponent->GetComponentLocation();
	State.Orientation = UpdatedComponent->GetComponentRotation();
	State.Velocity = Velocity;
	State.AngularVelocity = AngularVelocity;

	// Flight Model -> integrator: turn intent and state into the next state. The hover toggle overrides
	// the base assist mode while engaged; otherwise the base mode (Acro/Angle) flies.
	FDroneForces Forces;
	if (CurrentIntent.bHoverEngaged)
	{
		// Capture the hold point on the rising edge so hover parks where it was engaged, not where the
		// drone has since drifted. The Flight Model decides what holding means for its airframe.
		if (!bWasHovering)
		{
			HoverHoldLocation = State.Location;
			bWasHovering = true;
		}
		Forces = FlightModel->ComputeHoverForces(CurrentIntent, State, HoverHoldLocation);
	}
	else
	{
		bWasHovering = false;
		Forces = FlightModel->ComputeForces(CurrentIntent, State);

		// Angle mode adds a self-leveling torque on top of the pilot's command; Acro adds nothing.
		Forces.Torque += DroneFlight::ComputeLevelingTorque(BaseAssistMode, CurrentIntent, State, LevelingStrength);
	}

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(State, Forces, FlightModel->GetMass(), DeltaTime);

	// Apply with swept collision so world geometry blocks the drone; slide along blocking surfaces.
	const FVector Delta = Next.Location - State.Location;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, Next.Orientation.Quaternion(), /*bSweep*/ true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, /*bHandleImpact*/ true);
	}

	// Recover linear velocity from the distance actually travelled, so a blocked move sheds the
	// velocity it could not realize. Angular velocity carries forward unimpeded.
	Velocity = (UpdatedComponent->GetComponentLocation() - State.Location) / DeltaTime;
	AngularVelocity = Next.AngularVelocity;
	UpdateComponentVelocity();
}
