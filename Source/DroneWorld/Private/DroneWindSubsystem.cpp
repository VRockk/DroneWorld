#include "DroneWindSubsystem.h"
#include "Engine/World.h"

FVector UDroneWindSubsystem::GetCurrentWind() const
{
	const UWorld* World = GetWorld();
	const float Time = World ? World->GetTimeSeconds() : 0.f;

	// One fixed seed per world: the whole level's wind gusts on a single coherent schedule, so every
	// drone is shoved by the same breeze at the same moment.
	return DroneFlight::ComputeWorldWind(Wind, /*Seed*/ 0, Time);
}
