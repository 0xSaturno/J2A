#include "J2A.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "JsonToAssetImporter.h"
#include "AssetRegistry/AssetRegistryModule.h"

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
}

void FJ2AModule::OnImportJsonClicked()
{
	FString CurrentPath = "/Game";
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FString> SelectedPaths;
	ContentBrowserModule.Get().GetSelectedPathViewFolders(SelectedPaths);
	if (SelectedPaths.Num() > 0)
	{
		CurrentPath = SelectedPaths[0];
		if (CurrentPath.StartsWith(TEXT("/All/")))
		{
			CurrentPath = CurrentPath.RightChop(4);
		}
	}

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
			for (const FString& Filename : OutFilenames)
			{
				FString FileContent;
				if (FFileHelper::LoadFileToString(FileContent, *Filename))
				{
					FString BaseName = FPaths::GetBaseFilename(Filename);
					
					FString PackageName = CurrentPath + TEXT("/") + BaseName;
					UPackage* Package = CreatePackage(*PackageName);

					UObject* NewAsset = FJsonToAssetImporter::ImportJsonToAsset(FileContent, Package, *BaseName, RF_Public | RF_Standalone);
					
					if (NewAsset)
					{
						FAssetRegistryModule::AssetCreated(NewAsset);
						NewAsset->MarkPackageDirty();
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJ2AModule, J2A)
