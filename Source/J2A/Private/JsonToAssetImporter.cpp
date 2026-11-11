#include "JsonToAssetImporter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/MessageDialog.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
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
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "StaticParameterSet.h"
#include "Factories/Texture2dFactoryNew.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "MaterialEditingLibrary.h"
#include "Misc/PackageName.h"

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
				
				UMaterialInterface* LoadedParent = LoadObject<UMaterialInterface>(nullptr, *ObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
				UMaterial* ParentMat = Cast<UMaterial>(LoadedParent);

				FString ParentClass;
				(*ParentField)->TryGetStringField(TEXT("ObjectName"), ParentClass);
				const FString ParentName = FPackageName::GetLongPackageAssetName(ObjectPath);

				// A missing parent instance can't be replaced by a dummy master at the same path
				if (!LoadedParent && ParentClass.StartsWith(TEXT("MaterialInstanceConstant")))
				{
					J2ANotifs::ShowNotification(FString::Printf(TEXT("MaterialInstance: Import parent instance '%s' first"), *ParentName));
				}
				// Auto-generate the parent Master Material if it doesn't exist, using the MI's parameters!
				else if (!LoadedParent)
				{
					J2ANotifs::ShowNotification(FString::Printf(TEXT("MaterialInstance: Missing Master dependency '%s'"), *ParentName));

					UPackage* ParentPackage = CreatePackage(*ObjectPath);
					ParentMat = NewObject<UMaterial>(ParentPackage, UMaterial::StaticClass(), *ParentName, RF_Public | RF_Standalone);
					FAssetRegistryModule::AssetCreated(ParentMat);
					LoadedParent = ParentMat;
				}

				// If the master material is empty, populate it with dummy nodes using the MI's properties!
				if (ParentMat && ParentMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Num() == 0)
				{
					PopulateDummyMasterMaterial(ParentMat, Props);
					ParentMat->MarkPackageDirty();
				}

				if (LoadedParent)
				{
					NewMI->SetParentEditorOnly(LoadedParent);

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

		const TSharedPtr<FJsonObject>* StaticParams;
		const TArray<TSharedPtr<FJsonValue>>* StaticSwitches = nullptr;
		if (Props->TryGetObjectField(TEXT("StaticParametersRuntime"), StaticParams) && StaticParams->IsValid()
			&& (*StaticParams)->TryGetArrayField(TEXT("StaticSwitchParameters"), StaticSwitches))
		{
			FStaticParameterSet StaticParameters;
			NewMI->GetStaticParameterValues(StaticParameters);

			for (auto& Elem : *StaticSwitches)
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				FName ParamName = FName(*ParamInfo->GetStringField(TEXT("Name")));

				for (FStaticSwitchParameter& Switch : StaticParameters.StaticSwitchParameters)
				{
					if (Switch.ParameterInfo.Name == ParamName)
					{
						Switch.Value = ParamObj->GetBoolField(TEXT("Value"));
						Switch.bOverride = true;
					}
				}
			}

			NewMI->UpdateStaticPermutation(StaticParameters);
		}
	}

	NewMI->PostEditChange();

	RefreshInstancesOfMaterial(NewMI, false);

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
		// Reuse a dummy already at this path so instances pointing at it stay linked
		const FString ExistingPath = InParent->GetPathName() + TEXT(".") + InName.ToString();
		NewMat = LoadObject<UMaterial>(nullptr, *ExistingPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	if (NewMat)
	{
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(NewMat);
	}
	else
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

	RefreshInstancesOfMaterial(NewMat);

	return NewMat;
}

int32 FJsonToAssetImporter::RefreshInstancesOfMaterial(UMaterialInterface* Parent, bool bNotifyIfNone)
{
	if (!Parent)
	{
		return 0;
	}

	const FString MasterPath = Parent->GetPathName();
	const FString MasterFolder = FPackageName::GetLongPackagePath(Parent->GetOutermost()->GetName());
	const FString MisplacedPath = MasterFolder + TEXT(".") + Parent->GetName();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> InstanceAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), InstanceAssets, true);

	int32 NumRefreshed = 0;
	for (const FAssetData& Data : InstanceAssets)
	{
		FString ParentTag;
		if (!Data.GetTagValue(TEXT("Parent"), ParentTag))
		{
			continue;
		}

		const FString ParentPath = FPackageName::ExportTextPathToObjectPath(ParentTag);
		const bool bMisplaced = ParentPath == MisplacedPath;
		if (!bMisplaced && ParentPath != MasterPath)
		{
			continue;
		}

		UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Data.GetAsset());
		if (!Instance || Instance == Parent)
		{
			continue;
		}

		if (bMisplaced)
		{
			Instance->SetParentEditorOnly(Parent);
		}

		UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
		Instance->MarkPackageDirty();
		++NumRefreshed;
	}

	if (NumRefreshed > 0 || bNotifyIfNone)
	{
		J2ANotifs::ShowNotification(FString::Printf(TEXT("MasterMaterials: Refreshed %d instance(s) of '%s'"), NumRefreshed, *Parent->GetName()));
	}
	return NumRefreshed;
}

