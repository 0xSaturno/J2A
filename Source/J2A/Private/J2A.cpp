#include "J2A.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserItemPath.h"
#include "AssetToolsModule.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "JsonToAssetImporter.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#define LOCTEXT_NAMESPACE "FJ2AModule"

void FJ2AModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FJ2AModule::RegisterMenus));
}

void FJ2AModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FJ2AModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu");
	if (Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("J2AActions", LOCTEXT("J2ASection", "JSON 2 Asset"));
		Section.AddMenuEntry(
			"ImportFModelJson",
			LOCTEXT("ImportFModelJson_Label", "Import FModel JSON..."),
			LOCTEXT("ImportFModelJson_Tooltip", "Select an FModel JSON file to import into this folder."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FJ2AModule::OnImportJsonClicked))
		);
	}

	UToolMenu* MatMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.Material");
	if (MatMenu)
	{
		FToolMenuSection& Section = MatMenu->AddSection("J2AActions", LOCTEXT("J2ASection", "JSON 2 Asset"));
		Section.AddMenuEntry(
			"UpdateFromFModelJson",
			LOCTEXT("UpdateFromFModelJson_Label", "Update from JSON..."),
			LOCTEXT("UpdateFromFModelJson_Tooltip", "Update this asset using data from an FModel JSON."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FJ2AModule::OnUpdateAssetClicked))
		);
		Section.AddMenuEntry(
			"RefreshMaterialInstances",
			LOCTEXT("RefreshMaterialInstances_Label", "Refresh Instances"),
			LOCTEXT("RefreshMaterialInstances_Tooltip", "Update every instance of this material, relinking any stuck on a misplaced dummy parent."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FJ2AModule::OnRefreshInstancesClicked))
		);
	}

	UToolMenu* MIMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.MaterialInstanceConstant");
	if (MIMenu)
	{
		FToolMenuSection& Section = MIMenu->AddSection("J2AActions", LOCTEXT("J2ASection", "JSON 2 Asset"));
		Section.AddMenuEntry(
			"UpdateFromFModelJson",
			LOCTEXT("UpdateFromFModelJson_Label", "Update from JSON..."),
			LOCTEXT("UpdateFromFModelJson_Tooltip", "Update this asset using data from an FModel JSON."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FJ2AModule::OnUpdateAssetClicked))
		);
		Section.AddMenuEntry(
			"ReloadMaterialInstance",
			LOCTEXT("ReloadMaterialInstance_Label", "Reload Material"),
			LOCTEXT("ReloadMaterialInstance_Tooltip", "Relink this instance to its real parent material, if imported, and refresh its parameters."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FJ2AModule::OnReloadInstanceClicked))
		);
		Section.AddMenuEntry(
			"RefreshMaterialInstances",
			LOCTEXT("RefreshMaterialInstances_Label", "Refresh Instances"),
			LOCTEXT("RefreshChildInstances_Tooltip", "Update every instance using this instance as its parent, relinking any stuck on a misplaced dummy parent."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FJ2AModule::OnRefreshInstancesClicked))
		);
	}
}

void FJ2AModule::OnImportJsonClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFilenames;
		const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

		bool bOpened = DesktopPlatform->OpenFileDialog(
			ParentWindowWindowHandle,
			TEXT("Select FModel JSON File"),
			TEXT(""),
			TEXT(""),
			TEXT("JSON Files (*.json)|*.json"),
			EFileDialogFlags::Multiple,
			OutFilenames
		);

		if (bOpened && OutFilenames.Num() > 0)
		{
			// Unreal's own import picks UJ2AFactory, asks before overwriting and reports failures
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			const FContentBrowserItemPath CurrentPath = ContentBrowserModule.Get().GetCurrentPath();
			FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().ImportAssets(
				OutFilenames, CurrentPath.HasInternalPath() ? CurrentPath.GetInternalPathString() : FString(TEXT("/Game")));
		}
	}
}

void FJ2AModule::OnUpdateAssetClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	if (SelectedAssets.Num() == 0) return;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFilenames;
		const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

		bool bOpened = DesktopPlatform->OpenFileDialog(
			ParentWindowWindowHandle,
			TEXT("Select FModel JSON File"),
			TEXT(""),
			TEXT(""),
			TEXT("JSON Files (*.json)|*.json"),
			EFileDialogFlags::None,
			OutFilenames
		);

		if (bOpened && OutFilenames.Num() > 0)
		{
			FString FileContent;
			if (FFileHelper::LoadFileToString(FileContent, *OutFilenames[0]))
			{
				for (const FAssetData& AssetData : SelectedAssets)
				{
					UObject* Asset = AssetData.GetAsset();
					if (Asset)
					{
						if (FJsonToAssetImporter::UpdateExistingAssetFromJson(Asset, FileContent))
						{
							Asset->MarkPackageDirty();
						}
					}
				}
			}
		}
	}
}

void FJ2AModule::OnRefreshInstancesClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	for (const FAssetData& AssetData : SelectedAssets)
	{
		FJsonToAssetImporter::RefreshInstancesOfMaterial(Cast<UMaterialInterface>(AssetData.GetAsset()));
	}
}

void FJ2AModule::OnReloadInstanceClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	for (const FAssetData& AssetData : SelectedAssets)
	{
		FJsonToAssetImporter::RelinkInstanceParent(Cast<UMaterialInstanceConstant>(AssetData.GetAsset()));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FJ2AModule, J2A)
