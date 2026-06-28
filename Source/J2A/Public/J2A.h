#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FJ2AModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OnImportJsonClicked();
	void OnUpdateAssetClicked();
};
