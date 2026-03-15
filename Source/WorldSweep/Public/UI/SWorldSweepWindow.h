// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetRegistry/AssetData.h"

class UWorldSweepBatch;
class SWorldSweepMapView;

/** Main Slate panel for the WorldSweep editor tool. Provides batch configuration, sweep area coordinate inputs, and run controls. Open alongside the World Partition Editor and Data Layers tabs via Tools > WorldSweep. */
class WORLDSWEEP_API SWorldSweepWindow : public SCompoundWidget
{
public:

    SLATE_BEGIN_ARGS(SWorldSweepWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:

    FString GetBatchAssetPath() const;
    void OnBatchAssetChanged(const FAssetData& InAssetData);

    FReply OnUseEntireWorldClicked();

    TOptional<float> GetMinX() const;
    TOptional<float> GetMinY() const;
    TOptional<float> GetMaxX() const;
    TOptional<float> GetMaxY() const;

    void OnMinXCommitted(float InValue, ETextCommit::Type);
    void OnMinYCommitted(float InValue, ETextCommit::Type);
    void OnMaxXCommitted(float InValue, ETextCommit::Type);
    void OnMaxYCommitted(float InValue, ETextCommit::Type);

    FBox GetSweepArea() const;
    float GetCellSize() const;

    void OnMapSweepAreaChanged(const FBox& InBox);

    FReply OnRunClicked();
    bool CanRun() const;

private:

    TObjectPtr<UWorldSweepBatch> SelectedBatch;
    FBox SweepArea;
};
