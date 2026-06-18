#include "DroneMovementComponent.h"
#include "DronePreset.h"
#include "DroneWindSubsystem.h"
#include "Flight/DroneFlightModel.h"
#include "Flight/QuadFlightModel.h"
#include "Flight/DroneAssist.h"
#include "Flight/DroneIntegrator.h"
#include "Flight/DroneImperfection.h"
#include "Flight/DroneCrash.h"
#include "Flight/DroneFriction.h"
#include "Input/DroneInputShaping.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

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

	// Adopt the preset's imperfection tuning so this drone bobs, drifts, feels the wind, and spools its
	// motors to its own character.
	Imperfection = InPreset->Imperfection;

	// Adopt the preset's gimbal config so the onboard camera tilts within this drone's range and
	// stabilizes (or not) to the designer's chosen camera feel.
	Gimbal = InPreset->Gimbal;

	// Adopt the preset's rates so the pawn shapes raw sticks into Control Intent with this drone's feel -
	// its deadzone, expo, and overall sensitivity.
	Rates = InPreset->Rates;

	// Adopt the preset's crash toughness so a flimsy drone breaks on a knock a rugged one shrugs off,
	// and how much momentum the wreck keeps off the impact.
	CrashThreshold = InPreset->CrashThreshold;
	CrashMomentumRetention = InPreset->CrashMomentumRetention;

	// Adopt the preset's ground friction so a disarmed or crashed drone scrubs to a stop on the ground
	// with this drone's character rather than sliding on the velocity it carried in.
	GroundFrictionDeceleration = InPreset->GroundFrictionDeceleration;

	// Adopt the preset's ground settling so a disarmed or crashed drone lies flat against the slope it
	// rests on at this drone's settling rate.
	GroundSettleStrength = InPreset->GroundSettleStrength;
}

void UDroneMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// Seed the bob and drift from this instance so drones placed together do not wander in lockstep.
	ImperfectionSeed = (int32)GetUniqueID();

	if (Preset)
	{
		ApplyPreset(Preset);
	}
	else
	{
		// The Preset is the single source of this drone's tuning, so a missing one means the drone flies
		// on bare defaults rather than its intended character. Warn loudly rather than failing silently.
		UE_LOG(LogTemp, Warning, TEXT("DroneMovementComponent on %s has no Preset set; flying on default tuning."),
			*GetNameSafe(GetOwner()));
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

	// Gate the command by the arm state first: a disarmed drone produces no motor command at all, so the
	// throttle spools down to nothing and no rotation is commanded. The motors only respond once armed.
	const FDroneControlIntent Commanded = DroneInput::GateMotors(CurrentIntent);

	// Spool the realized throttle toward the (gated) command so thrust ramps in rather than snapping, then
	// fly the flight model on the lagged throttle. The stick demand on the other axes is unaffected.
	SpooledThrottle = DroneFlight::StepMotorLag(SpooledThrottle, Commanded.Throttle, Imperfection.MotorLagTau, DeltaTime);
	FDroneControlIntent Intent = Commanded;
	Intent.Throttle = SpooledThrottle;

	// Advance the imperfection clock once per tick so the hover wander and the additive perturbation
	// below read the same moment.
	ImperfectionTime += DeltaTime;

	// Probe for the ground once this tick: a motorless drone uses it both to settle flat against the slope
	// (a torque added below) and to scrub off its slide (after the move). Probed before integrating so the
	// settle torque can act this tick.
	FVector GroundNormal;
	bGrounded = FindGroundContact(GroundNormal);

	// Flight Model -> integrator: turn intent and state into the next state. A crashed or disarmed drone
	// is dead to the sticks; otherwise the hover toggle overrides the base assist mode while engaged, and
	// failing that the base mode (Acro/Angle) flies.
	FDroneForces Forces;
	if (bCrashed || !Intent.bArmed)
	{
		// Cut to a null command so only the flight model's gravity and drag remain - no thrust, no
		// commanded torque, no self-leveling, and no hover hold - so the motors have no effect at all. A
		// crashed drone falls with the tumble spin EnterCrash imparted; a disarmed one simply sits or
		// falls until the pilot arms. The Imperfection Layer is skipped: neither is actively flying. The
		// ground-settle torque is added below, after the branch, since it lays the drone down whatever its
		// motor state.
		Forces = FlightModel->ComputeForces(FDroneControlIntent(), State);
	}
	else
	{
		if (Intent.bHoverEngaged)
		{
			// Capture the hold point on the rising edge so hover parks where it was engaged, not where the
			// drone has since drifted. The Flight Model decides what holding means for its airframe.
			if (!bWasHovering)
			{
				HoverHoldLocation = State.Location;
				bWasHovering = true;
			}

			// Wander the hold point with the coherent-noise drift so a hovering drone gently circles its
			// spot rather than freezing rigidly on the setpoint. The position-hold spring tracks this moving
			// target instead of fighting a drift force, so horizontal drift is applied here rather than as a
			// force in the Imperfection Layer below.
			// A grounded drone holds its spot exactly: the surface, not the air, governs it, so the
			// hold-point drift is switched off rather than nudging it across the floor.
			const FVector HoverDrift = bGrounded ? FVector::ZeroVector : DroneFlight::ComputeDriftOffset(Imperfection, ImperfectionSeed, ImperfectionTime);
			const FVector DriftingHold = HoverHoldLocation + HoverDrift;
			Forces = FlightModel->ComputeHoverForces(Intent, State, DriftingHold);
		}
		else
		{
			bWasHovering = false;
			Forces = FlightModel->ComputeForces(Intent, State);

			// Angle mode adds a self-leveling torque on top of the pilot's command; Acro adds nothing.
			// Self-leveling drives toward world level, which on a slope would fight the resting attitude, so
			// while grounded it yields to the ground-settle torque below: the drone lies along the surface it
			// rests on rather than holding itself world-level against it.
			if (!bGrounded)
			{
				Forces.Torque += DroneFlight::ComputeLevelingTorque(BaseAssistMode, Intent, State, LevelingStrength);
			}
		}

		// The Imperfection Layer perturbs the ideal force and torque after the flight model, uniformly
		// across models: hover bob, coherent-noise drift (in free flight; while hovering it instead wanders
		// the hold point above), the world wind scaled by this drone's susceptibility, and a slight
		// roll/pitch wobble. The world supplies the wind; a level with no wind subsystem simply feels none.
		FVector WorldWind = FVector::ZeroVector;
		if (const UWorld* World = GetWorld())
		{
			if (const UDroneWindSubsystem* WindSubsystem = World->GetSubsystem<UDroneWindSubsystem>())
			{
				WorldWind = WindSubsystem->GetCurrentWind();
			}
		}
		const FDroneForces Imperfect = DroneFlight::ComputeImperfectionForces(Imperfection, ImperfectionSeed, ImperfectionTime, WorldWind, Intent.bHoverEngaged, bGrounded);
		Forces.Force += Imperfect.Force;
		Forces.Torque += Imperfect.Torque;
	}

	// Resting on the ground, settle flat against the slope: a critically damped torque aligns the drone's
	// up axis with the ground normal, or the anti-normal if it landed inverted, so a flipped drone stays
	// flipped. This lays the drone down whatever its motor state - disarmed, crashed, or armed and parked
	// on the floor - and replaces self-leveling while grounded, so the drone conforms to the surface
	// rather than holding itself world-level against it. Airborne there is no contact, so a falling or
	// tumbling drone keeps its attitude until it touches down.
	if (bGrounded)
	{
		Forces.Torque += DroneFlight::ComputeGroundSettleTorque(State.Orientation, State.AngularVelocity, GroundNormal, GroundSettleStrength);
	}

	const FDroneFlightState Next = DroneIntegrator::IntegrateStep(State, Forces, FlightModel->GetMass(), DeltaTime);

	// Apply with swept collision so world geometry blocks the drone; slide along blocking surfaces.
	const FVector Delta = Next.Location - State.Location;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, Next.Orientation.Quaternion(), /*bSweep*/ true, Hit);
	bool bJustCrashed = false;
	if (Hit.IsValidBlockingHit())
	{
		// A blocking contact crashes the drone if it strikes hard enough, otherwise it is a bump. The
		// closing speed is the velocity carried into the move projected onto the surface, so a head-on
		// hit at or above the threshold crashes while a glancing brush slides off.
		const float ImpactSpeed = DroneFlight::ComputeImpactSpeed(Next.Velocity, Hit.Normal);
		if (!bCrashed && DroneFlight::ImpactCrashes(ImpactSpeed, CrashThreshold))
		{
			EnterCrash();
			bJustCrashed = true;
		}

		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, /*bHandleImpact*/ true);
	}

	if (bJustCrashed)
	{
		// Keep a fraction of the impact velocity so the wreck tumbles off the crash with some momentum
		// rather than skating frictionlessly along the surface it struck at full speed (which would glide
		// along a wall barely falling). Gravity and drag bleed the rest as it drops, and the tumble spin
		// EnterCrash imparted carries over.
		Velocity = Next.Velocity * CrashMomentumRetention;
	}
	else
	{
		// Recover linear velocity from the distance actually travelled, so a blocked move sheds the
		// velocity it could not realize. Angular velocity carries forward unimpeded.
		Velocity = (UpdatedComponent->GetComponentLocation() - State.Location) / DeltaTime;
		AngularVelocity = Next.AngularVelocity;
	}

	// A drone resting on a surface scrubs off the velocity it carried in instead of sliding along forever
	// on it - the same ground friction whether it is disarmed, crashed, or armed but sitting on the floor,
	// since contact with the ground is what brakes it, not the motor state. (The move's own hit misses a
	// flat horizontal glide, so the probe above is what catches the contact.) The friction acts only
	// tangent to the surface, so it brakes a slide without fighting a climbing drone's lift. The
	// just-crashed tick is left alone so the wreck keeps the tumble-off momentum it was given.
	if (!bJustCrashed && bGrounded)
	{
		Velocity = DroneFlight::ApplySurfaceFriction(Velocity, GroundNormal, GroundFrictionDeceleration, DeltaTime);
	}

	UpdateComponentVelocity();
}

