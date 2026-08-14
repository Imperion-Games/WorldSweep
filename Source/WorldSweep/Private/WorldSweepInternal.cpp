// Copyright © ToaGames. All Rights Reserved.

#include "WorldSweepInternal.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"

namespace WorldSweepInternal
{
    FBox AccumulateActorBounds(UWorld* InWorld, int32* OutActorCount)
    {
        FBox Bounds(EForceInit::ForceInit);
        int32 Count = 0;

        if (InWorld)
        {
            for (TActorIterator<AActor> It(InWorld); It; ++It)
            {
                AActor* Actor = *It;
                if (Actor && !Actor->IsA<AWorldSettings>())
                {
                    Bounds += Actor->GetActorLocation();
                    ++Count;
                }
            }
        }

        if (OutActorCount)
        {
            *OutActorCount = Count;
        }

        return Bounds;
    }
}
