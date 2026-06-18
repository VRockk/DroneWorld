#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Flight/DroneImperfection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionMotorLagDelaysThrustResponse,
	"DroneWorld.Imperfection.MotorLagDelaysThrustResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionMotorLagDelaysThrustResponse::RunTest(const FString& Parameters)
{
	// From idle, snapping the stick to full does not realize full thrust this tick: the motors spool
	// up, so the realized throttle sits between the old value and the command for one short step.
	const float Spooled = DroneFlight::StepMotorLag(0.f, 1.f, 0.15f, 0.016f);

	TestTrue(TEXT("throttle rises from idle toward the command"), Spooled > 0.f);
	TestTrue(TEXT("throttle has not reached full in a single tick"), Spooled < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionMotorLagSpoolsUpToCommand,
	"DroneWorld.Imperfection.MotorLagSpoolsUpToCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionMotorLagSpoolsUpToCommand::RunTest(const FString& Parameters)
{
	// Held at full command, each step climbs and the throttle converges to the command rather than
	// stalling short or overshooting: after enough spool time the motors have caught up.
	float Throttle = 0.f;
	float Previous = -1.f;
	for (int32 Step = 0; Step < 120; ++Step)
	{
		Throttle = DroneFlight::StepMotorLag(Throttle, 1.f, 0.15f, 0.016f);
		TestTrue(TEXT("throttle climbs monotonically toward the command"), Throttle > Previous && Throttle <= 1.f);
		Previous = Throttle;
	}

	TestTrue(TEXT("throttle has effectively caught up to the command"), FMath::IsNearlyEqual(Throttle, 1.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionWindScalesWithSusceptibility,
	"DroneWorld.Imperfection.WindScalesWithSusceptibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionWindScalesWithSusceptibility::RunTest(const FString& Parameters)
{
	// Wind enters the layer as the world wind scaled by the preset's susceptibility: a drone twice as
	// susceptible feels twice the wind force, in the wind's direction. Bob and drift are tuned out here
	// so only the wind term remains.
	FImperfectionParams Light;
	Light.HoverBobAmplitude = 0.f;
	Light.DriftAmplitude = 0.f;
	Light.WindSusceptibility = 2.f;

	FImperfectionParams Heavy = Light;
	Heavy.WindSusceptibility = 1.f;

	const FVector WorldWind(150.f, -60.f, 0.f);
	const FDroneForces LightForces = DroneFlight::ComputeImperfectionForces(Light, 1, 3.f, WorldWind, false);
	const FDroneForces HeavyForces = DroneFlight::ComputeImperfectionForces(Heavy, 1, 3.f, WorldWind, false);

	TestTrue(TEXT("wind force points along the world wind, scaled by susceptibility"),
		LightForces.Force.Equals(WorldWind * 2.f));
	TestTrue(TEXT("the more susceptible drone is shoved twice as hard"),
		LightForces.Force.Equals(HeavyForces.Force * 2.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionIsDeterministicPerSeedAndTime,
	"DroneWorld.Imperfection.IsDeterministicPerSeedAndTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionIsDeterministicPerSeedAndTime::RunTest(const FString& Parameters)
{
	// The perturbation is a pure function of (params, seed, time, wind): the same seed and time always
	// reproduce the same force, so a drone's drift is repeatable headless. Two different seeds wander
	// independently, so drones placed side by side do not bob and drift in lockstep.
	const FImperfectionParams Params;
	const FVector WorldWind(40.f, 0.f, 0.f);

	const FDroneForces First = DroneFlight::ComputeImperfectionForces(Params, 7, 1.234f, WorldWind, true);
	const FDroneForces Again = DroneFlight::ComputeImperfectionForces(Params, 7, 1.234f, WorldWind, true);
	const FDroneForces OtherSeed = DroneFlight::ComputeImperfectionForces(Params, 99, 1.234f, WorldWind, true);

	TestTrue(TEXT("same seed and time reproduce the identical perturbation"), First.Force.Equals(Again.Force));
	TestFalse(TEXT("a different seed wanders on a different path"), First.Force.Equals(OtherSeed.Force));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionHoverBobIsBoundedAndOscillatory,
	"DroneWorld.Imperfection.HoverBobIsBoundedAndOscillatory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionHoverBobIsBoundedAndOscillatory::RunTest(const FString& Parameters)
{
	// While hovering, the vertical bob swings both up and down (oscillatory) but never exceeds its
	// configured amplitude (bounded), so a parked drone breathes gently rather than launching itself.
	// Drift and wind are tuned out so only the vertical bob remains.
	FImperfectionParams Params;
	Params.DriftAmplitude = 0.f;
	Params.HoverBobAmplitude = 500.f;
	Params.HoverBobFrequency = 0.5f;

	float MinZ = TNumericLimits<float>::Max();
	float MaxZ = TNumericLimits<float>::Lowest();
	for (int32 Step = 0; Step <= 200; ++Step)
	{
		const float Time = Step * 0.02f; // two full bob periods at 0.5 Hz
		const float Z = DroneFlight::ComputeImperfectionForces(Params, 3, Time, FVector::ZeroVector, true).Force.Z;
		MinZ = FMath::Min(MinZ, Z);
		MaxZ = FMath::Max(MaxZ, Z);
		TestTrue(TEXT("bob never exceeds its amplitude"), FMath::Abs(Z) <= Params.HoverBobAmplitude + 0.01f);
	}

	TestTrue(TEXT("bob lifts above neutral at some point"), MaxZ > 0.f);
	TestTrue(TEXT("bob dips below neutral at some point"), MinZ < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionDriftKeepsANonHoveringDroneAlive,
	"DroneWorld.Imperfection.DriftKeepsANonHoveringDroneAlive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionDriftKeepsANonHoveringDroneAlive::RunTest(const FString& Parameters)
{
	// Even in calm air and not hovering, the coherent-noise drift gives a non-zero horizontal nudge, so
	// nothing is ever perfectly still. The hover bob, by contrast, stays off until hover is engaged.
	FImperfectionParams Params;
	Params.HoverBobAmplitude = 500.f;
	Params.DriftAmplitude = 300.f;

	const FDroneForces Forces = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, FVector::ZeroVector, false);

	TestTrue(TEXT("drift gives a non-zero horizontal nudge in calm air"), Forces.Force.Size2D() > 1.f);
	TestTrue(TEXT("no vertical bob while not hovering"), FMath::IsNearlyZero((float)Forces.Force.Z));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionGroundedSuppressesAllPerturbation,
	"DroneWorld.Imperfection.GroundedSuppressesAllPerturbation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionGroundedSuppressesAllPerturbation::RunTest(const FString& Parameters)
{
	// A drone resting on a surface is held by the ground, not flying, so the whole layer switches off: no
	// drift or wind to skate it across the floor and no attitude wobble to jitter it in place - a parked
	// drone sits perfectly still. Airborne the same tuning perturbs both force and torque as before.
	FImperfectionParams Params;
	Params.DriftAmplitude = 300.f;
	Params.WindSusceptibility = 2.f;
	Params.AttitudeWobbleAmplitude = 20.f;
	const FVector WorldWind(150.f, -60.f, 0.f);

	const FDroneForces Airborne = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, WorldWind, /*bHovering*/ false, /*bGrounded*/ false);
	const FDroneForces Grounded = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, WorldWind, /*bHovering*/ false, /*bGrounded*/ true);

	TestTrue(TEXT("airborne, the layer perturbs the linear force"), Airborne.Force.Size() > 1.f);
	TestTrue(TEXT("airborne, the layer wobbles roll and pitch"), Airborne.Torque.Size() > 0.f);
	TestTrue(TEXT("grounded, no perturbing force remains"), Grounded.Force.IsNearlyZero());
	TestTrue(TEXT("grounded, no perturbing torque remains"), Grounded.Torque.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionHoverDriftMovesTheHoldPointNotTheForce,
	"DroneWorld.Imperfection.HoverDriftMovesTheHoldPointNotTheForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionHoverDriftMovesTheHoldPointNotTheForce::RunTest(const FString& Parameters)
{
	// While hovering, horizontal drift is expressed as a wander of the hold point, not as an additive
	// force, so the position-hold spring follows the drift instead of fighting it. The imperfection
	// force while hovering therefore carries no horizontal drift - only the vertical bob and wind.
	FImperfectionParams Params;
	Params.DriftAmplitude = 1000.f;
	Params.HoverBobAmplitude = 0.f;

	const FDroneForces Forces = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, FVector::ZeroVector, true);

	TestTrue(TEXT("no horizontal drift force while hovering"), FMath::IsNearlyZero((float)Forces.Force.Size2D()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionHoverDriftWandersTheHoldPoint,
	"DroneWorld.Imperfection.HoverDriftWandersTheHoldPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionHoverDriftWandersTheHoldPoint::RunTest(const FString& Parameters)
{
	// The hover hold point drifts horizontally so a parked drone gently wanders around its spot rather
	// than freezing on the setpoint. The wander is bounded by the configured radius, has no vertical
	// component (the bob owns vertical), and is deterministic per seed and time.
	FImperfectionParams Params;
	Params.DriftHoverRadius = 200.f;

	const FVector Offset = DroneFlight::ComputeDriftOffset(Params, 5, 0.37f);
	const FVector Again = DroneFlight::ComputeDriftOffset(Params, 5, 0.37f);

	TestTrue(TEXT("the hold point drifts horizontally"), Offset.Size2D() > 1.f);
	TestTrue(TEXT("each axis stays within the configured radius"),
		FMath::Abs(Offset.X) <= Params.DriftHoverRadius + 0.01f && FMath::Abs(Offset.Y) <= Params.DriftHoverRadius + 0.01f);
	TestTrue(TEXT("the wander has no vertical component"), FMath::IsNearlyZero((float)Offset.Z));
	TestTrue(TEXT("same seed and time reproduce the same wander"), Offset.Equals(Again));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionAttitudeWobbleNudgesRollAndPitch,
	"DroneWorld.Imperfection.AttitudeWobbleNudgesRollAndPitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionAttitudeWobbleNudgesRollAndPitch::RunTest(const FString& Parameters)
{
	// A slight coherent-noise wobble keeps the airframe from sitting perfectly rigid: the layer perturbs
	// torque about roll (X) and pitch (Y), leaves yaw (Z) to the pilot, stays within the configured
	// amplitude, and is deterministic per seed and time.
	FImperfectionParams Params;
	Params.AttitudeWobbleAmplitude = 20.f;

	const FDroneForces Forces = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, FVector::ZeroVector, true);
	const FDroneForces Again = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, FVector::ZeroVector, true);

	TestTrue(TEXT("roll and pitch get a non-zero wobble torque"),
		FVector(Forces.Torque.X, Forces.Torque.Y, 0.f).Size() > 1.f);
	TestTrue(TEXT("roll wobble stays within amplitude"), FMath::Abs(Forces.Torque.X) <= Params.AttitudeWobbleAmplitude + 0.01f);
	TestTrue(TEXT("pitch wobble stays within amplitude"), FMath::Abs(Forces.Torque.Y) <= Params.AttitudeWobbleAmplitude + 0.01f);
	TestTrue(TEXT("yaw is left untouched by the layer"), FMath::IsNearlyZero((float)Forces.Torque.Z));
	TestTrue(TEXT("same seed and time reproduce the same wobble"), Forces.Torque.Equals(Again.Torque));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionAttitudeWobbleAppliesWhetherHoveringOrNot,
	"DroneWorld.Imperfection.AttitudeWobbleAppliesWhetherHoveringOrNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionAttitudeWobbleAppliesWhetherHoveringOrNot::RunTest(const FString& Parameters)
{
	// The attitude wobble is always-on, so a hovering drone is no more rigid than one flying free: the
	// roll/pitch perturbation is identical in both states for the same seed and time.
	FImperfectionParams Params;
	Params.AttitudeWobbleAmplitude = 20.f;

	const FDroneForces Hovering = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, FVector::ZeroVector, true);
	const FDroneForces FreeFlight = DroneFlight::ComputeImperfectionForces(Params, 5, 0.37f, FVector::ZeroVector, false);

	TestTrue(TEXT("the wobble torque is present"), FVector(Hovering.Torque.X, Hovering.Torque.Y, 0.f).Size() > 1.f);
	TestTrue(TEXT("the same wobble applies whether hovering or flying free"), Hovering.Torque.Equals(FreeFlight.Torque));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionWorldWindBlowsAlongDirectionAtBaseSpeed,
	"DroneWorld.Imperfection.WorldWindBlowsAlongDirectionAtBaseSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionWorldWindBlowsAlongDirectionAtBaseSpeed::RunTest(const FString& Parameters)
{
	// With no gusts, the world wind is a steady breeze: a force of the base magnitude along the wind
	// direction, unchanging over time and independent of how the direction was scaled.
	FWorldWindParams Wind;
	Wind.Direction = FVector(0.f, 10.f, 0.f); // due +Y, deliberately not unit length
	Wind.BaseSpeed = 250.f;
	Wind.GustVariance = 0.f;

	const FVector Early = DroneFlight::ComputeWorldWind(Wind, 4, 0.f);
	const FVector Late = DroneFlight::ComputeWorldWind(Wind, 4, 12.f);

	TestTrue(TEXT("wind blows along the direction at the base speed"), Early.Equals(FVector(0.f, 250.f, 0.f)));
	TestTrue(TEXT("a gust-free wind is steady over time"), Early.Equals(Late));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDroneImperfectionWorldWindGustsVaryButStayBounded,
	"DroneWorld.Imperfection.WorldWindGustsVaryButStayBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneImperfectionWorldWindGustsVaryButStayBounded::RunTest(const FString& Parameters)
{
	// Gusts make the wind ebb and flow: its magnitude changes over time, but never strays further from
	// the base than the gust variance allows, so a gust is a believable shove rather than a teleport.
	FWorldWindParams Wind;
	Wind.Direction = FVector(1.f, 0.f, 0.f);
	Wind.BaseSpeed = 400.f;
	Wind.GustVariance = 150.f;
	Wind.GustFrequency = 0.3f;

	float MinX = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	for (int32 Step = 0; Step <= 300; ++Step)
	{
		const float X = DroneFlight::ComputeWorldWind(Wind, 8, Step * 0.05f).X;
		MinX = FMath::Min(MinX, X);
		MaxX = FMath::Max(MaxX, X);
		TestTrue(TEXT("gusting wind stays within variance of the base"),
			X >= Wind.BaseSpeed - Wind.GustVariance - 0.01f && X <= Wind.BaseSpeed + Wind.GustVariance + 0.01f);
	}

	TestTrue(TEXT("the gust actually varies the wind over time"), MaxX - MinX > 1.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
