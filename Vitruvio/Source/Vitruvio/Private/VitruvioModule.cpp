// Copyright 2019 - 2020 Esri. All Rights Reserved.

#include "VitruvioModule.h"

#include "AsyncHelpers.h"
#include "PRTTypes.h"
#include "PRTUtils.h"
#include "UnrealCallbacks.h"

#include "Util/AttributeConversion.h"
#include "Util/MaterialConversion.h"
#include "Util/PolygonWindings.h"

#include "prt/API.h"
#include "prtx/EncoderInfoBuilder.h"

#include "Core.h"
#include "Interfaces/IPluginManager.h"
#include "MeshDescription.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/UObjectBaseUtility.h"

#define LOCTEXT_NAMESPACE "VitruvioModule"

DEFINE_LOG_CATEGORY(LogUnrealPrt);

namespace
{
constexpr const wchar_t* ATTRIBUTE_EVAL_ENCODER_ID = L"com.esri.prt.core.AttributeEvalEncoder";

class FLoadResolveMapTask
{
	TLazyObjectPtr<URulePackage> LazyRulePackagePtr;
	TPromise<ResolveMapSPtr> Promise;
	TMap<TLazyObjectPtr<URulePackage>, ResolveMapSPtr>& ResolveMapCache;
	FCriticalSection& LoadResolveMapLock;
	FString RpkFolder;

public:
	FLoadResolveMapTask(TPromise<ResolveMapSPtr>&& InPromise, const FString RpkFolder, const TLazyObjectPtr<URulePackage> LazyRulePackagePtr,
						TMap<TLazyObjectPtr<URulePackage>, ResolveMapSPtr>& ResolveMapCache, FCriticalSection& LoadResolveMapLock)
		: LazyRulePackagePtr(LazyRulePackagePtr), Promise(MoveTemp(InPromise)), ResolveMapCache(ResolveMapCache),
		  LoadResolveMapLock(LoadResolveMapLock), RpkFolder(RpkFolder)
	{
	}

	static const TCHAR* GetTaskName() { return TEXT("FLoadResolveMapTask"); }
	FORCEINLINE static TStatId GetStatId() { RETURN_QUICK_DECLARE_CYCLE_STAT(FLoadResolveMapTask, STATGROUP_TaskGraphTasks); }

	static ENamedThreads::Type GetDesiredThread() { return ENamedThreads::AnyThread; }

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		const FString UriPath = LazyRulePackagePtr->GetPathName();

		// Create rpk on disk for PRT
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		const FString RpkFile = FPaths::GetBaseFilename(UriPath, true) + TEXT(".rpk");
		const FString RpkPath = FPaths::Combine(RpkFolder, RpkFile);
		PlatformFile.CreateDirectoryTree(*RpkFolder);
		IFileHandle* RpkHandle = PlatformFile.OpenWrite(*RpkPath);
		if (RpkHandle)
		{
			// Write file to disk
			RpkHandle->Write(LazyRulePackagePtr->Data.GetData(), LazyRulePackagePtr->Data.Num());
			RpkHandle->Flush();
			delete RpkHandle;

			// Create rpk
			const std::wstring AbsoluteRpkPath(TCHAR_TO_WCHAR(*FPaths::ConvertRelativePathToFull(RpkPath)));
			const std::wstring AbsoluteRpkFolder(TCHAR_TO_WCHAR(*FPaths::Combine(FPaths::GetPath(FPaths::ConvertRelativePathToFull(RpkPath)),
																				 FPaths::GetBaseFilename(UriPath, true) + TEXT("_Unpacked"))));
			const std::wstring RpkFileUri = prtu::toFileURI(AbsoluteRpkPath);

			prt::Status Status;
			const ResolveMapSPtr ResolveMapPtr(prt::createResolveMap(RpkFileUri.c_str(), AbsoluteRpkFolder.c_str(), &Status), PRTDestroyer());
			{
				FScopeLock Lock(&LoadResolveMapLock);
				ResolveMapCache.Add(LazyRulePackagePtr, ResolveMapPtr);
				Promise.SetValue(ResolveMapPtr);
			}
		}
		else
		{
			Promise.SetValue(nullptr);
		}
	}
};

