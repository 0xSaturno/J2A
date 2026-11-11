#include "JsonToAssetImporter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/DataAsset.h"
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


// J2A dummies hold only parameters, the Adds chaining them and a constant. Anything else is a hand-built material we must not touch
static bool IsDummyMaster(const UMaterial* Material)
{
	for (const UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (Expression && !Expression->HasAParameterName() && !Expression->IsA<UMaterialExpressionAdd>() && !Expression->IsA<UMaterialExpressionConstant>())
		{
			return false;
		}
	}
	return true;
}

TSharedPtr<FJsonObject> FJsonToAssetImporter::ParseRootObject(const FString& JsonString)
{
	TSharedPtr<FJsonValue> JsonValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonValue) || !JsonValue.IsValid())
	{
		return nullptr;
	}

	// FModel exports a list of objects, the asset itself comes first
	if (JsonValue->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& JsonArray = JsonValue->AsArray();
		return (JsonArray.Num() > 0 && JsonArray[0]->Type == EJson::Object) ? JsonArray[0]->AsObject() : nullptr;
	}
	return JsonValue->Type == EJson::Object ? JsonValue->AsObject() : nullptr;
}

UClass* FJsonToAssetImporter::ResolveAssetClass(const FString& AssetType)
{
	if (AssetType == TEXT("DataTable"))
	{
		return UDataTable::StaticClass();
	}
	if (AssetType == TEXT("MaterialInstanceConstant"))
	{
		return UMaterialInstanceConstant::StaticClass();
	}
	if (AssetType == TEXT("Material"))
	{
		return UMaterial::StaticClass();
	}

	// Anything else must be a DataAsset: building other classes from JSON (meshes, textures, blueprints) leaves a hollow asset that can crash the editor
	UClass* AssetClass = AssetType.IsEmpty() ? nullptr : FindFirstObject<UClass>(*AssetType, EFindFirstObjectOptions::NativeFirst);
	return (AssetClass && AssetClass->IsChildOf(UDataAsset::StaticClass()) && !AssetClass->HasAnyClassFlags(CLASS_Abstract)) ? AssetClass : nullptr;
}

bool FJsonToAssetImporter::CanImportJson(const FString& JsonString)
{
	const TSharedPtr<FJsonObject> RootObject = ParseRootObject(JsonString);
	FString AssetType;
	return RootObject.IsValid() && RootObject->TryGetStringField(TEXT("Type"), AssetType) && ResolveAssetClass(AssetType) != nullptr;
}

bool FJsonToAssetImporter::UpdateExistingAssetFromJson(UObject* ExistingAsset, const FString& JsonString)
{
	const TSharedPtr<FJsonObject> RootObject = ParseRootObject(JsonString);

	if (!RootObject.IsValid() || !ExistingAsset)
	{
		return false;
	}

	// The wrong kind of JSON would rebuild the asset from unrelated data, e.g. wipe a master's graph
	FString JsonType;
	RootObject->TryGetStringField(TEXT("Type"), JsonType);
	if (JsonType != ExistingAsset->GetClass()->GetName())
	{
		J2ANotifs::ShowNotification(FString::Printf(TEXT("Update from JSON: skipped '%s', the JSON is a '%s'"), *ExistingAsset->GetName(), *JsonType));
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
	const TSharedPtr<FJsonObject> RootObject = ParseRootObject(JsonString);
	if (!RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("J2A: Failed to parse JSON, or its root is not an object."));
		return nullptr;
	}

	FString AssetType;
	RootObject->TryGetStringField(TEXT("Type"), AssetType);
	UClass* AssetClass = ResolveAssetClass(AssetType);
	if (!AssetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("J2A: '%s' is not a supported type or a loaded DataAsset class."), *AssetType);
		return nullptr;
	}

	// NewObject is fatal when the name is already taken by an asset of another class
	UObject* Existing = StaticFindObject(nullptr, InParent, *InName.ToString());
	if (Existing && !Existing->IsA(AssetClass))
	{
		J2ANotifs::ShowNotification(FString::Printf(TEXT("Import: skipped '%s', it already exists here as a %s"), *InName.ToString(), *Existing->GetClass()->GetName()));
		return nullptr;
	}

	if (AssetClass == UDataTable::StaticClass())
	{
		return ImportDataTable(RootObject, InParent, InName, Flags);
	}
	else if (AssetClass == UMaterialInstanceConstant::StaticClass())
	{
		return ImportMaterialInstance(RootObject, InParent, InName, Flags);
	}
	else if (AssetClass == UMaterial::StaticClass())
	{
		return ImportMasterMaterial(RootObject, InParent, InName, Flags);
	}
	else
	{
		return ImportDataAsset(RootObject, AssetClass, InParent, InName, Flags);
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
		RowStruct = FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::NativeFirst);
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

	const TSharedPtr<FJsonObject>* RowsObj;
	if (JsonObject->TryGetObjectField(TEXT("Rows"), RowsObj))
	{
		// FModel keys rows by name, Unreal wants a list of rows that each carry their name.
		// A private key field keeps row structs with their own "Name" column intact.
		const FString KeyField = TEXT("J2ARowName");
		TArray<TSharedPtr<FJsonValue>> RowList;
		for (const auto& Row : (*RowsObj)->Values)
		{
			const TSharedPtr<FJsonObject>* RowObj;
			if (Row.Value.IsValid() && Row.Value->TryGetObject(RowObj))
			{
				(*RowObj)->SetStringField(KeyField, Row.Key);
				RowList.Add(Row.Value);
			}
		}

		FString RowsJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RowsJson);
		FJsonSerializer::Serialize(RowList, Writer);

		NewDT->ImportKeyField = KeyField;
		const TArray<FString> Problems = NewDT->CreateTableFromJSONString(RowsJson);
		NewDT->ImportKeyField.Empty();

		for (const FString& Problem : Problems)
		{
			UE_LOG(LogTemp, Warning, TEXT("J2A: DataTable %s: %s"), *InName.ToString(), *Problem);
		}
		if (Problems.Num() > 0)
		{
			J2ANotifs::ShowNotification(FString::Printf(TEXT("DataTable: '%s' imported with %d problem(s), see Output Log"), *InName.ToString(), Problems.Num()));
		}
	}

	return NewDT;
}

