// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetRegistry/AssetData.h"
#include "Core/WorldSweepBatch.h"

class SWidget;
class SWorldSweepMapView;
class UWorld;

/** Main Slate panel for the WorldSweep editor tool. Provides batch configuration, sweep area coordinate inputs, and run controls. Adapts its layout based on the current world type (World Partition, Streaming Levels, or Flat Level). */
class WORLDSWEEP_API SWorldSweepWindow : public SCompoundWidget
{
public:

    SLATE_BEGIN_ARGS(SWorldSweepWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWorldSweepWindow();

private:

    /** Build the left-hand world view: the interactive map for World Partition, an informational panel otherwise. */
    TSharedRef<SWidget> BuildWorldPanel();

    /** Build a centred informational panel used by the non-World-Partition modes. */
    static TSharedRef<SWidget> BuildInfoPanel(const FText& InMessage);

    /** Build the right-hand configuration column: batch picker, sweep area, and run button. */
    TSharedRef<SWidget> BuildConfigPanel();

    /** Build the sweep-area coordinate block, including the "Use Entire World" shortcut. */
    TSharedRef<SWidget> BuildSweepAreaPanel();

    /** Build one labelled Min/Max coordinate row. Takes plain callables so this header need not pull in SNumericEntryBox. */
    TSharedRef<SWidget> BuildCoordinateRow(const FText& InLabel, TAttribute<TOptional<double>> InX, TAttribute<TOptional<double>> InY, TFunction<void(double)> InOnXCommitted, TFunction<void(double)> InOnYCommitted);

    void RefreshWorldMode();
    void OnWorldMapChanged(uint32 InChangeType);

    /** Returns the editor world, or null when no world is loaded. */
    static UWorld* GetEditorWorld();

    FString GetBatchAssetPath() const;
    void OnBatchAssetChanged(const FAssetData& InAssetData);

    FReply OnUseEntireWorldClicked();
    EVisibility GetUseEntireWorldVisibility() const;
    EVisibility GetSweepAreaVisibility() const;

    TOptional<double> GetMinX() const;
    TOptional<double> GetMinY() const;
    TOptional<double> GetMaxX() const;
    TOptional<double> GetMaxY() const;

    void OnMinXCommitted(double InValue, ETextCommit::Type InCommitType);
    void OnMinYCommitted(double InValue, ETextCommit::Type InCommitType);
    void OnMaxXCommitted(double InValue, ETextCommit::Type InCommitType);
    void OnMaxYCommitted(double InValue, ETextCommit::Type InCommitType);

    /** Recompute SweepArea.IsValid after one of its corners changed. */
    void RefreshSweepAreaValidity();

    FBox GetSweepArea() const;
    float GetCellSize() const;

    void OnMapSweepAreaChanged(const FBox& InBox);

    int32 GetLeftPanelIndex() const;

    FReply OnRunClicked();
    bool CanRun() const;

private:

    /**
     * Selected batch asset. Weak because this widget is neither a UObject nor an FGCObject,
     * so a TObjectPtr here would create no GC reference and could silently dangle. Every
     * read goes through a validity check.
     */
    TWeakObjectPtr<UWorldSweepBatch> SelectedBatch;

    FBox SweepArea;
    EWorldSweepMode DetectedMode;
    FDelegateHandle WindowMapChangeHandle;
};
