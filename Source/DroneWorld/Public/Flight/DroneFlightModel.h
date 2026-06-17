#pragma once

#include "CoreMinimal.h"
#include "Flight/DroneFlightTypes.h"
#include "DroneFlightModel.generated.h"

// The type-specific force-computation strategy: it turns pilot intent and the current motion state
// into net force and angular acceleration. It is the only piece of the movement pipeline that
// differs between drone types; integration, collision, and imperfection are shared. Instances are
// inline-editable subobjects held by a Preset, so a model carries its own parameters.
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType)
class DRONEWORLD_API UDroneFlightModel : public UObject
{
	GENERATED_BODY()

public:
	// Pure given the model's parameters: same intent and state always yield the same forces.
	virtual FDroneForces ComputeForces(const FDroneControlIntent& Intent, const FDroneFlightState& State) const
		PURE_VIRTUAL(UDroneFlightModel::ComputeForces, return FDroneForces(););

	// The full force law used while Hover assist is engaged. Each model decides what "hold position"
	// means for its airframe: a position-capable model pulls back toward HoldLocation (horizontal and
	// altitude hold), while a model that cannot stop banks into a loiter circle around it. Replaces
	// ComputeForces for the duration of the hover, so it must also carry its own gravity compensation.
	virtual FDroneForces ComputeHoverForces(const FDroneControlIntent& Intent, const FDroneFlightState& State, const FVector& HoldLocation) const
		PURE_VIRTUAL(UDroneFlightModel::ComputeHoverForces, return FDroneForces(););

	// The drone's mass, used by the integrator to turn force into linear acceleration.
	virtual float GetMass() const
		PURE_VIRTUAL(UDroneFlightModel::GetMass, return 1.f;);
};