void SetInitialShapeGeometry(const InitialShapeBuilderUPtr& InitialShapeBuilder, const FInitialShapeData& InitialShape)
{
	std::vector<double> vertexCoords;
	std::vector<uint32_t> indices;
	std::vector<uint32_t> faceCounts;

	uint32_t CurrentIndex = 0;
	for (const TArray<FVector>& FaceVertices : InitialShape.GetFaceVertices())
	{
		faceCounts.push_back(FaceVertices.Num());
		for (const FVector& Vertex : FaceVertices)
		{
			indices.push_back(CurrentIndex++);

			const FVector CEVertex = FVector(Vertex.X, Vertex.Z, Vertex.Y) / 100.0;
			vertexCoords.push_back(CEVertex.X);
			vertexCoords.push_back(CEVertex.Y);
			vertexCoords.push_back(CEVertex.Z);
		}
	}

	const prt::Status SetGeometryStatus = InitialShapeBuilder->setGeometry(vertexCoords.data(), vertexCoords.size(), indices.data(), indices.size(),
																		   faceCounts.data(), faceCounts.size());

	if (SetGeometryStatus != prt::STATUS_OK)
	{
		UE_LOG(LogUnrealPrt, Error, TEXT("InitialShapeBuilder setGeometry failed status = %hs"), prt::getStatusDescription(SetGeometryStatus))
	}
}

AttributeMapUPtr GetDefaultAttributeValues(const std::wstring& RuleFile, const std::wstring& StartRule, const ResolveMapSPtr& ResolveMapPtr,
										   const FInitialShapeData& InitialShape, prt::Cache* Cache, const int32 RandomSeed)
{
	AttributeMapBuilderUPtr UnrealCallbacksAttributeBuilder(prt::AttributeMapBuilder::create());
	UnrealCallbacks UnrealCallbacks(UnrealCallbacksAttributeBuilder, nullptr, nullptr, nullptr);

	InitialShapeBuilderUPtr InitialShapeBuilder(prt::InitialShapeBuilder::create());

	SetInitialShapeGeometry(InitialShapeBuilder, InitialShape);

	const AttributeMapUPtr EmptyAttributes(AttributeMapBuilderUPtr(prt::AttributeMapBuilder::create())->createAttributeMap());
	InitialShapeBuilder->setAttributes(RuleFile.c_str(), StartRule.c_str(), RandomSeed, L"", EmptyAttributes.get(), ResolveMapPtr.get());

	const InitialShapeUPtr Shape(InitialShapeBuilder->createInitialShapeAndReset());
	const InitialShapeNOPtrVector InitialShapes = {Shape.get()};

	const std::vector<const wchar_t*> EncoderIds = {ATTRIBUTE_EVAL_ENCODER_ID};
	const AttributeMapUPtr AttributeEncodeOptions = prtu::createValidatedOptions(ATTRIBUTE_EVAL_ENCODER_ID);
	const AttributeMapNOPtrVector EncoderOptions = {AttributeEncodeOptions.get()};

	prt::generate(InitialShapes.data(), InitialShapes.size(), nullptr, EncoderIds.data(), EncoderIds.size(), EncoderOptions.data(), &UnrealCallbacks,
				  Cache, nullptr);

	return AttributeMapUPtr(UnrealCallbacksAttributeBuilder->createAttributeMap());
}

