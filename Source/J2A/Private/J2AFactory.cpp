#include "J2AFactory.h"
#include "JsonToAssetImporter.h"
#include "Misc/FileHelper.h"

UJ2AFactory::UJ2AFactory()
{
	Formats.Add(TEXT("json;FModel JSON Data"));
	SupportedClass = UObject::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = true;
	
	// Set priority higher than the default UE JSON/DataTable factory
	ImportPriority = DefaultImportPriority + 100;
}

bool UJ2AFactory::FactoryCanImport(const FString& Filename)
{
	FString FileContent;
	return FFileHelper::LoadFileToString(FileContent, *Filename) && FJsonToAssetImporter::CanImportJson(FileContent);
}

UObject* UJ2AFactory::FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn)
{
	FString JsonString(BufferEnd - Buffer, Buffer);
	return FJsonToAssetImporter::ImportJsonToAsset(JsonString, InParent, InName, Flags);
}
