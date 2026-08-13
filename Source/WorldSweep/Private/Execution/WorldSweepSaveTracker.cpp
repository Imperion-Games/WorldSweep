// Copyright © ToaGames. All Rights Reserved.

#include "Execution/WorldSweepSaveTracker.h"

#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "Misc/PackageName.h"
#include "SourceControlHelpers.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

#include "WorldSweepLog.h"

namespace
{
    /** Check out (or mark for add) InFiles when a source control provider is active. No-op otherwise. */
    void CheckOutIfSourceControlled(const TArray<FString>& InFiles)
    {
        if (InFiles.IsEmpty() || !ISourceControlModule::Get().IsEnabled())
        {
            return;
        }

        SourceControlHelpers::CheckOutOrAddFiles(InFiles);
    }
}

FScopedWorldSweepSaveTracker::FScopedWorldSweepSaveTracker(bool InEnabled)
    : bEnabled(InEnabled)
{
    if (!bEnabled)
    {
        return;
    }

    for (TObjectIterator<UPackage> It; It; ++It)
    {
        if (It->IsDirty())
        {
            PreExistingDirtyNames.Add(It->GetFName());
        }
    }

    DirtyHandle = UPackage::PackageDirtyStateChangedEvent.AddLambda(
        [this](UPackage* InPackage)
        {
            if (InPackage && InPackage->IsDirty() && !PreExistingDirtyNames.Contains(InPackage->GetFName()))
            {
                DirtyPackages.AddUnique(InPackage);
            }
        });
}

FScopedWorldSweepSaveTracker::~FScopedWorldSweepSaveTracker()
{
    if (DirtyHandle.IsValid())
    {
        UPackage::PackageDirtyStateChangedEvent.Remove(DirtyHandle);
        DirtyHandle.Reset();
    }

    DirtyPackages.Reset();
    PreExistingDirtyNames.Reset();
}

void FScopedWorldSweepSaveTracker::SaveDirty()
{
    if (!bEnabled)
    {
        return;
    }

    DirtyPackages.RemoveAll([](const TObjectPtr<UPackage>& InPackage)
    {
        return !IsValid(InPackage) || !InPackage->IsDirty();
    });

    if (DirtyPackages.IsEmpty())
    {
        return;
    }

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Saving %d package(s) dirtied during sweep..."), DirtyPackages.Num());

    TSet<FString> ExistingFilePaths;
    TArray<FString> NewFilePaths;

    for (const TObjectPtr<UPackage>& Package : DirtyPackages)
    {
        if (!IsValid(Package))
        {
            continue;
        }

        FString FilePath;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), FilePath, FPackageName::GetAssetPackageExtension()))
        {
            continue;
        }

        if (IFileManager::Get().FileExists(*FilePath))
        {
            ExistingFilePaths.Add(FilePath);
        }
        else
        {
            NewFilePaths.Add(FilePath);
        }
    }

    // Check out files that already exist on disk before writing over them.
    CheckOutIfSourceControlled(ExistingFilePaths.Array());

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Reserve(DirtyPackages.Num());
    for (const TObjectPtr<UPackage>& Package : DirtyPackages)
    {
        if (IsValid(Package))
        {
            PackagesToSave.Add(Package.Get());
        }
    }
    UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);

    // Mark newly created files for add, but only those the save actually produced.
    TArray<FString> CreatedFilePaths;
    for (const FString& Path : NewFilePaths)
    {
        if (IFileManager::Get().FileExists(*Path))
        {
            CreatedFilePaths.Add(Path);
        }
    }
    CheckOutIfSourceControlled(CreatedFilePaths);

    DirtyPackages.Reset();
}

void FScopedWorldSweepSaveTracker::AddReferencedObjects(FReferenceCollector& InCollector)
{
    InCollector.AddReferencedObjects(DirtyPackages);
}

FString FScopedWorldSweepSaveTracker::GetReferencerName() const
{
    return TEXT("FScopedWorldSweepSaveTracker");
}
