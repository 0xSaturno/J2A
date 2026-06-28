#include "JsonToAssetImporter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/MessageDialog.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedStruct.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Factories/Texture2dFactoryNew.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace J2ANotifs
{
	static void ShowNotification(const FString& Message)
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}


bool FJsonToAssetImporter::UpdateExistingAssetFromJson(UObject* ExistingAsset, const FString& JsonString)
{
	TSharedPtr<FJsonValue> JsonValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonValue) || !JsonValue.IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject = nullptr;
	if (JsonValue->Type == EJson::Array)
	{
		auto JsonArray = JsonValue->AsArray();
		if (JsonArray.Num() > 0 && JsonArray[0]->Type == EJson::Object)
		{
			RootObject = JsonArray[0]->AsObject();
		}
	}
	else if (JsonValue->Type == EJson::Object)
	{
		RootObject = JsonValue->AsObject();
	}

	if (!RootObject.IsValid() || !ExistingAsset)
	{
		return false;
	}

	if (UMaterialInstanceConstant* ExistingMI = Cast<UMaterialInstanceConstant>(ExistingAsset))
	{
		ImportMaterialInstance(RootObject, nullptr, NAME_None, RF_NoFlags, ExistingMI);
		return true;
	}
	else if (UMaterial* ExistingMat = Cast<UMaterial>(ExistingAsset))
	{
		ImportMasterMaterial(RootObject, nullptr, NAME_None, RF_NoFlags, ExistingMat);
		return true;
	}

	return false;
}

UObject* FJsonToAssetImporter::ImportJsonToAsset(const FString& JsonString, UObject* InParent, FName InName, EObjectFlags Flags)
{
	TSharedPtr<FJsonValue> JsonValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonValue) || !JsonValue.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("J2A: Failed to parse JSON string."));
		return nullptr;
	}

	TSharedPtr<FJsonObject> RootObject = nullptr;

	if (JsonValue->Type == EJson::Array)
	{
		auto JsonArray = JsonValue->AsArray();
		if (JsonArray.Num() > 0 && JsonArray[0]->Type == EJson::Object)
		{
			RootObject = JsonArray[0]->AsObject();
		}
	}
	else if (JsonValue->Type == EJson::Object)
	{
		RootObject = JsonValue->AsObject();
	}

	if (!RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("J2A: JSON root is not a valid object."));
		return nullptr;
	}

	FString AssetType;
	if (!RootObject->TryGetStringField(TEXT("Type"), AssetType))
	{
		UE_LOG(LogTemp, Warning, TEXT("J2A: Missing 'Type' field in JSON. Treating as DataAsset."));
		AssetType = TEXT("DataAsset");
	}

	if (AssetType == TEXT("DataTable"))
	{
		return ImportDataTable(RootObject, InParent, InName, Flags);
	}
	else if (AssetType == TEXT("MaterialInstanceConstant"))
	{
		return ImportMaterialInstance(RootObject, InParent, InName, Flags);
	}
	else if (AssetType == TEXT("Material"))
	{
		return ImportMasterMaterial(RootObject, InParent, InName, Flags);
	}
	else
	{
		return ImportDataAsset(RootObject, InParent, InName, Flags);
	}
}

