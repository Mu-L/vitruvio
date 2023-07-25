// Copyright © 2017-2023 Esri R&D Center Zurich. All rights reserved.

#include "InstanceReplacementDialog.h"

#include "DetailLayoutBuilder.h"
#include "EngineUtils.h"
#include "Framework/Docking/TabManager.h"
#include "ISinglePropertyView.h"
#include "ReplacementDialog.h"
#include "VitruvioComponent.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"

class SInstanceReplacementDialogWidget : public SReplacementDialogWidget
{
	UInstanceReplacementDialogOptions* ReplacementDialogOptions = nullptr;

	TArray<TSharedPtr<SCheckBox>> IsolateCheckboxes;
	TSharedPtr<SCheckBox> ApplyToAllVitruvioActorsCheckBox;

public:
	SLATE_BEGIN_ARGS(SInstanceReplacementDialogWidget) {}
	SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_ARGUMENT(UVitruvioComponent*, VitruvioComponent)
	SLATE_END_ARGS()

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	void Construct(const FArguments& InArgs);

protected:
	virtual FText CreateHeaderText() override;
	virtual TSharedPtr<ISinglePropertyView> CreateTargetReplacementWidget() override;
	virtual void UpdateApplyButtonEnablement() override;
	virtual void OnCreateNewAsset() override;
	virtual void AddDialogOptions(const TSharedPtr<SVerticalBox>& Content) override;
	virtual void OnWindowClosed() override;
	virtual void UpdateReplacementTable() override;
	virtual FReply OnReplacementConfirmed() override;
	virtual FReply OnReplacementCanceled() override;
};

void SInstanceReplacementDialogWidget::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ReplacementDialogOptions);
}

void SInstanceReplacementDialogWidget::Construct(const FArguments& InArgs)
{
	ReplacementDialogOptions = NewObject<UInstanceReplacementDialogOptions>();
	ReplacementDialogOptions->TargetReplacementAsset = InArgs._VitruvioComponent->InstanceReplacement;

	// clang-format off
	SReplacementDialogWidget::Construct(SReplacementDialogWidget::FArguments()
		.ParentWindow(InArgs._ParentWindow)
		.VitruvioComponent(InArgs._VitruvioComponent));
	// clang-format on

	ApplyButton->SetEnabled(ReplacementDialogOptions->TargetReplacementAsset != nullptr);
}

FText SInstanceReplacementDialogWidget::CreateHeaderText()
{
	return FText::FromString(TEXT("Choose Instance replacements and the DataTable where they will be added."));
}

TSharedPtr<ISinglePropertyView> SInstanceReplacementDialogWidget::CreateTargetReplacementWidget()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FSinglePropertyParams SinglePropertyArgs;
	SinglePropertyArgs.NamePlacement = EPropertyNamePlacement::Hidden;

	const TSharedPtr<ISinglePropertyView> TargetReplacementWidget = PropertyEditorModule.CreateSingleProperty(
		ReplacementDialogOptions, GET_MEMBER_NAME_CHECKED(UInstanceReplacementDialogOptions, TargetReplacementAsset), SinglePropertyArgs);

	return TargetReplacementWidget;
}

void SInstanceReplacementDialogWidget::UpdateApplyButtonEnablement()
{
	ApplyButton->SetEnabled(ReplacementDialogOptions->TargetReplacementAsset != nullptr);
}

void SInstanceReplacementDialogWidget::OnCreateNewAsset()
{
	CreateNewAsset<UInstanceReplacementAsset, UInstanceReplacementDialogOptions>(ReplacementDialogOptions);
}

void SInstanceReplacementDialogWidget::AddDialogOptions(const TSharedPtr<SVerticalBox>& Content)
{
	// clang-format off
	const FString ApplyToAllCheckBoxText = TEXT("Apply to all '") + VitruvioComponent->GetRpk()->GetName() + TEXT("' VitruvioActors");
	
	Content->AddSlot()
	.Padding(4)
	.AutoHeight()
	[
		SAssignNew(ApplyToAllVitruvioActorsCheckBox, SCheckBox)
		.IsChecked(true)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
			.Text(FText::FromString(ApplyToAllCheckBoxText))
		]
	];
	// clang-format on
}

