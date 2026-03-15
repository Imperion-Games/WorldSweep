// Copyright © ToaGames. All Rights Reserved.

#include "UI/SWorldSweepWindow.h"
#include "UI/SWorldSweepMapView.h"
#include "Core/WorldSweepBatch.h"
#include "Execution/WorldSweepRunner.h"
#include "WorldSweepLog.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "PropertyCustomizationHelpers.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "SWorldSweepWindow"

void SWorldSweepWindow::Construct(const FArguments& InArgs)
{
    SweepArea.Init();
    DetectedMode = EWorldSweepMode::Auto;

    RefreshWorldMode();

    WindowMapChangeHandle = FEditorDelegates::MapChange.AddSP(this, &SWorldSweepWindow::OnWorldMapChanged);

    ChildSlot
    [
        SNew(SSplitter)
        .Orientation(Orient_Horizontal)

        // Left — world view panel (content adapts to world type)
        + SSplitter::Slot()
        .Value(0.4f)
        [
            SNew(SWidgetSwitcher)
            .WidgetIndex(this, &SWorldSweepWindow::GetLeftPanelIndex)

            // Index 0: World Partition — interactive map view
            + SWidgetSwitcher::Slot()
            [
                SNew(SWorldSweepMapView)
                .InWorld(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
                .CellSize(this, &SWorldSweepWindow::GetCellSize)
                .SweepArea(this, &SWorldSweepWindow::GetSweepArea)
                .OnSweepAreaChanged(this, &SWorldSweepWindow::OnMapSweepAreaChanged)
            ]

            // Index 1: Streaming Levels — informational panel
            + SWidgetSwitcher::Slot()
            [
                SNew(SBox)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .Padding(16.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("StreamingLevelsInfo",
                        "Streaming Levels mode\n\n"
                        "Each sub-level will be loaded and processed\n"
                        "as a cell. Use the Sweep Area below to limit\n"
                        "processing to levels within that region."))
                    .Justification(ETextJustify::Center)
                    .AutoWrapText(false)
                ]
            ]

            // Index 2: Flat Level — informational panel
            + SWidgetSwitcher::Slot()
            [
                SNew(SBox)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .Padding(16.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("FlatLevelInfo",
                        "Flat Level mode\n\n"
                        "All actors in the persistent level will be\n"
                        "processed in a single pass. Optionally set a\n"
                        "Sweep Area below to filter by location."))
                    .Justification(ETextJustify::Center)
                    .AutoWrapText(false)
                ]
            ]
        ]

        // Right — batch configuration and run controls
        + SSplitter::Slot()
        .Value(0.6f)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            .Padding(12.0f)
            [
                SNew(SVerticalBox)

                // ----- Batch Asset -----

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("BatchLabel", "Batch Asset"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UWorldSweepBatch::StaticClass())
                    .ObjectPath(this, &SWorldSweepWindow::GetBatchAssetPath)
                    .OnObjectChanged(this, &SWorldSweepWindow::OnBatchAssetChanged)
                    .AllowClear(true)
                    .DisplayThumbnail(false)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [ SNew(SSeparator) ]

                // ----- Sweep Area -----

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [
                    SNew(SBox)
                    .Visibility(this, &SWorldSweepWindow::GetSweepAreaVisibility)
                    [
                        SNew(SVerticalBox)

                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("SweepAreaLabel", "Sweep Area"))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        ]

                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [
                            SNew(SBox)
                            .Visibility(this, &SWorldSweepWindow::GetUseEntireWorldVisibility)
                            [
                                SNew(SButton)
                                .Text(LOCTEXT("UseEntireWorldButton", "Use Entire World"))
                                .OnClicked(this, &SWorldSweepWindow::OnUseEntireWorldClicked)
                            ]
                        ]

                        // Min row
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                        [
                            SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("MinLabel", "Min"))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            ]

                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                            [
                                SNew(SNumericEntryBox<float>)
                                .Label() [ SNew(STextBlock).Text(LOCTEXT("XLabel", "X")) ]
                                .Value(this, &SWorldSweepWindow::GetMinX)
                                .OnValueCommitted(this, &SWorldSweepWindow::OnMinXCommitted)
                                .AllowSpin(false)
                            ]

                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            [
                                SNew(SNumericEntryBox<float>)
                                .Label() [ SNew(STextBlock).Text(LOCTEXT("YLabel", "Y")) ]
                                .Value(this, &SWorldSweepWindow::GetMinY)
                                .OnValueCommitted(this, &SWorldSweepWindow::OnMinYCommitted)
                                .AllowSpin(false)
                            ]
                        ]

                        // Max row
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                        [
                            SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("MaxLabel", "Max"))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            ]

                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                            [
                                SNew(SNumericEntryBox<float>)
                                .Label() [ SNew(STextBlock).Text(LOCTEXT("XLabel2", "X")) ]
                                .Value(this, &SWorldSweepWindow::GetMaxX)
                                .OnValueCommitted(this, &SWorldSweepWindow::OnMaxXCommitted)
                                .AllowSpin(false)
                            ]

                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            [
                                SNew(SNumericEntryBox<float>)
                                .Label() [ SNew(STextBlock).Text(LOCTEXT("YLabel2", "Y")) ]
                                .Value(this, &SWorldSweepWindow::GetMaxY)
                                .OnValueCommitted(this, &SWorldSweepWindow::OnMaxYCommitted)
                                .AllowSpin(false)
                            ]
                        ]

                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [ SNew(SSeparator) ]
                    ]
                ]

                // ----- Run -----

                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("RunButton", "Run Batch"))
                    .IsEnabled(this, &SWorldSweepWindow::CanRun)
                    .OnClicked(this, &SWorldSweepWindow::OnRunClicked)
                ]
            ]
        ]
    ];
}

