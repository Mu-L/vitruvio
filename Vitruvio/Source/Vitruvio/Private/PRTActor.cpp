// Fill out your copyright notice in the Description page of Project Settings.

#include "PRTActor.h"
#include "UnrealGeometryEncoderModule.h"
#include "VitruvioModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

APRTActor::APRTActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APRTActor::BeginPlay()
{
	Super::BeginPlay();
}

void APRTActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// Note that we also tick in editor for initialization
	if (!Initialized)
	{
		// Load default values for generate attributes if they have not been set
		if (Rpk)
		{
			UStaticMesh* InitialShape = GetStaticMeshComponent()->GetStaticMesh();
			VitruvioModule::Get().LoadDefaultRuleAttributesAsync(InitialShape, Rpk).Then([this](const TFuture<TMap<FString, URuleAttribute*>>& Attributes) {
				GenerateAttributes = Attributes.Get();
			});
		}

		if (GenerateAutomatically)
		{
			Regenerate();
		}

		Initialized = true;
	}
}

void APRTActor::Regenerate()
{
	if (Rpk)
	{
		// Remove old prt meshes
		TArray<AActor*> GeneratedMeshes;
		GetAttachedActors(GeneratedMeshes);
		for (const auto& Child : GeneratedMeshes)
		{
			Child->Destroy();
		}

		// Generate
		if (GetStaticMeshComponent())
		{
			UStaticMesh* InitialShape = GetStaticMeshComponent()->GetStaticMesh();

			if (InitialShape)
			{
				VitruvioModule::Get().LoadDefaultRuleAttributesAsync(InitialShape, Rpk)
				.Then([=](const TFuture<TMap<FString, URuleAttribute*>>& Attributes)
					{
						return VitruvioModule::Get().Generate(InitialShape, OpaqueParent, Rpk, Attributes.Get());
					})
				.Then([=](const TFuture<FGenerateResult>& Result)
				{
					const FGraphEventRef CreateMeshTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, &Result]()
					{
						FActorSpawnParameters Parameters;
						Parameters.Owner = this;
						AStaticMeshActor* StaticMeshActor = GetWorld()->SpawnActor<AStaticMeshActor>(Parameters);
						StaticMeshActor->SetMobility(EComponentMobility::Movable);
						StaticMeshActor->GetStaticMeshComponent()->SetStaticMesh(Result.Get().ShapeMesh);
						StaticMeshActor->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);

						for (const auto Instance : Result.Get().Instances)
						{
							auto InstancedComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(StaticMeshActor);
							InstancedComponent->SetStaticMesh(Instance.Key);
							for (FTransform InstanceTransform : Instance.Value)
							{
								InstancedComponent->AddInstance(InstanceTransform);
							}
							StaticMeshActor->AddInstanceComponent(InstancedComponent);
							InstancedComponent->RegisterComponent();
							InstancedComponent->SetRelativeTransform(StaticMeshActor->GetTransform());
						}
						StaticMeshActor->RegisterAllComponents();
						
					}, TStatId(), nullptr, ENamedThreads::GameThread);
					
					FTaskGraphInterface::Get().WaitUntilTaskCompletes(CreateMeshTask);
				});
			}
		}
	}
}

#ifdef WITH_EDITOR
void APRTActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(APRTActor, Rpk))
	{
		GenerateAttributes.Empty();
	}

	if (GenerateAutomatically)
	{
		Regenerate();
	}
}

bool APRTActor::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif
