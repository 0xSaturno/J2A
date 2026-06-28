#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "J2AFactory.generated.h"

/**
 * UJ2AFactory overrides the default JSON factory for FModel exported JSON files.
 * It detects the "Type" field in the FModel JSON array and automatically generates the correct Asset.
 */
UCLASS()
class J2A_API UJ2AFactory : public UFactory
{
	GENERATED_BODY()

public:
	UJ2AFactory();

	//~ Begin UFactory Interface
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn) override;
	//~ End UFactory Interface
};