UObject* FJsonToAssetImporter::ImportDataTable(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags)
{
	FString StructName;
	auto Props = JsonObject->GetObjectField(TEXT("Properties"));
	if (Props.IsValid() && Props->HasField(TEXT("RowStruct")))
	{
		auto RowStructObj = Props->GetObjectField(TEXT("RowStruct"));
		FString ObjectName = RowStructObj->GetStringField(TEXT("ObjectName"));
		int32 TickPos;
		if (ObjectName.FindChar('\'', TickPos))
		{
			StructName = ObjectName.Mid(TickPos + 1);
			StructName.RemoveFromEnd(TEXT("'"));
		}
	}

	UScriptStruct* RowStruct = nullptr;
	if (!StructName.IsEmpty())
	{
		RowStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *StructName);
		if (!RowStruct)
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> AssetData;
			AssetRegistryModule.Get().GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), AssetData, true);

			for (const FAssetData& Data : AssetData)
			{
				if (Data.AssetName.ToString() == StructName)
				{
					RowStruct = Cast<UScriptStruct>(Data.GetAsset());
					break;
				}
			}
		}
	}

	if (!RowStruct)
	{
		UE_LOG(LogTemp, Error, TEXT("J2A: Could not find RowStruct %s for DataTable."), *StructName);
		return nullptr;
	}

	UDataTable* NewDT = NewObject<UDataTable>(InParent, UDataTable::StaticClass(), InName, Flags);
	NewDT->RowStruct = RowStruct;

	auto RowsObj = JsonObject->GetObjectField(TEXT("Rows"));
	if (RowsObj.IsValid())
	{
		FString RowsJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RowsJson);
		FJsonSerializer::Serialize(RowsObj.ToSharedRef(), Writer);

		TArray<FString> Problems;
		NewDT->CreateTableFromJSONString(RowsJson);
	}

	return NewDT;
}

UObject* FJsonToAssetImporter::ImportMaterialInstance(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags, UMaterialInstanceConstant* ExistingMI)
{
	UMaterialInstanceConstant* NewMI = ExistingMI;
	if (!NewMI)
	{
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		NewMI = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), InParent, InName, Flags, nullptr, GWarn));
	}

	auto Props = JsonObject->GetObjectField(TEXT("Properties"));
	if (Props.IsValid())
	{
		const TSharedPtr<FJsonObject>* ParentField;
		if (Props->TryGetObjectField(TEXT("Parent"), ParentField))
		{
			FString ObjectPath;
			if ((*ParentField)->TryGetStringField(TEXT("ObjectPath"), ObjectPath))
			{
				int32 DotPos;
				if (ObjectPath.FindChar('.', DotPos))
				{
					ObjectPath = ObjectPath.Left(DotPos);
				}
				
				UMaterial* ParentMat = LoadObject<UMaterial>(nullptr, *ObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
				
				// Auto-generate the parent Master Material if it doesn't exist, using the MI's parameters!
				if (!ParentMat)
				{
					FString ParentName = FPaths::GetBaseFilename(ObjectPath);
					J2ANotifs::ShowNotification(FString::Printf(TEXT("MaterialInstance: Missing Master dependency '%s'"), *ParentName));
					
					FString ParentPackagePath = FPaths::GetPath(ObjectPath);
					UPackage* ParentPackage = CreatePackage(*ParentPackagePath);
					ParentMat = NewObject<UMaterial>(ParentPackage, UMaterial::StaticClass(), *ParentName, RF_Public | RF_Standalone);
					FAssetRegistryModule::AssetCreated(ParentMat);
				}

				if (ParentMat)
				{
					// If the master material is empty, populate it with dummy nodes using the MI's properties!
					if (ParentMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Num() == 0)
					{
						PopulateDummyMasterMaterial(ParentMat, Props);
						ParentMat->MarkPackageDirty();
					}
					
					NewMI->SetParentEditorOnly(ParentMat);
					
					// Force the MI to update its parameter cache so the overrides will actually stick!
					NewMI->PreEditChange(nullptr);
					NewMI->PostEditChange();
				}
			}
		}

		if (Props->HasField(TEXT("ScalarParameterValues")))
		{
			for (auto& Elem : Props->GetArrayField(TEXT("ScalarParameterValues")))
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				FName ParamName = FName(*ParamInfo->GetStringField(TEXT("Name")));
				float ParamValue = ParamObj->GetNumberField(TEXT("ParameterValue"));
				NewMI->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(ParamName), ParamValue);
			}
		}

		if (Props->HasField(TEXT("VectorParameterValues")))
		{
			for (auto& Elem : Props->GetArrayField(TEXT("VectorParameterValues")))
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				FName ParamName = FName(*ParamInfo->GetStringField(TEXT("Name")));
				auto ValObj = ParamObj->GetObjectField(TEXT("ParameterValue"));
				FLinearColor Color(
					ValObj->GetNumberField(TEXT("R")),
					ValObj->GetNumberField(TEXT("G")),
					ValObj->GetNumberField(TEXT("B")),
					ValObj->GetNumberField(TEXT("A"))
				);
				NewMI->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(ParamName), Color);
			}
		}

		if (Props->HasField(TEXT("TextureParameterValues")))
		{
			for (auto& Elem : Props->GetArrayField(TEXT("TextureParameterValues")))
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				FName ParamName = FName(*ParamInfo->GetStringField(TEXT("Name")));
				
				const TSharedPtr<FJsonObject>* ValObj;
				if (ParamObj->TryGetObjectField(TEXT("ParameterValue"), ValObj) && ValObj->IsValid())
				{
					FString ObjectPath;
					if ((*ValObj)->TryGetStringField(TEXT("ObjectPath"), ObjectPath))
					{
						UTexture2D* Tex = TryCreateDummyTexture(ObjectPath);
						if (Tex)
						{
							NewMI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamName), Tex);
						}
					}
				}
			}
		}
	}
	
	NewMI->PostEditChange();

	return NewMI;
}