void CleanupTempRpkFolder()
{
	FString TempDir(WCHAR_TO_TCHAR(prtu::temp_directory_path().c_str()));
	const FString RpkUnpackFolder = FPaths::Combine(TempDir, TEXT("PRT"), TEXT("UnrealGeometryEncoder"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteDirectoryRecursively(*RpkUnpackFolder);
}

FString GetPlatformName()
{
#if PLATFORM_64BITS && PLATFORM_WINDOWS
	return "Win64";
#elif PLATFORM_MAC
	return "Mac";
#else
	return "Unknown";
#endif
}

FString GetPrtThirdPartyPath()
{
	const FString BaseDir = FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin("Vitruvio")->GetBaseDir());
	const FString BinariesPath = FPaths::Combine(*BaseDir, TEXT("Source"), TEXT("ThirdParty"), TEXT("PRT"));
	return BinariesPath;
}

FString GetEncoderExtensionPath()
{
	const FString BaseDir = FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin("Vitruvio")->GetBaseDir());
	const FString BinariesPath = FPaths::Combine(*BaseDir, TEXT("Source"), TEXT("ThirdParty"), TEXT("UnrealGeometryEncoderLib"), TEXT("lib"),
												 GetPlatformName(), TEXT("Release"));
	return BinariesPath;
}

FString GetPrtLibDir()
{
	const FString BaseDir = GetPrtThirdPartyPath();
	const FString LibDir = FPaths::Combine(*BaseDir, TEXT("lib"), GetPlatformName(), TEXT("Release"));
	return LibDir;
}

FString GetPrtBinDir()
{
	const FString BaseDir = GetPrtThirdPartyPath();
	const FString BinDir = FPaths::Combine(*BaseDir, TEXT("bin"), GetPlatformName(), TEXT("Release"));
	return BinDir;
}

FString GetPrtDllPath()
{
	const FString BaseDir = GetPrtBinDir();
	return FPaths::Combine(*BaseDir, TEXT("com.esri.prt.core.dll"));
}

} // namespace

FGenerateResult ConvertResult_GameThread(VitruvioModule const* Module, UMaterial* OpaqueParent, UMaterial* MaskedParent, UMaterial* TranslucentParent,
										 TSharedPtr<UnrealCallbacks> OutputHandler,
										 TMap<Vitruvio::FMaterialAttributeContainer, UMaterialInstanceDynamic*>& MaterialCache)
{
	const TFuture<FGenerateResult> GenerateResultFuture =
		Vitruvio::ExecuteOnGameThread<FGenerateResult>([=, &OutputHandler, &MaterialCache]() -> FGenerateResult {
			TMap<int32, UStaticMesh*> MeshMap;
			TMap<int32, TArray<Vitruvio::FMaterialAttributeContainer>> Materials = OutputHandler->GetMaterials();

			auto CachedMaterial = [OpaqueParent, MaskedParent, TranslucentParent, &MaterialCache](
									  const Vitruvio::FMaterialAttributeContainer& MaterialAttributes, const FName& Name, UObject* Outer) {
				if (MaterialCache.Contains(MaterialAttributes))
				{
					return MaterialCache[MaterialAttributes];
				}
				else
				{
					UMaterialInstanceDynamic* Material =
						Vitruvio::GameThread_CreateMaterialInstance(Outer, Name, OpaqueParent, MaskedParent, TranslucentParent, MaterialAttributes);
					MaterialCache.Add(MaterialAttributes, Material);
					return Material;
				}
			};

			// convert all meshes
			TMap<int32, FMeshDescription> Meshes = OutputHandler->GetMeshes();
			for (auto& IdAndMesh : Meshes)
			{
				UStaticMesh* StaticMesh = NewObject<UStaticMesh>();

				const TArray<Vitruvio::FMaterialAttributeContainer>& MeshMaterials = Materials[IdAndMesh.Key];
				FStaticMeshAttributes Attributes(IdAndMesh.Value);
				const auto PolygonGroups = IdAndMesh.Value.PolygonGroups();
				size_t MaterialIndex = 0;
				for (const auto& PolygonId : PolygonGroups.GetElementIDs())
				{
					const FName MaterialName = Attributes.GetPolygonGroupMaterialSlotNames()[PolygonId];
					const FName SlotName = StaticMesh->AddMaterial(CachedMaterial(MeshMaterials[MaterialIndex], MaterialName, StaticMesh));
					Attributes.GetPolygonGroupMaterialSlotNames()[PolygonId] = SlotName;

					++MaterialIndex;
				}

				TArray<const FMeshDescription*> MeshDescriptionPtrs;
				MeshDescriptionPtrs.Emplace(&IdAndMesh.Value);
				StaticMesh->BuildFromMeshDescriptions(MeshDescriptionPtrs);
				MeshMap.Add(IdAndMesh.Key, StaticMesh);
			}

			// convert materials
			TArray<FInstance> Instances;
			for (const auto& Instance : OutputHandler->GetInstances())
			{
				UStaticMesh* Mesh = MeshMap[Instance.Key.PrototypeId];
				TArray<UMaterialInstanceDynamic*> OverrideMaterials;
				for (size_t MaterialIndex = 0; MaterialIndex < Instance.Key.MaterialOverrides.Num(); ++MaterialIndex)
				{
					const Vitruvio::FMaterialAttributeContainer& MaterialContainer = Instance.Key.MaterialOverrides[MaterialIndex];
					FName MaterialName = FName(MaterialContainer.StringProperties["name"]);
					OverrideMaterials.Add(CachedMaterial(MaterialContainer, MaterialName, Mesh));
				}

				Instances.Add({Mesh, OverrideMaterials, Instance.Value});
			}

			UStaticMesh* ShapeMesh = MeshMap.Contains(UnrealCallbacks::NO_PROTOTYPE_INDEX) ? MeshMap[UnrealCallbacks::NO_PROTOTYPE_INDEX] : nullptr;
			return {true, ShapeMesh, Instances};
		});

	GenerateResultFuture.Wait();
	return GenerateResultFuture.Get();
}

