// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepNamingConventionChecker.h"
#include "WorldSweepLog.h"
#include "GameFramework/Actor.h"
#include "Internationalization/Regex.h"

UWorldSweepNamingConventionChecker::UWorldSweepNamingConventionChecker()
    : Pattern(TEXT(".*"))
    , bLogMatchingActors(false)
    , ViolationCount(0)
    , CheckedCount(0)
{
    EventFlags = static_cast<int32>(EWorldSweepEventFlags::Actors);
}

void UWorldSweepNamingConventionChecker::OnBatchStarted_Implementation()
{
    ViolationCount = 0;
    CheckedCount   = 0;
}

void UWorldSweepNamingConventionChecker::OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds)
{
    if (!InActor || Pattern.IsEmpty())
    {
        return;
    }

    const FString ActorLabel = InActor->GetActorLabel();
    FRegexMatcher Matcher(FRegexPattern(Pattern), ActorLabel);
    const bool bMatches = Matcher.FindNext();

    ++CheckedCount;

    if (!bMatches)
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("[NamingChecker] Violation — '%s' (%s) does not match pattern '%s'"),
            *ActorLabel,
            *InActor->GetClass()->GetName(),
            *Pattern);
        ++ViolationCount;
    }
    else if (bLogMatchingActors)
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[NamingChecker] Match — '%s' (%s)"),
            *ActorLabel,
            *InActor->GetClass()->GetName());
    }
}

void UWorldSweepNamingConventionChecker::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled)
{
    UE_LOG(LogWorldSweep, Log, TEXT("[NamingChecker] Audit complete. Checked: %d | Violations: %d | Pattern: '%s'"),
        CheckedCount,
        ViolationCount,
        *Pattern);
}
