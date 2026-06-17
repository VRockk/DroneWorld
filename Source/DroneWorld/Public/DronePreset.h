#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Flight/DroneFlightTypes.h"
#include "DronePreset.generated.h"

class UDroneFlightModel;

// A drone definition a designer picks and tunes in the editor. It carries an instanced, inline-
// editable Flight Model subobject that holds the drone's type-specific parameters; the movement
// component installs it via ApplyPreset. Choosing a different Flight Model class surfaces only that
// model's fields, and adding a new model subclass needs no change here.
UCLASS(BlueprintType)
class DRONEWORLD_API UDronePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UDroneFlightModel> FlightModel;

	// The assist level this drone flies in until the pilot toggles hover. Angle (self-leveling) is the
	// approachable default; choose Acro for a raw, hands-on feel.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone")
	EDroneAssistMode BaseAssistMode = EDroneAssistMode::Angle;

	// Self-leveling stiffness used by Angle mode, 1/s^2. Higher values snap back to level harder when
	// the sticks are released.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drone", meta = (ClampMin = "0.0"))
	float LevelingStrength = 6.f;
};