void VitruvioModule::InitializePrt()
{
	const FString PrtLibPath = GetPrtDllPath();
	const FString PrtBinDir = GetPrtBinDir();
	const FString PrtLibDir = GetPrtLibDir();

	FPlatformProcess::AddDllDirectory(*PrtBinDir);
	FPlatformProcess::AddDllDirectory(*PrtLibDir);
	PrtDllHandle = FPlatformProcess::GetDllHandle(*PrtLibPath);

	TArray<wchar_t*> PRTPluginsPaths;
	const FString EncoderExtensionPath = GetEncoderExtensionPath();
	const FString PrtExtensionPaths = GetPrtLibDir();
	PRTPluginsPaths.Add(const_cast<wchar_t*>(TCHAR_TO_WCHAR(*EncoderExtensionPath)));
	PRTPluginsPaths.Add(const_cast<wchar_t*>(TCHAR_TO_WCHAR(*PrtExtensionPaths)));

	LogHandler = new UnrealLogHandler;
	prt::addLogHandler(LogHandler);

	prt::Status Status;
	PrtLibrary = prt::init(PRTPluginsPaths.GetData(), PRTPluginsPaths.Num(), prt::LogLevel::LOG_TRACE, &Status);
	Initialized = Status == prt::STATUS_OK;

	PrtCache.reset(prt::CacheObject::create(prt::CacheObject::CACHE_TYPE_NONREDUNDANT));

	const FString TempDir(WCHAR_TO_TCHAR(prtu::temp_directory_path().c_str()));
	RpkFolder = FPaths::CreateTempFilename(*TempDir, TEXT("Vitruvio_"), TEXT(""));
}

void VitruvioModule::StartupModule()
{
	InitializePrt();
}

void VitruvioModule::ShutdownModule()
{
	// TODO: what happens if we still have threads busy with generation?

	if (PrtDllHandle)
	{
		FPlatformProcess::FreeDllHandle(PrtDllHandle);
		PrtDllHandle = nullptr;
	}
	if (PrtLibrary)
	{
		PrtLibrary->destroy();
	}

	CleanupTempRpkFolder();

	delete LogHandler;
}

TFuture<FGenerateResult> VitruvioModule::GenerateAsync(const FInitialShapeData& InitialShape, UMaterial* OpaqueParent, UMaterial* MaskedParent,
													   UMaterial* TranslucentParent, URulePackage* RulePackage,
													   const TMap<FString, URuleAttribute*>& Attributes, const int32 RandomSeed) const
{
	check(RulePackage);

	if (!Initialized)
	{
		UE_LOG(LogUnrealPrt, Warning, TEXT("PRT not initialized"))
		return {};
	}

	return Async(EAsyncExecution::Thread, [=]() -> FGenerateResult {
		return Generate(InitialShape, OpaqueParent, MaskedParent, TranslucentParent, RulePackage, Attributes, RandomSeed);
	});
}

