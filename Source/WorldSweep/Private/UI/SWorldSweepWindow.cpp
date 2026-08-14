// Copyright © ToaGames. All Rights Reserved.

#include "UI/SWorldSweepWindow.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "WorldPartition/WorldPartition.h"

#include "Core/WorldSweepBatch.h"
#include "Execution/WorldSweepRunner.h"
#include "UI/SWorldSweepMapView.h"
#include "WorldSweepInternal.h"
#include "WorldSweepLog.h"

#define LOCTEXT_NAMESPACE "SWorldSweepWindow"

namespace
{
    /** Cell size shown in the map view when no batch is selected, in cm. Matches the World Partition default. */
    constexpr float DefaultCellSize = 25600.0f;
}

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

        + SSplitter::Slot()
        .Value(0.4f)
        [
            BuildWorldPanel()
        ]

        + SSplitter::Slot()
        .Value(0.6f)
        [
            BuildConfigPanel()
        ]
    ];
}

SWorldSweepWindow::~SWorldSweepWindow()
{
    FEditorDelegates::MapChange.Remove(WindowMapChangeHandle);
}

TSharedRef<SWidget> SWorldSweepWindow::BuildWorldPanel()
{
    return SNew(SWidgetSwitcher)
        .WidgetIndex(this, &SWorldSweepWindow::GetLeftPanelIndex)

        // Index 0: World Partition. Interactive map view.
        + SWidgetSwitcher::Slot()
        [
            SNew(SWorldSweepMapView)
            .InWorld(GetEditorWorld())
            .CellSize(this, &SWorldSweepWindow::GetCellSize)
            .SweepArea(this, &SWorldSweepWindow::GetSweepArea)
            .OnSweepAreaChanged(this, &SWorldSweepWindow::OnMapSweepAreaChanged)
        ]

        // Index 1: Streaming Levels.
        + SWidgetSwitcher::Slot()
        [
            BuildInfoPanel(LOCTEXT("StreamingLevelsInfo",
                "Streaming Levels mode\n\n"
                "Each sub-level will be loaded and processed\n"
                "as a cell. Use the Sweep Area below to limit\n"
                "processing to levels within that region."))
        ]

        // Index 2: Flat Level.
        + SWidgetSwitcher::Slot()
        [
            BuildInfoPanel(LOCTEXT("FlatLevelInfo",
                "Flat Level mode\n\n"
                "All actors in the persistent level will be\n"
                "processed in a single pass. Optionally set a\n"
                "Sweep Area below to filter by location."))
        ];
}

TSharedRef<SWidget> SWorldSweepWindow::BuildInfoPanel(const FText& InMessage)
{
    return SNew(SBox)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .Padding(16.0f)
        [
            SNew(STextBlock)
            .Text(InMessage)
            .Justification(ETextJustify::Center)
            .AutoWrapText(false)
        ];
}

TSharedRef<SWidget> SWorldSweepWindow::BuildConfigPanel()
{
    return SNew(SScrollBox)
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
                BuildSweepAreaPanel()
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
        ];
}

TSharedRef<SWidget> SWorldSweepWindow::BuildSweepAreaPanel()
{
    return SNew(SBox)
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

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                BuildCoordinateRow(
                    LOCTEXT("MinLabel", "Min"),
                    TAttribute<TOptional<double>>(this, &SWorldSweepWindow::GetMinX),
                    TAttribute<TOptional<double>>(this, &SWorldSweepWindow::GetMinY),
                    [this](double InValue) { OnMinXCommitted(InValue, ETextCommit::OnEnter); },
                    [this](double InValue) { OnMinYCommitted(InValue, ETextCommit::OnEnter); })
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
            [
                BuildCoordinateRow(
                    LOCTEXT("MaxLabel", "Max"),
                    TAttribute<TOptional<double>>(this, &SWorldSweepWindow::GetMaxX),
                    TAttribute<TOptional<double>>(this, &SWorldSweepWindow::GetMaxY),
                    [this](double InValue) { OnMaxXCommitted(InValue, ETextCommit::OnEnter); },
                    [this](double InValue) { OnMaxYCommitted(InValue, ETextCommit::OnEnter); })
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [ SNew(SSeparator) ]
        ];
}

TSharedRef<SWidget> SWorldSweepWindow::BuildCoordinateRow(const FText& InLabel, TAttribute<TOptional<double>> InX, TAttribute<TOptional<double>> InY, TFunction<void(double)> InOnXCommitted, TFunction<void(double)> InOnYCommitted)
{
    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(InLabel)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
        ]

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            SNew(SNumericEntryBox<double>)
            .Label() [ SNew(STextBlock).Text(LOCTEXT("XLabel", "X")) ]
            .Value(InX)
            .OnValueCommitted_Lambda([InOnXCommitted](double InValue, ETextCommit::Type) { InOnXCommitted(InValue); })
            .AllowSpin(false)
        ]

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        [
            SNew(SNumericEntryBox<double>)
            .Label() [ SNew(STextBlock).Text(LOCTEXT("YLabel", "Y")) ]
            .Value(InY)
            .OnValueCommitted_Lambda([InOnYCommitted](double InValue, ETextCommit::Type) { InOnYCommitted(InValue); })
            .AllowSpin(false)
        ];
}

