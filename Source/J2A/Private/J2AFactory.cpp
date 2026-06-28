#include "J2AFactory.h"
#include "JsonToAssetImporter.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

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
	if (FFileHelper::LoadFileToString(FileContent, *Filename))
	{
		TSharedPtr<FJsonValue> JsonValue;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

		if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid())
		{
			// FModel JSONs are typically exported as an array of objects
			if (JsonValue->Type == EJson::Array)
			{
				auto JsonArray = JsonValue->AsArray();
				if (JsonArray.Num() > 0 && JsonArray[0]->Type == EJson::Object)
				{
					auto FirstObject = JsonArray[0]->AsObject();
					// If the object has a "Type" field, it's an FModel asset JSON
					if (FirstObject->HasField(TEXT("Type")) || FirstObject->HasField(TEXT("Properties")))
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

UObject* UJ2AFactory::FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn)
{
	FString JsonString(BufferEnd - Buffer, Buffer);
	return FJsonToAssetImporter::ImportJsonToAsset(JsonString, InParent, InName, Flags);
}
