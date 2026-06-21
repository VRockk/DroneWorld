#include "NNERuntimeTRTSettings.h"

FTensorRTDimensionRange UNNERuntimeTRTSettings::GetDimensionRangeFor(const FString& InputName, int32 DimIndex) const
{
	const FTensorRTDimensionOverride* PerNameDim = nullptr;
	const FTensorRTDimensionOverride* PerName = nullptr;
	const FTensorRTDimensionOverride* PerDim = nullptr;

	for (const FTensorRTDimensionOverride& Override : DimensionOverrides)
	{
		const bool bHasName = !Override.Name.IsEmpty();
		const bool bHasIndex = Override.Index >= 0;

		const bool bMatchesName = !bHasName || Override.Name.Equals(InputName, ESearchCase::IgnoreCase);
		const bool bMatchesIndex = !bHasIndex || Override.Index == DimIndex;

		if (!bMatchesName || !bMatchesIndex)
		{
			continue;
		}

		if (bHasName && bHasIndex)
		{
			PerNameDim = &Override;
		}
		else if (bHasName)
		{
			PerName = &Override;
		}
		else if (bHasIndex)
		{
			PerDim = &Override;
		}
	}

	if (PerNameDim)	return PerNameDim->Range;
	if (PerName)	return PerName->Range;
	if (PerDim)		return PerDim->Range;
	return DefaultDimensionRanges;
}