void SInstanceReplacementDialogWidget::OnWindowClosed()
{
	for (const auto& [InstanceKey, Replacement] : ReplacementDialogOptions->InstanceReplacements)
	{
		if (VitruvioComponent->GetGeneratedModelComponent())
		{
			VitruvioComponent->GetGeneratedModelComponent()->SetVisibility(true, false);
		}

		for (UStaticMeshComponent* MeshComponent : Replacement->MeshComponents)
		{
			MeshComponent->SetVisibility(true, false);
		}
	}

	VitruvioComponent->Generate();
}

void SInstanceReplacementDialogWidget::UpdateReplacementTable()
{
	ReplacementsBox->ClearChildren();
	IsolateCheckboxes.Empty();

	ReplacementDialogOptions->InstanceReplacements.Empty();

	TMap<FString, FInstanceReplacement> CurrentReplacements;
	if (ReplacementDialogOptions->TargetReplacementAsset)
	{
		for (const FInstanceReplacement& ReplacementData : ReplacementDialogOptions->TargetReplacementAsset->Replacements)
		{
			CurrentReplacements.Add(ReplacementData.SourceMeshIdentifier, ReplacementData);
		}
	}

	for (UGeneratedModelHISMComponent* HISMComponent : VitruvioComponent->GetGeneratedModelHISMComponents())
	{
		UInstanceReplacementWrapper* InstanceReplacement;
		if (UInstanceReplacementWrapper** Existing = ReplacementDialogOptions->InstanceReplacements.Find(HISMComponent->GetMeshIdentifier()))
		{
			InstanceReplacement = *Existing;
		}
		else
		{
			InstanceReplacement = NewObject<UInstanceReplacementWrapper>();
			InstanceReplacement->SourceMeshIdentifier = HISMComponent->GetMeshIdentifier();
			ReplacementDialogOptions->InstanceReplacements.Add(HISMComponent->GetMeshIdentifier(), InstanceReplacement);

			if (FInstanceReplacement* Replacement = CurrentReplacements.Find(HISMComponent->GetMeshIdentifier()))
			{
				InstanceReplacement->Replacements = Replacement->Replacements;
			}
		}
		
		InstanceReplacement->MeshComponents.Add(HISMComponent);
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FSinglePropertyParams SinglePropertyArgs;
	SinglePropertyArgs.NamePlacement = EPropertyNamePlacement::Hidden;

	auto ResetMaterialPreview = [this](const TArray<TSharedPtr<SCheckBox>>& CheckBoxes, int32 IgnoreIndex) {
		for (int32 Index = 0; Index < CheckBoxes.Num(); ++Index)
		{
			if (Index != IgnoreIndex)
			{
				const TSharedPtr<SCheckBox>& CheckBox = CheckBoxes[Index];
				CheckBox->SetIsChecked(false);
			}
		}
	};

	for (const auto& [Key, Replacement] : ReplacementDialogOptions->InstanceReplacements)
	{
		bool bValid = !Replacement->SourceMeshIdentifier.IsEmpty();

		TSharedRef<SHorizontalBox> ReplacementBox = SNew(SHorizontalBox);
		TSharedPtr<STextBlock> SourceMaterialText;

		TSharedPtr<SCheckBox> IsolateCheckbox;

		FString MeshIdentifier = Replacement->SourceMeshIdentifier;

		TArray<FString> MeshNamesArray;
		Algo::Transform(Replacement->MeshComponents, MeshNamesArray, [](const UStaticMeshComponent* StaticMeshComponent) { return StaticMeshComponent->GetName(); });
		if (!MeshNamesArray.IsEmpty())
		{
			const FString MeshNameString = FString::Join(MeshNamesArray, TEXT(", "));
			MeshIdentifier += " [" + MeshNameString + "]";
		}

		// clang-format off
		ReplacementBox->AddSlot()
		.VAlign(VAlign_Top)
		.Padding(0, 8, 0,0)
		[
			SNew(SVerticalBox) +
			SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(SourceMaterialText, STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(FText::FromString(MeshIdentifier))
				.ColorAndOpacity(bValid ? FLinearColor(1.0f, 1.0f, 1.0f, 1.0f) : FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
			]
			
			+ SVerticalBox::Slot()
			.Padding(0, 4, 0, 0)
			.AutoHeight()
			[
				SAssignNew(IsolateCheckbox, SCheckBox)
				.OnCheckStateChanged_Lambda(
					[ResetMaterialPreview, Key, IgnoreIndex = IsolateCheckboxes.Num(), Replacement, this](ECheckBoxState CheckBoxState)
					{
						ResetMaterialPreview(IsolateCheckboxes, IgnoreIndex);

						VitruvioComponent->GetGeneratedModelComponent()->SetVisibility(CheckBoxState != ECheckBoxState::Checked, false);
						
						for (const auto& [OtherKey, OtherReplacement] : ReplacementDialogOptions->InstanceReplacements)
						{
							const bool bVisible = (CheckBoxState == ECheckBoxState::Checked && Replacement == OtherReplacement) ||
													CheckBoxState == ECheckBoxState::Unchecked;

							for (UStaticMeshComponent* MeshComponent : OtherReplacement->MeshComponents)
							{
								MeshComponent->SetVisibility(bVisible, false);
							}
						}
					})
				.IsChecked(false)
				.IsEnabled(bValid)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
					.Text(FText::FromString("Isolate"))
				]
			]
		];
		// clang-format on

		IsolateCheckboxes.Add(IsolateCheckbox);

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bShowObjectLabel = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowCustomFilterOption = false;
		DetailsViewArgs.bShowScrollBar = false;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bShowSectionSelector = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::HideNameArea;

		TSharedRef<IDetailsView> MeshReplacementsDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

		MeshReplacementsDetailsView->SetEnabled(bValid);
		MeshReplacementsDetailsView->SetObject(Replacement, true);

		// clang-format off
		ReplacementBox->AddSlot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(4)
			[
				SNew(SBox)
				.MinDesiredWidth(200)
				[
					MeshReplacementsDetailsView
				]
			]
		];

		// clang-format on

		ReplacementsBox->AddSlot().Padding(4.0f).VAlign(VAlign_Fill).HAlign(HAlign_Fill)[ReplacementBox];
	}
}