UObject* FJsonToAssetImporter::ImportDataAsset(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags)
{
	FString AssetType = JsonObject->GetStringField(TEXT("Type"));
	UClass* AssetClass = FindObject<UClass>(ANY_PACKAGE, *AssetType);
	
	if (!AssetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("J2A: Could not find class %s for DataAsset."), *AssetType);
		return nullptr;
	}

	UObject* NewAsset = NewObject<UObject>(InParent, AssetClass, InName, Flags);

	auto Props = JsonObject->GetObjectField(TEXT("Properties"));
	if (Props.IsValid())
	{
		FJsonObjectConverter::JsonObjectToUStruct(Props.ToSharedRef(), AssetClass, NewAsset, 0, 0);
	}

	return NewAsset;
}

UObject* FJsonToAssetImporter::ImportMasterMaterial(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags, UMaterial* ExistingMat)
{
	UMaterial* NewMat = ExistingMat;
	if (!NewMat)
	{
		NewMat = NewObject<UMaterial>(InParent, UMaterial::StaticClass(), InName, Flags);
	}
	
	auto Props = JsonObject->GetObjectField(TEXT("Properties"));
	
	TSharedPtr<FJsonObject> CachedExpressionData;
	const TSharedPtr<FJsonObject>* CachedObjPtr;
	if (JsonObject->TryGetObjectField(TEXT("CachedExpressionData"), CachedObjPtr) && CachedObjPtr->IsValid())
	{
		CachedExpressionData = *CachedObjPtr;
	}

	if (Props.IsValid() || CachedExpressionData.IsValid())
	{
		PopulateDummyMasterMaterial(NewMat, Props, CachedExpressionData);
	}

	return NewMat;
}