SWorldSweepWindow::~SWorldSweepWindow()
{
    FEditorDelegates::MapChange.Remove(WindowMapChangeHandle);
}

// ---------------------------------------------------------------------------

void SWorldSweepWindow::RefreshWorldMode()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        DetectedMode = EWorldSweepMode::FlatLevel;
        return;
    }

    if (World->GetWorldPartition())
    {
        DetectedMode = EWorldSweepMode::WorldPartition;
    }
    else if (!World->GetStreamingLevels().IsEmpty())
    {
        DetectedMode = EWorldSweepMode::StreamingLevels;
    }
    else
    {
        DetectedMode = EWorldSweepMode::FlatLevel;
    }
}

void SWorldSweepWindow::OnWorldMapChanged(uint32 /*InChangeType*/)
{
    RefreshWorldMode();
}

// ---------------------------------------------------------------------------

int32 SWorldSweepWindow::GetLeftPanelIndex() const
{
    switch (DetectedMode)
    {
        case EWorldSweepMode::StreamingLevels: return 1;
        case EWorldSweepMode::FlatLevel:       return 2;
        default:                               return 0;
    }
}

EVisibility SWorldSweepWindow::GetSweepAreaVisibility() const
{
    // Sweep area is always shown: required for WP, optional spatial filter for Flat, region filter for Streaming.
    return EVisibility::Visible;
}

EVisibility SWorldSweepWindow::GetUseEntireWorldVisibility() const
{
    // For Streaming Levels, world bounds can't be derived without loading every level first.
    return DetectedMode == EWorldSweepMode::StreamingLevels ? EVisibility::Collapsed : EVisibility::Visible;
}

// ---------------------------------------------------------------------------

FString SWorldSweepWindow::GetBatchAssetPath() const
{
    if (SelectedBatch)
    {
        return SelectedBatch->GetPathName();
    }
    return FString();
}

void SWorldSweepWindow::OnBatchAssetChanged(const FAssetData& InAssetData)
{
    SelectedBatch = Cast<UWorldSweepBatch>(InAssetData.GetAsset());
}

// ---------------------------------------------------------------------------