UObject* FJsonToAssetImporter::ImportMaterialInstance(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags, UMaterialInstanceConstant* ExistingMI)
{
	// A brand new instance has no children to refresh; one we update or replace may
	const bool bMayHaveChildren = ExistingMI || FindObject<UMaterialInstanceConstant>(InParent, *InName.ToString());

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

				// Give a dummy master every parameter this instance uses; later instances add the ones it still lacks
				if (ParentMat && IsDummyMaster(ParentMat))
				{
					PopulateDummyMasterMaterial(ParentMat, Props);
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

	if (bMayHaveChildren)
	{
		RefreshInstancesOfMaterial(NewMI, false);
	}

	return NewMI;
}

UObject* FJsonToAssetImporter::ImportDataAsset(TSharedPtr<FJsonObject> JsonObject, UClass* AssetClass, UObject* InParent, FName InName, EObjectFlags Flags)
{
	UObject* NewAsset = NewObject<UObject>(InParent, AssetClass, InName, Flags);

	// The converter stops at the first field it cannot read, so the fields after it keep their defaults
	auto Props = JsonObject->GetObjectField(TEXT("Properties"));
	if (Props.IsValid() && !FJsonObjectConverter::JsonObjectToUStruct(Props.ToSharedRef(), AssetClass, NewAsset, 0, 0))
	{
		J2ANotifs::ShowNotification(FString::Printf(TEXT("DataAsset: '%s' is only partly filled, a field could not be read (see Output Log)"), *InName.ToString()));
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
	auto& Expressions = NewMat->GetEditorOnlyData()->ExpressionCollection.Expressions;
	const int32 NumExisting = Expressions.Num();

	// Place new nodes below what a populated dummy already has
	int32 NodeY = 150 * NumExisting;
	TArray<UMaterialExpression*> FinalNodes;

	// When merging into a populated dummy, keep what already feeds BaseColor and skip parameters it has
	UMaterialExpression* OldOutput = NewMat->GetEditorOnlyData()->BaseColor.Expression;
	if (OldOutput && Expressions.Contains(OldOutput))
	{
		FinalNodes.Add(OldOutput);
	}
	const int32 NumSeeded = FinalNodes.Num();

	TSet<FName> KnownParams;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (Expression && Expression->HasAParameterName())
		{
			KnownParams.Add(Expression->GetParameterName());
		}
	}

	// Switches need both inputs connected to compile, so they share one constant
	UMaterialExpressionConstant* SwitchInput = nullptr;
	auto AddStaticSwitchNode = [&](FName ParamName, bool bDefaultValue)
	{
		if (KnownParams.Contains(ParamName))
		{
			return;
		}

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
				
				if (KnownParams.Contains(ParamName))
				{
					continue;
				}

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
				
				if (KnownParams.Contains(ParamName))
				{
					continue;
				}

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
				
				if (KnownParams.Contains(ParamName))
				{
					continue;
				}

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
						if (KnownParams.Contains(ParamName))
						{
							continue;
						}
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

	// The dummy already had everything, so leave it alone and skip a shader recompile
	if (NumExisting > 0 && FinalNodes.Num() == NumSeeded)
	{
		return;
	}

	int32 AddY = 150 * NumExisting;
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

	// A normal map on the default Color sampler fails to compile
	for (UMaterialExpression* Expression : Expressions)
	{
		if (UMaterialExpressionTextureBase* TextureNode = Cast<UMaterialExpressionTextureBase>(Expression))
		{
			TextureNode->AutoSetSampleType();
		}
	}

	NewMat->PreEditChange(nullptr);
	NewMat->PostEditChange();
	NewMat->MarkPackageDirty();
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
