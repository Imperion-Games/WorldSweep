// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class UPackage;

/** RAII helper that tracks packages dirtied during a WorldSweep and saves them with source-control checkout. Registers the package-dirty listener in the constructor and removes it in the destructor, so early returns cannot leak the delegate. Reports the tracked packages to the GC as strong references so they survive DoCollectGarbage() calls between cells. */
class WORLDSWEEP_API FScopedWorldSweepSaveTracker : public FGCObject
{
public:

    /** InEnabled false makes SaveDirty() a no-op and skips listener registration. Used when the batch has bSaveModifications = false so the tracker adds no overhead. */
    explicit FScopedWorldSweepSaveTracker(bool InEnabled);
    virtual ~FScopedWorldSweepSaveTracker();

    /** Save and check out any packages dirtied since construction. Safe to call multiple times: only packages that are still dirty are saved. */
    void SaveDirty();

    //~ Begin FGCObject Interface
    virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
    virtual FString GetReferencerName() const override;
    //~ End FGCObject Interface

private:

    TArray<TObjectPtr<UPackage>> DirtyPackages;
    TSet<FName> PreExistingDirtyNames;
    FDelegateHandle DirtyHandle;
    bool bEnabled;
};
