#include "View/DroneViewSelection.h"

namespace DroneView
{
	EDroneViewMode SelectViewMode(bool bIsLocallyControlled, bool bHmdEnabled)
	{
		return (bIsLocallyControlled && bHmdEnabled) ? EDroneViewMode::VrPilotStation : EDroneViewMode::OnboardDirect;
	}
}
