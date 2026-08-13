// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepNaniteAuditor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/EngineVersionComparison.h"

#include "WorldSweepLog.h"

namespace
{
    /**
     * Read a static mesh's Nanite settings.
     *
     * UE 5.7 added GetNaniteSettings/SetNaniteSettings and deprecated direct access to the
     * UStaticMesh::NaniteSettings member. UE 5.4 to 5.6 have only the member. Reading the
     * member on 5.7+ compiles but emits a deprecation warning, so each range uses its own path.
     */
    FMeshNaniteSettings GetMeshNaniteSettings(const UStaticMesh& InMesh)
    {
#if UE_VERSION_OLDER_THAN(5, 7, 0)
        return InMesh.NaniteSettings;
#else
        return InMesh.GetNaniteSettings();
#endif
    }

    /** Write a static mesh's Nanite settings. See GetMeshNaniteSettings for the version split. */
    void SetMeshNaniteSettings(UStaticMesh& InOutMesh, const FMeshNaniteSettings& InSettings)
    {
#if UE_VERSION_OLDER_THAN(5, 7, 0)
        InOutMesh.NaniteSettings = InSettings;
#else
        InOutMesh.SetNaniteSettings(InSettings);
#endif
    }
}

UWorldSweepNaniteAuditor::UWorldSweepNaniteAuditor()
    : bEnableNanite(true)
    , bApplyChanges(false)
    , ViolationCount(0)
    , ChangedCount(0)
{
    EventFlags = static_cast<int32>(EWorldSweepEventFlags::Components);
}

void UWorldSweepNaniteAuditor::OnBatchStarted_Implementation()
{
    ProcessedMeshPaths.Reset();
    ViolationCount = 0;
    ChangedCount   = 0;
}

void UWorldSweepNaniteAuditor::OnComponentFound_Implementation(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds)
{
    const UStaticMeshComponent* SMC  = Cast<UStaticMeshComponent>(InComponent);
    UStaticMesh*                Mesh = SMC ? SMC->GetStaticMesh() : nullptr;
    if (!Mesh)
    {
        return;
    }

    // Deduplicate by asset path. The same mesh may be referenced by many actors.
    const FName MeshPath(*Mesh->GetPathName());
    if (ProcessedMeshPaths.Contains(MeshPath))
    {
        return;
    }
    ProcessedMeshPaths.Add(MeshPath);

    FMeshNaniteSettings Settings = GetMeshNaniteSettings(*Mesh);
    if (Settings.bEnabled == bEnableNanite)
    {
        return;
    }

    const bool bCurrentlyEnabled = Settings.bEnabled;

    ++ViolationCount;

    if (bApplyChanges)
    {
        Mesh->Modify();
        Settings.bEnabled = bEnableNanite;
        SetMeshNaniteSettings(*Mesh, Settings);
        ++ChangedCount;

        UE_LOG(LogWorldSweep, Log, TEXT("[NaniteAuditor] Nanite %s → '%s' (referenced by '%s')"),
            bEnableNanite ? TEXT("enabled on") : TEXT("disabled on"),
            *Mesh->GetName(),
            *InActor->GetActorLabel());
    }
    else
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("[NaniteAuditor] Nanite is %s but expected %s: '%s' (referenced by '%s')"),
            bCurrentlyEnabled ? TEXT("Enabled") : TEXT("Disabled"),
            bEnableNanite     ? TEXT("Enabled") : TEXT("Disabled"),
            *Mesh->GetName(),
            *InActor->GetActorLabel());
    }
}

void UWorldSweepNaniteAuditor::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool WasCancelled)
{
    if (bApplyChanges)
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[NaniteAuditor] Apply complete. Meshes changed: %d | Unique meshes visited: %d"),
            ChangedCount,
            ProcessedMeshPaths.Num());
    }
    else
    {
        if (ViolationCount == 0)
        {
            UE_LOG(LogWorldSweep, Log, TEXT("[NaniteAuditor] Audit complete. All %d unique mesh(es) have Nanite %s."),
                ProcessedMeshPaths.Num(),
                bEnableNanite ? TEXT("Enabled") : TEXT("Disabled"));
        }
        else
        {
            UE_LOG(LogWorldSweep, Warning, TEXT("[NaniteAuditor] Audit complete. %d of %d unique mesh(es) do not have Nanite %s."),
                ViolationCount,
                ProcessedMeshPaths.Num(),
                bEnableNanite ? TEXT("Enabled") : TEXT("Disabled"));
        }
    }
}

bool UWorldSweepNaniteAuditor::HasFailed_Implementation() const
{
    // In apply mode, violations have been fixed. The batch is not a failure.
    return !bApplyChanges && ViolationCount > 0;
}