void FJsonToAssetImporter::PopulateDummyMasterMaterial(UMaterial* NewMat, TSharedPtr<FJsonObject> Props, TSharedPtr<FJsonObject> CachedExpressionData)
{
	int32 NodeY = 0;
	TArray<UMaterialExpression*> FinalNodes;

	if (Props.IsValid())
	{
		if (Props->HasField(TEXT("ScalarParameterValues")))
		{
			for (auto& Elem : Props->GetArrayField(TEXT("ScalarParameterValues")))
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				FName ParamName = FName(*ParamInfo->GetStringField(TEXT("Name")));
				float ParamValue = ParamObj->GetNumberField(TEXT("ParameterValue"));
				
				UMaterialExpressionScalarParameter* Node = NewObject<UMaterialExpressionScalarParameter>(NewMat);
				Node->ParameterName = ParamName;
				Node->DefaultValue = ParamValue;
				Node->MaterialExpressionEditorY = NodeY;
				NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Node);
				FinalNodes.Add(Node);
				NodeY += 150;
			}
		}

		if (Props->HasField(TEXT("VectorParameterValues")))
		{
			for (auto& Elem : Props->GetArrayField(TEXT("VectorParameterValues")))
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				FName ParamName = FName(*ParamInfo->GetStringField(TEXT("Name")));
				auto ValObj = ParamObj->GetObjectField(TEXT("ParameterValue"));
				
				UMaterialExpressionVectorParameter* Node = NewObject<UMaterialExpressionVectorParameter>(NewMat);
				Node->ParameterName = ParamName;
				Node->DefaultValue = FLinearColor(
					ValObj->GetNumberField(TEXT("R")),
					ValObj->GetNumberField(TEXT("G")),
					ValObj->GetNumberField(TEXT("B")),
					ValObj->GetNumberField(TEXT("A"))
				);
				Node->MaterialExpressionEditorY = NodeY;
				NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Node);
				FinalNodes.Add(Node);
				NodeY += 150;
			}
		}

		if (Props->HasField(TEXT("TextureParameterValues")))
		{
			for (auto& Elem : Props->GetArrayField(TEXT("TextureParameterValues")))
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				FName ParamName = FName(*ParamInfo->GetStringField(TEXT("Name")));
				
				UMaterialExpressionTextureSampleParameter2D* Node = NewObject<UMaterialExpressionTextureSampleParameter2D>(NewMat);
				Node->ParameterName = ParamName;
				Node->MaterialExpressionEditorY = NodeY;
				
				const TSharedPtr<FJsonObject>* ValObj;
				if (ParamObj->TryGetObjectField(TEXT("ParameterValue"), ValObj) && ValObj->IsValid())
				{
					FString ObjectPath;
					if ((*ValObj)->TryGetStringField(TEXT("ObjectPath"), ObjectPath))
					{
						UTexture2D* Tex = TryCreateDummyTexture(ObjectPath);
						if (Tex)
						{
							Node->Texture = Tex;
						}
					}
				}
				NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Node);
				FinalNodes.Add(Node);
				NodeY += 150;
			}
		}
	}

	if (CachedExpressionData.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ScalarValues = nullptr;
		CachedExpressionData->TryGetArrayField(TEXT("ScalarValues"), ScalarValues);

		const TArray<TSharedPtr<FJsonValue>>* VectorValues = nullptr;
		CachedExpressionData->TryGetArrayField(TEXT("VectorValues"), VectorValues);

		const TArray<TSharedPtr<FJsonValue>>* TextureValues = nullptr;
		CachedExpressionData->TryGetArrayField(TEXT("TextureValues"), TextureValues);

		auto TryAddParameters = [&](const FString& KeyName, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, auto CreateNodeLambda)
		{
			if (CachedExpressionData->HasField(KeyName))
			{
				auto Entry = CachedExpressionData->GetObjectField(KeyName);
				const TArray<TSharedPtr<FJsonValue>>* ParamSet;
				if (Entry->TryGetArrayField(TEXT("ParameterInfoSet"), ParamSet))
				{
					for (int32 i = 0; i < ParamSet->Num(); ++i)
					{
						auto ParamObj = (*ParamSet)[i]->AsObject();
						FName ParamName = FName(*ParamObj->GetStringField(TEXT("Name")));
						TSharedPtr<FJsonValue> ValObj = (ValuesArray && ValuesArray->IsValidIndex(i)) ? (*ValuesArray)[i] : nullptr;
						CreateNodeLambda(ParamName, ValObj);
					}
				}
			}
		};

		TryAddParameters(TEXT("RuntimeEntries"), ScalarValues, [&](FName ParamName, TSharedPtr<FJsonValue> ValObj) {
			UMaterialExpressionScalarParameter* Node = NewObject<UMaterialExpressionScalarParameter>(NewMat);
			Node->ParameterName = ParamName;
			Node->DefaultValue = ValObj.IsValid() ? ValObj->AsNumber() : 0.0f;
			Node->MaterialExpressionEditorY = NodeY;
			NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Node);
			FinalNodes.Add(Node);
			NodeY += 150;
		});

		TryAddParameters(TEXT("RuntimeEntries[1]"), VectorValues, [&](FName ParamName, TSharedPtr<FJsonValue> ValObj) {
			UMaterialExpressionVectorParameter* Node = NewObject<UMaterialExpressionVectorParameter>(NewMat);
			Node->ParameterName = ParamName;
			if (ValObj.IsValid() && ValObj->Type == EJson::Object)
			{
				auto ColorObj = ValObj->AsObject();
				Node->DefaultValue = FLinearColor(
					ColorObj->GetNumberField(TEXT("R")),
					ColorObj->GetNumberField(TEXT("G")),
					ColorObj->GetNumberField(TEXT("B")),
					ColorObj->GetNumberField(TEXT("A"))
				);
			}
			Node->MaterialExpressionEditorY = NodeY;
			NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Node);
			FinalNodes.Add(Node);
			NodeY += 150;
		});

		TryAddParameters(TEXT("RuntimeEntries[3]"), TextureValues, [&](FName ParamName, TSharedPtr<FJsonValue> ValObj) {
			UMaterialExpressionTextureSampleParameter2D* Node = NewObject<UMaterialExpressionTextureSampleParameter2D>(NewMat);
			Node->ParameterName = ParamName;
			Node->MaterialExpressionEditorY = NodeY;
			
			if (ValObj.IsValid() && ValObj->Type == EJson::Object)
			{
				FString AssetPathName;
				if (ValObj->AsObject()->TryGetStringField(TEXT("AssetPathName"), AssetPathName) && !AssetPathName.IsEmpty() && AssetPathName != TEXT("None"))
				{
					UTexture2D* Tex = TryCreateDummyTexture(AssetPathName);
					if (Tex)
					{
						Node->Texture = Tex;
					}
				}
			}
			NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Node);
			FinalNodes.Add(Node);
			NodeY += 150;
		});
	}

	int32 AddY = 0;
	while (FinalNodes.Num() > 1)
	{
		UMaterialExpression* NodeA = FinalNodes[0];
		UMaterialExpression* NodeB = FinalNodes[1];
		FinalNodes.RemoveAt(0, 2);

		UMaterialExpressionAdd* AddNode = NewObject<UMaterialExpressionAdd>(NewMat);
		AddNode->MaterialExpressionEditorX = 250;
		AddNode->MaterialExpressionEditorY = AddY;
		AddY += 150;
		
		AddNode->A.Expression = NodeA;
		AddNode->B.Expression = NodeB;
		
		NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(AddNode);
		FinalNodes.Add(AddNode);
	}

	if (FinalNodes.Num() == 1)
	{
		NewMat->GetEditorOnlyData()->BaseColor.Expression = FinalNodes[0];
	}

	NewMat->PreEditChange(nullptr);
	NewMat->PostEditChange();
}

UTexture2D* FJsonToAssetImporter::TryCreateDummyTexture(const FString& InObjectPath)
{
	FString ObjectPath = InObjectPath;
	int32 DotPos;
	if (ObjectPath.FindChar('.', DotPos))
	{
		ObjectPath = ObjectPath.Left(DotPos);
	}

	UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr, *ObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (ExistingTexture)
	{
		return ExistingTexture;
	}

	FString PackageName = ObjectPath;
	FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	
	UTexture2DFactoryNew* Factory = NewObject<UTexture2DFactoryNew>();
	
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UTexture2D::StaticClass(), Factory);
	if (NewAsset)
	{
		J2ANotifs::ShowNotification(FString::Printf(TEXT("MasterMaterials: Created missing texture '%s'"), *AssetName));
		
		UPackage* Package = NewAsset->GetOutermost();
		Package->MarkPackageDirty();
		return Cast<UTexture2D>(NewAsset);
	}

	return nullptr;
}
