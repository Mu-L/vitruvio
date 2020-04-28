#pragma once

#include "Containers/Array.h"
#include "UObject/Object.h"
#include "RuleAttributes.generated.h"

using FAttributeGroups = TArray<FString>;

UCLASS()
class VITRUVIO_API URuleAttribute : public UObject
{
	GENERATED_BODY()

public:
	FString Name;

	FAttributeGroups Groups;
};

UCLASS()
class VITRUVIO_API UStringAttribute : public URuleAttribute
{
	GENERATED_BODY()

public:
	FString Value;
};

UCLASS()
class VITRUVIO_API UFloatAttribute : public URuleAttribute
{
	GENERATED_BODY()

public:
	float Value;
	float Min;
	float Max;
};

UCLASS()
class VITRUVIO_API UBoolAttribute : public URuleAttribute
{
	GENERATED_BODY()

public:
	bool Value;
};