void UDroneMovementComponent::EnterCrash()
{
	if (bCrashed)
	{
		return;
	}

	bCrashed = true;

	// Impart a tumble so the wreck spins as it drops rather than falling flat; the flight model's angular
	// damping bleeds it off over the fall. Yaw is left out so the spin reads as an end-over-end tumble.
	AngularVelocity = FVector(220.f, 160.f, 0.f);

	OnCrashed.Broadcast(GetPawnOwner());
}

void UDroneMovementComponent::RecoverFromCrash()
{
	bCrashed = false;
	Velocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;
	SpooledThrottle = 0.f;
	bWasHovering = false;
	UpdateComponentVelocity();
}

bool UDroneMovementComponent::FindGroundContact(FVector& OutNormal) const
{
	const UWorld* World = GetWorld();
	if (!World || !UpdatedComponent)
	{
		return false;
	}

	// Sweep the drone's own bounding sphere a short way straight down. Starting from the resting position
	// the sphere is already touching the floor, so even a few centimetres of probe finds it; a flat
	// horizontal glide that the move's swept hit misses is still caught here.
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const float Radius = UpdatedComponent->Bounds.SphereRadius;
	const float ProbeDepth = 4.f;
	const FVector End = Start - FVector(0.f, 0.f, ProbeDepth);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DroneGroundProbe), /*bTraceComplex*/ false, GetOwner());

	FHitResult Hit;
	if (World->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(Radius), QueryParams))
	{
		// A start-penetrating sweep returns a zero normal; fall back to straight up so a drone resting
		// flush on the floor still scrubs horizontally rather than being left to slide.
		OutNormal = Hit.Normal.IsNearlyZero() ? FVector::UpVector : Hit.Normal;
		return true;
	}

	return false;
}