FGenerateResult VitruvioModule::Generate(const FInitialShapeData& InitialShape, UMaterial* OpaqueParent, UMaterial* MaskedParent,
										 UMaterial* TranslucentParent, URulePackage* RulePackage, const TMap<FString, URuleAttribute*>& Attributes,
										 const int32 RandomSeed) const
{
	check(RulePackage);

	if (!Initialized)
	{
		UE_LOG(LogUnrealPrt, Warning, TEXT("PRT not initialized"))
		return {false};
	}

	GenerateCallsCounter.Increment();

	const InitialShapeBuilderUPtr InitialShapeBuilder(prt::InitialShapeBuilder::create());
	SetInitialShapeGeometry(InitialShapeBuilder, InitialShape);

	const ResolveMapSPtr ResolveMap = LoadResolveMapAsync(RulePackage).Get();

	const std::wstring RuleFile = prtu::getRuleFileEntry(ResolveMap);
	const wchar_t* RuleFileUri = ResolveMap->getString(RuleFile.c_str());

	const RuleFileInfoUPtr StartRuleInfo(prt::createRuleFileInfo(RuleFileUri));
	const std::wstring StartRule = prtu::detectStartRule(StartRuleInfo);

	const AttributeMapUPtr AttributeMap = Vitruvio::CreateAttributeMap(Attributes);
	InitialShapeBuilder->setAttributes(RuleFile.c_str(), StartRule.c_str(), RandomSeed, L"", AttributeMap.get(), ResolveMap.get());

	AttributeMapBuilderUPtr AttributeMapBuilder(prt::AttributeMapBuilder::create());
	const TSharedPtr<UnrealCallbacks> OutputHandler(new UnrealCallbacks(AttributeMapBuilder, OpaqueParent, MaskedParent, TranslucentParent));

	const InitialShapeUPtr Shape(InitialShapeBuilder->createInitialShapeAndReset());

	const std::vector<const wchar_t*> EncoderIds = {UNREAL_GEOMETRY_ENCODER_ID};
	const AttributeMapUPtr UnrealEncoderOptions(prtu::createValidatedOptions(UNREAL_GEOMETRY_ENCODER_ID));
	const AttributeMapNOPtrVector EncoderOptions = {UnrealEncoderOptions.get()};

	InitialShapeNOPtrVector Shapes = {Shape.get()};

	const prt::Status GenerateStatus = prt::generate(Shapes.data(), Shapes.size(), nullptr, EncoderIds.data(), EncoderIds.size(),
													 EncoderOptions.data(), OutputHandler.Get(), PrtCache.get(), nullptr);

	if (GenerateStatus != prt::STATUS_OK)
	{
		UE_LOG(LogUnrealPrt, Error, TEXT("PRT generate failed: %hs"), prt::getStatusDescription(GenerateStatus))
	}

	GenerateCallsCounter.Decrement();

	// Convert result in GameThread
	FGenerateResult GenerateResult = ConvertResult_GameThread(this, OpaqueParent, MaskedParent, TranslucentParent, OutputHandler, MaterialCache);

	return GenerateResult;
}