bool FJsonToAssetImporter::RelinkInstanceParent(UMaterialInstanceConstant* Instance)
{
	if (!Instance || !Instance->Parent)
	{
		return false;
	}

	// A misplaced dummy lives at /Folder.Name, the real parent at /Folder/Name.Name
	UMaterialInterface* CurrentParent = Instance->Parent;
	const FString ParentName = CurrentParent->GetName();
	const FString ExpectedPath = CurrentParent->GetOutermost()->GetName() / ParentName + TEXT(".") + ParentName;

	if (UMaterialInterface* RealParent = LoadObject<UMaterialInterface>(nullptr, *ExpectedPath, nullptr, LOAD_Quiet | LOAD_NoWarn))
	{
		Instance->SetParentEditorOnly(RealParent);
		J2ANotifs::ShowNotification(FString::Printf(TEXT("MaterialInstance: Relinked '%s' to '%s'"), *Instance->GetName(), *ExpectedPath));
	}

	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->MarkPackageDirty();
	return true;
}

void FJsonToAssetImporter::PopulateDummyMasterMaterial(UMaterial* NewMat, TSharedPtr<FJsonObject> Props, TSharedPtr<FJsonObject> CachedExpressionData)
{
	int32 NodeY = 0;
	TArray<UMaterialExpression*> FinalNodes;

	// Switches need both inputs connected to compile, so they share one constant
	UMaterialExpressionConstant* SwitchInput = nullptr;
	auto AddStaticSwitchNode = [&](FName ParamName, bool bDefaultValue)
	{
		if (!SwitchInput)
		{
			SwitchInput = NewObject<UMaterialExpressionConstant>(NewMat);
			SwitchInput->MaterialExpressionEditorX = -250;
			SwitchInput->MaterialExpressionEditorY = NodeY;
			NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(SwitchInput);
		}

		UMaterialExpressionStaticSwitchParameter* Node = NewObject<UMaterialExpressionStaticSwitchParameter>(NewMat);
		Node->ParameterName = ParamName;
		Node->DefaultValue = bDefaultValue;
		Node->A.Expression = SwitchInput;
		Node->B.Expression = SwitchInput;
		Node->MaterialExpressionEditorY = NodeY;
		NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Node);
		FinalNodes.Add(Node);
		NodeY += 150;
	};

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

		// A dummy master needs the instance's switches or the overrides have nothing to apply to
		const TSharedPtr<FJsonObject>* StaticParams;
		const TArray<TSharedPtr<FJsonValue>>* StaticSwitches = nullptr;
		if (Props->TryGetObjectField(TEXT("StaticParametersRuntime"), StaticParams) && StaticParams->IsValid()
			&& (*StaticParams)->TryGetArrayField(TEXT("StaticSwitchParameters"), StaticSwitches))
		{
			for (auto& Elem : *StaticSwitches)
			{
				auto ParamObj = Elem->AsObject();
				auto ParamInfo = ParamObj->GetObjectField(TEXT("ParameterInfo"));
				AddStaticSwitchNode(FName(*ParamInfo->GetStringField(TEXT("Name"))), false);
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

		const TArray<TSharedPtr<FJsonValue>>* StaticSwitchValues = nullptr;
		CachedExpressionData->TryGetArrayField(TEXT("StaticSwitchValues"), StaticSwitchValues);

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

		TryAddParameters(TEXT("RuntimeEntries[7]"), StaticSwitchValues, [&](FName ParamName, TSharedPtr<FJsonValue> ValObj) {
			AddStaticSwitchNode(ParamName, ValObj.IsValid() && ValObj->Type == EJson::Boolean && ValObj->AsBool());
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
