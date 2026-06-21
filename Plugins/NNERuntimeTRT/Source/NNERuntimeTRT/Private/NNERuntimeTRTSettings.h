#pragma once

#include "UObject/Object.h"
#include "Containers/Map.h"

#include "NNERuntimeTRTSettings.generated.h"

/** Minimum, optimum and maximal input dimensions for dynamic input tensors */
USTRUCT()
struct FTensorRTDimensionRange
{
	GENERATED_BODY()

	/** Minimum permitted value for dynamic input dimensions */
	UPROPERTY(EditAnywhere, Category="DimensionRange", meta=(ClampMin="1"))
	int32 Min = 1;

	/** Optimal value for dynamic input dimensions */
	UPROPERTY(EditAnywhere, Category="DimensionRange", meta=(ClampMin="1"))
	int32 Opt = 128;

	/** Maximum permitted value for dynamic input dimensions */
	UPROPERTY(EditAnywhere, Category="DimensionRange", meta=(ClampMin="1"))
	int32 Max = 4096;
};

/** Dimension override for specific input tensors */
USTRUCT()
struct FTensorRTDimensionOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="DimensionOverride")
	FString Name;

	UPROPERTY(EditAnywhere, Category="DimensionOverride")
	int32 Index = -1;

	UPROPERTY(EditAnywhere, Category="DimensionOverride")
	FTensorRTDimensionRange Range;
};

UCLASS()
class UNNERuntimeTRTSettings : public UObject
{
	GENERATED_BODY()

public:

	/** Default range for all dynamic input dimensions */
	UPROPERTY(EditAnywhere, Category="NNERuntimeTRT")
	FTensorRTDimensionRange DefaultDimensionRanges;

	/** Optional override for specific input tensors */
	UPROPERTY(EditAnywhere, Category="NNERuntimeTRT")
	TArray<FTensorRTDimensionOverride> DimensionOverrides;

	/** Helper to get range for a specific input tensor and dimension index */
	FTensorRTDimensionRange GetDimensionRangeFor(const FString& InputName, int32 DimIndex) const;
};