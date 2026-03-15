// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLayoutExtender;

/** Editor module for WorldSweep — registers the docked tab with the level editor, Tools menu entry, and message log listing. */
class WORLDSWEEP_API FWorldSweepModule : public IModuleInterface
{
    //~ Begin IModuleInterface Interface
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    //~ End IModuleInterface Interface

    static const FName WorldSweepTabName;

private:

    void RegisterMenus();
    void RegisterWorldSweepTabs(TSharedPtr<FTabManager> InTabManager);
    void RegisterWorldSweepLayout(FLayoutExtender& InExtender);
};