FReply SInstanceReplacementDialogWidget::OnReplacementConfirmed()
{
	if (ReplacementDialogOptions->TargetReplacementAsset)
	{
		for (const auto& Replacement : ReplacementDialogOptions->InstanceReplacements)
		{
			if (Replacement.Value->Replacements.IsEmpty())
			{
				continue;
			}

			FInstanceReplacement InstanceReplacement;
			InstanceReplacement.SourceMeshIdentifier = Replacement.Value->SourceMeshIdentifier;
			InstanceReplacement.Replacements = Replacement.Value->Replacements;
			ReplacementDialogOptions->TargetReplacementAsset->Replacements.Add(InstanceReplacement);
		}
	}

	TArray<UVitruvioComponent*> ApplyToComponents;
	ApplyToComponents.Add(VitruvioComponent);

	if (ApplyToAllVitruvioActorsCheckBox->IsChecked())
	{
		TArray<AActor*> Actors;
		if (const UWorld* World = GEngine->GetWorldFromContextObject(VitruvioComponent, EGetWorldErrorMode::LogAndReturnNull))
		{
			for (TActorIterator<AActor> It(World, AActor::StaticClass()); It; ++It)
			{
				if (UVitruvioComponent* Component = It->FindComponentByClass<UVitruvioComponent>())
				{
					ApplyToComponents.Add(Component);
				}
			}
		}
	}

	for (UVitruvioComponent* Component : ApplyToComponents)
	{
		Component->InstanceReplacement = ReplacementDialogOptions->TargetReplacementAsset;
		Component->Generate();
	}

	if (WeakParentWindow.IsValid())
	{
		WeakParentWindow.Pin()->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SInstanceReplacementDialogWidget::OnReplacementCanceled()
{
	if (WeakParentWindow.IsValid())
	{
		WeakParentWindow.Pin()->RequestDestroyWindow();
	}

	return FReply::Handled();
}

void FInstanceReplacementDialog::OpenDialog(UVitruvioComponent* VitruvioComponent)
{
	FReplacementDialog::OpenDialog<SInstanceReplacementDialogWidget>(VitruvioComponent, {800, 600});
}