TFuture<FAttributeMapPtr> VitruvioModule::LoadDefaultRuleAttributesAsync(const FInitialShapeData& InitialShape, URulePackage* RulePackage,
																		 const int32 RandomSeed) const
{
	check(RulePackage);

	if (!Initialized)
	{
		UE_LOG(LogUnrealPrt, Warning, TEXT("PRT not initialized"))
		return {};
	}

	return Async(EAsyncExecution::Thread, [=]() -> TSharedPtr<FAttributeMap> {
		const ResolveMapSPtr ResolveMap = LoadResolveMapAsync(RulePackage).Get();

		const std::wstring RuleFile = prtu::getRuleFileEntry(ResolveMap);
		const wchar_t* RuleFileUri = ResolveMap->getString(RuleFile.c_str());

		const RuleFileInfoUPtr StartRuleInfo(prt::createRuleFileInfo(RuleFileUri));
		const std::wstring StartRule = prtu::detectStartRule(StartRuleInfo);

		prt::Status InfoStatus;
		RuleFileInfoUPtr RuleInfo(prt::createRuleFileInfo(RuleFileUri, PrtCache.get(), &InfoStatus));
		if (!RuleInfo || InfoStatus != prt::STATUS_OK)
		{
			UE_LOG(LogUnrealPrt, Error, TEXT("could not get rule file info from rule file %s"), RuleFileUri)
			return {};
		}

		AttributeMapUPtr DefaultAttributeMap(
			GetDefaultAttributeValues(RuleFile.c_str(), StartRule.c_str(), ResolveMap, InitialShape, PrtCache.get(), RandomSeed));

		return MakeShared<FAttributeMap>(std::move(DefaultAttributeMap), std::move(RuleInfo));
	});
}

TFuture<ResolveMapSPtr> VitruvioModule::LoadResolveMapAsync(URulePackage* const RulePackage) const
{
	TPromise<ResolveMapSPtr> Promise;
	TFuture<ResolveMapSPtr> Future = Promise.GetFuture();

	const TLazyObjectPtr<URulePackage> LazyRulePackagePtr(RulePackage);

	// Check if has already been cached
	{
		FScopeLock Lock(&LoadResolveMapLock);
		const auto CachedResolveMap = ResolveMapCache.Find(LazyRulePackagePtr);
		if (CachedResolveMap)
		{
			Promise.SetValue(*CachedResolveMap);
			return Future;
		}
	}

	// Check if a task is already running for loading the specified resolve map
	FGraphEventRef* ScheduledTaskEvent;
	{
		FScopeLock Lock(&LoadResolveMapLock);
		ScheduledTaskEvent = ResolveMapEventGraphRefCache.Find(LazyRulePackagePtr);
	}
	if (ScheduledTaskEvent)
	{
		// Add task which only fetches the result from the cache once the actual loading has finished
		FGraphEventArray Prerequisites;
		Prerequisites.Add(*ScheduledTaskEvent);
		TGraphTask<TAsyncGraphTask<ResolveMapSPtr>>::CreateTask(&Prerequisites)
			.ConstructAndDispatchWhenReady(
				[this, LazyRulePackagePtr]() {
					FScopeLock Lock(&LoadResolveMapLock);
					return ResolveMapCache[LazyRulePackagePtr];
				},
				MoveTemp(Promise),
				ENamedThreads::
					AnyThread); // TODO AnyThread could ben GameThread
								// https://forums.unrealengine.com/development-discussion/c-gameplay-programming/14071-multithreading-creating-a-task-hangs-the-game
	}
	else
	{
		RpkLoadingTasksCounter.Increment();

		FGraphEventRef LoadTask;
		{
			FScopeLock Lock(&LoadResolveMapLock);
			// Task which does the actual resolve map loading which might take a long time
			LoadTask = TGraphTask<FLoadResolveMapTask>::CreateTask().ConstructAndDispatchWhenReady(MoveTemp(Promise), RpkFolder, LazyRulePackagePtr,
																								   ResolveMapCache, LoadResolveMapLock);
			ResolveMapEventGraphRefCache.Add(LazyRulePackagePtr, LoadTask);
		}

		// Task which removes the event from the cache once finished
		FFunctionGraphTask::CreateAndDispatchWhenReady(
			[this, LazyRulePackagePtr]() {
				FScopeLock Lock(&LoadResolveMapLock);
				RpkLoadingTasksCounter.Decrement();
				ResolveMapEventGraphRefCache.Remove(LazyRulePackagePtr);
			},
			TStatId(), LoadTask, ENamedThreads::AnyThread);
	}

	return Future;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(VitruvioModule, Vitruvio)
