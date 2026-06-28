#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceConstant;
class UMaterial;
class UTexture2D;

class J2A_API FJsonToAssetImporter
{
public:
	/**
	 * Parses an FModel JSON string and updates an existing UObject.
	 */
	static bool UpdateExistingAssetFromJson(UObject* ExistingAsset, const FString& JsonString);

	/**
	 * Parses an FModel JSON string and generates the appropriate UObject.
	 */
	static UObject* ImportJsonToAsset(const FString& JsonString, UObject* InParent, FName InName, EObjectFlags Flags);

private:
	// Specific asset importers
	static UObject* ImportDataTable(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags);
	static UObject* ImportMaterialInstance(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags, UMaterialInstanceConstant* ExistingMI = nullptr);
	static UObject* ImportDataAsset(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags);
	static UObject* ImportMasterMaterial(TSharedPtr<FJsonObject> JsonObject, UObject* InParent, FName InName, EObjectFlags Flags, UMaterial* ExistingMat = nullptr);

	// Helper for generating dummy nodes
	static void PopulateDummyMasterMaterial(UMaterial* NewMat, TSharedPtr<FJsonObject> Props, TSharedPtr<FJsonObject> CachedExpressionData = nullptr);
	
	// Helper to create dummy texture if it doesn't exist
	static UTexture2D* TryCreateDummyTexture(const FString& InObjectPath);
};
