// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepNaniteAuditor.h"
#include "WorldSweepLog.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

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

    // Deduplicate by asset path — the same mesh may be referenced by many actors.
    const FName MeshPath(*Mesh->GetPathName());
    if (ProcessedMeshPaths.Contains(MeshPath))
    {
        return;
    }
    ProcessedMeshPaths.Add(MeshPath);

    const bool bCurrentlyEnabled = Mesh->GetNaniteSettings().bEnabled;
    if (bCurrentlyEnabled == bEnableNanite)
    {
        return;
    }

    ++ViolationCount;

    if (bApplyChanges)
    {
        FMeshNaniteSettings NewSettings = Mesh->GetNaniteSettings();
        NewSettings.bEnabled = bEnableNanite;
        Mesh->SetNaniteSettings(NewSettings);
        Mesh->MarkPackageDirty();
        ++ChangedCount;

        UE_LOG(LogWorldSweep, Log, TEXT("[NaniteAuditor] Nanite %s → '%s' (referenced by '%s')"),
            bEnableNanite ? TEXT("enabled on") : TEXT("disabled on"),
            *Mesh->GetName(),
            *InActor->GetActorLabel());
    }
    else
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("[NaniteAuditor] Nanite is %s but expected %s — '%s' (referenced by '%s')"),
            bCurrentlyEnabled ? TEXT("Enabled") : TEXT("Disabled"),
            bEnableNanite     ? TEXT("Enabled") : TEXT("Disabled"),
            *Mesh->GetName(),
            *InActor->GetActorLabel());
    }
}

void UWorldSweepNaniteAuditor::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled)
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