void SWorldSweepWindow::RefreshWorldMode()
{
    UWorld* World = GetEditorWorld();
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

void SWorldSweepWindow::OnWorldMapChanged(uint32 InChangeType)
{
    RefreshWorldMode();
}

UWorld* SWorldSweepWindow::GetEditorWorld()
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

FString SWorldSweepWindow::GetBatchAssetPath() const
{
    const UWorldSweepBatch* Batch = SelectedBatch.Get();
    return Batch ? Batch->GetPathName() : FString();
}

void SWorldSweepWindow::OnBatchAssetChanged(const FAssetData& InAssetData)
{
    SelectedBatch = Cast<UWorldSweepBatch>(InAssetData.GetAsset());
}

FReply SWorldSweepWindow::OnUseEntireWorldClicked()
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return FReply::Handled();
    }

    if (const UWorldPartition* Partition = World->GetWorldPartition())
    {
        // Start from the World Partition editor bounds, then expand to cover any persistent-level
        // actors placed outside the WP grid. GetEditorWorldBounds covers every WP cell, including
        // those outside the runtime streaming range; GetRuntimeWorldBounds can be smaller and
        // would silently drop edge cells.
        SweepArea = Partition->GetEditorWorldBounds();
        SweepArea += WorldSweepInternal::AccumulateActorBounds(World);

        // Pin Z to the full legal world height so sky and atmosphere actors at extreme altitudes
        // are never clipped by the cell bounds check.
        SweepArea.Min.Z = -WorldSweepInternal::HalfWorldMax;
        SweepArea.Max.Z =  WorldSweepInternal::HalfWorldMax;

        return FReply::Handled();
    }

    const FBox ActorBounds = WorldSweepInternal::AccumulateActorBounds(World);
    if (ActorBounds.IsValid)
    {
        SweepArea = ActorBounds.ExpandBy(WorldSweepInternal::ActorBoundsPadding);
    }

    return FReply::Handled();
}

EVisibility SWorldSweepWindow::GetUseEntireWorldVisibility() const
{
    // For Streaming Levels, world bounds can't be derived without loading every level first.
    return DetectedMode == EWorldSweepMode::StreamingLevels ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SWorldSweepWindow::GetSweepAreaVisibility() const
{
    // Sweep area is always shown: required for WP, optional spatial filter for Flat, region filter for Streaming.
    return EVisibility::Visible;
}

TOptional<double> SWorldSweepWindow::GetMinX() const
{
    return SweepArea.IsValid ? TOptional<double>(SweepArea.Min.X) : TOptional<double>();
}

TOptional<double> SWorldSweepWindow::GetMinY() const
{
    return SweepArea.IsValid ? TOptional<double>(SweepArea.Min.Y) : TOptional<double>();
}

TOptional<double> SWorldSweepWindow::GetMaxX() const
{
    return SweepArea.IsValid ? TOptional<double>(SweepArea.Max.X) : TOptional<double>();
}

TOptional<double> SWorldSweepWindow::GetMaxY() const
{
    return SweepArea.IsValid ? TOptional<double>(SweepArea.Max.Y) : TOptional<double>();
}

void SWorldSweepWindow::OnMinXCommitted(double InValue, ETextCommit::Type InCommitType)
{
    SweepArea.Min.X = InValue;
    RefreshSweepAreaValidity();
}

void SWorldSweepWindow::OnMinYCommitted(double InValue, ETextCommit::Type InCommitType)
{
    SweepArea.Min.Y = InValue;
    RefreshSweepAreaValidity();
}

void SWorldSweepWindow::OnMaxXCommitted(double InValue, ETextCommit::Type InCommitType)
{
    SweepArea.Max.X = InValue;
    RefreshSweepAreaValidity();
}

void SWorldSweepWindow::OnMaxYCommitted(double InValue, ETextCommit::Type InCommitType)
{
    SweepArea.Max.Y = InValue;
    RefreshSweepAreaValidity();
}

void SWorldSweepWindow::RefreshSweepAreaValidity()
{
    SweepArea.IsValid = (SweepArea.Max.X > SweepArea.Min.X) && (SweepArea.Max.Y > SweepArea.Min.Y);
}

FBox SWorldSweepWindow::GetSweepArea() const
{
    return SweepArea;
}

float SWorldSweepWindow::GetCellSize() const
{
    const UWorldSweepBatch* Batch = SelectedBatch.Get();
    return Batch ? Batch->CellSize : DefaultCellSize;
}

void SWorldSweepWindow::OnMapSweepAreaChanged(const FBox& InBox)
{
    SweepArea = InBox;
}

int32 SWorldSweepWindow::GetLeftPanelIndex() const
{
    switch (DetectedMode)
    {
        case EWorldSweepMode::StreamingLevels: return 1;
        case EWorldSweepMode::FlatLevel:       return 2;
        default:                               return 0;
    }
}

FReply SWorldSweepWindow::OnRunClicked()
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        WS_NOTIFY_ERROR(TEXT("WorldSweep: No editor world found. Cannot run batch."));
        return FReply::Handled();
    }

    UWorldSweepBatch* Batch = SelectedBatch.Get();
    if (!Batch)
    {
        WS_NOTIFY_ERROR(TEXT("WorldSweep: No batch asset selected, or the selection was unloaded. Cannot run batch."));
        return FReply::Handled();
    }

    UWorldSweepRunner* Runner = NewObject<UWorldSweepRunner>();
    Runner->AddToRoot();
    const FWorldSweepResult Result = Runner->Execute(Batch, SweepArea, World);
    Runner->RemoveFromRoot();

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Run %s. Cells: %d | Actors: %d"),
        Result.bWasCancelled ? TEXT("cancelled") : TEXT("complete"),
        Result.CellsProcessed,
        Result.ActorsProcessed);

    return FReply::Handled();
}

bool SWorldSweepWindow::CanRun() const
{
    if (!SelectedBatch.IsValid())
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

#undef LOCTEXT_NAMESPACE