FReply SWorldSweepWindow::OnUseEntireWorldClicked()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FReply::Handled();
    }

    if (UWorldPartition* WP = World->GetWorldPartition())
    {
        SweepArea = WP->GetRuntimeWorldBounds();
    }
    else
    {
        FBox ActorBounds(EForceInit::ForceInit);
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (*It && !(*It)->IsA<AWorldSettings>())
            {
                ActorBounds += (*It)->GetActorLocation();
            }
        }

        if (ActorBounds.IsValid)
        {
            SweepArea = ActorBounds.ExpandBy(500.0f);
        }
    }

    return FReply::Handled();
}

FBox SWorldSweepWindow::GetSweepArea() const
{
    return SweepArea;
}

float SWorldSweepWindow::GetCellSize() const
{
    return SelectedBatch ? SelectedBatch->CellSize : 25600.0f;
}

void SWorldSweepWindow::OnMapSweepAreaChanged(const FBox& InBox)
{
    SweepArea = InBox;
}

// ---------------------------------------------------------------------------

TOptional<float> SWorldSweepWindow::GetMinX() const
{
    return SweepArea.IsValid ? TOptional<float>(SweepArea.Min.X) : TOptional<float>();
}

TOptional<float> SWorldSweepWindow::GetMinY() const
{
    return SweepArea.IsValid ? TOptional<float>(SweepArea.Min.Y) : TOptional<float>();
}

TOptional<float> SWorldSweepWindow::GetMaxX() const
{
    return SweepArea.IsValid ? TOptional<float>(SweepArea.Max.X) : TOptional<float>();
}

TOptional<float> SWorldSweepWindow::GetMaxY() const
{
    return SweepArea.IsValid ? TOptional<float>(SweepArea.Max.Y) : TOptional<float>();
}

void SWorldSweepWindow::OnMinXCommitted(float InValue, ETextCommit::Type)
{
    SweepArea.Min.X = InValue;
    SweepArea.IsValid = (SweepArea.Max.X > SweepArea.Min.X) && (SweepArea.Max.Y > SweepArea.Min.Y);
}

void SWorldSweepWindow::OnMinYCommitted(float InValue, ETextCommit::Type)
{
    SweepArea.Min.Y = InValue;
    SweepArea.IsValid = (SweepArea.Max.X > SweepArea.Min.X) && (SweepArea.Max.Y > SweepArea.Min.Y);
}

void SWorldSweepWindow::OnMaxXCommitted(float InValue, ETextCommit::Type)
{
    SweepArea.Max.X = InValue;
    SweepArea.IsValid = (SweepArea.Max.X > SweepArea.Min.X) && (SweepArea.Max.Y > SweepArea.Min.Y);
}

void SWorldSweepWindow::OnMaxYCommitted(float InValue, ETextCommit::Type)
{
    SweepArea.Max.Y = InValue;
    SweepArea.IsValid = (SweepArea.Max.X > SweepArea.Min.X) && (SweepArea.Max.Y > SweepArea.Min.Y);
}

// ---------------------------------------------------------------------------

bool SWorldSweepWindow::CanRun() const
{
    if (!SelectedBatch)
    {
        return false;
    }

    // World Partition requires an explicit sweep area to define the region to stream.
    if (DetectedMode == EWorldSweepMode::WorldPartition && !SweepArea.IsValid)
    {
        return false;
    }

    return true;
}

FReply SWorldSweepWindow::OnRunClicked()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweep: No editor world found. Cannot run batch."));
        return FReply::Handled();
    }

    UWorldSweepRunner* Runner = NewObject<UWorldSweepRunner>();
    Runner->AddToRoot();
    const FWorldSweepResult Result = Runner->Execute(SelectedBatch, SweepArea, World);
    Runner->RemoveFromRoot();

    if (Result.bWasCancelled)
    {
        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Run cancelled. Cells processed: %d | Actors processed: %d"),
            Result.CellsProcessed, Result.ActorsProcessed);
    }
    else
    {
        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Run complete. Cells: %d | Actors: %d"),
            Result.CellsProcessed, Result.ActorsProcessed);
    }

    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
