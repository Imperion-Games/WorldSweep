// Copyright © ToaGames. All Rights Reserved.

#include "UI/SWorldSweepMapView.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "WorldPartition/WorldPartition.h"

namespace
{
    /** Maximum number of cell lines to draw before skipping the grid entirely (avoids overdraw on huge worlds at low zoom). */
    constexpr int32 MaxCellGridLines = 300;

    /** Half-extent of the fallback world square used when the world exposes no bounds, in cm. */
    constexpr double FallbackWorldHalfExtent = 512000.0;

    /** Half the legal world height, in cm. Sweep areas span this range so actors at extreme altitudes are never clipped. */
    constexpr double HalfWorldMax = 1048576.0;

    /** Minimum drag extent, in world units, below which a selection is treated as an accidental click. */
    constexpr double MinDragExtent = 100.0;

    /** Build the five-point line loop tracing the rectangle from InMin to InMax. */
    TArray<FVector2D> MakeRectBorder(const FVector2D& InMin, const FVector2D& InMax)
    {
        return { InMin, FVector2D(InMax.X, InMin.Y), InMax, FVector2D(InMin.X, InMax.Y), InMin };
    }
}

void SWorldSweepMapView::Construct(const FArguments& InArgs)
{
    World              = InArgs._InWorld;
    CellSize           = InArgs._CellSize;
    SweepArea          = InArgs._SweepArea;
    OnSweepAreaChanged = InArgs._OnSweepAreaChanged;

    ResetInteractionState();
    ResolveWorldBounds();

    SetClipping(EWidgetClipping::ClipToBounds);

    MapChangeHandle = FEditorDelegates::MapChange.AddRaw(this, &SWorldSweepMapView::OnEditorMapChanged);
}

SWorldSweepMapView::~SWorldSweepMapView()
{
    FEditorDelegates::MapChange.Remove(MapChangeHandle);
}

int32 SWorldSweepMapView::OnPaint(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry, const FSlateRect& InCullingRect, FSlateWindowElementList& OutDrawElements, int32 InLayerId, const FWidgetStyle& InWidgetStyle, bool InParentEnabled) const
{
    const FWorldSweepViewBox View = GetViewBox(InAllottedGeometry);

    int32 LayerId = InLayerId;
    PaintBackground(InAllottedGeometry, OutDrawElements, LayerId);
    PaintCellGrid(InAllottedGeometry, OutDrawElements, LayerId, View);
    PaintLoadedRegions(InAllottedGeometry, OutDrawElements, LayerId, View);
    PaintSweepArea(InAllottedGeometry, OutDrawElements, LayerId, View);
    PaintDragPreview(InAllottedGeometry, OutDrawElements, LayerId, View);
    PaintCursorCoordinates(InAllottedGeometry, OutDrawElements, LayerId, View);

    return LayerId;
}

FReply SWorldSweepMapView::OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());

    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FWorldSweepViewBox View = GetViewBox(InGeometry);
        DragStartWorld   = LocalToWorld(LocalPos, View, InGeometry.GetLocalSize());
        DragCurrentWorld = DragStartWorld;
        bIsDragging      = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        LastPanMouseLocal = LocalPos;
        bIsPanning        = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    return FReply::Unhandled();
}

FReply SWorldSweepMapView::OnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
    LastMouseLocal = LocalPos;

    if (bIsDragging)
    {
        const FWorldSweepViewBox View = GetViewBox(InGeometry);
        DragCurrentWorld = LocalToWorld(LocalPos, View, InGeometry.GetLocalSize());
        bShiftHeld       = InMouseEvent.IsShiftDown();
        return FReply::Handled();
    }

    if (bIsPanning)
    {
        const FWorldSweepViewBox View = GetViewBox(InGeometry);
        const FVector2D LocalSize     = InGeometry.GetLocalSize();
        const FVector2D WorldDelta    = LocalToWorld(LocalPos, View, LocalSize) - LocalToWorld(LastPanMouseLocal, View, LocalSize);

        ViewCenter       -= WorldDelta;
        LastPanMouseLocal = LocalPos;
        return FReply::Handled();
    }

    // Always consume so the cursor coordinate readout keeps updating.
    return FReply::Handled();
}

FReply SWorldSweepMapView::OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
    {
        bIsDragging = false;

        const FVector2D RawStart   = SnapToCellGrid(DragStartWorld);
        const FVector2D RawCurrent = SnapToCellGrid(DragCurrentWorld);

        bShiftHeld = false;

        const FVector2D SelectMin(FMath::Min(RawStart.X, RawCurrent.X), FMath::Min(RawStart.Y, RawCurrent.Y));
        const FVector2D SelectMax(FMath::Max(RawStart.X, RawCurrent.X), FMath::Max(RawStart.Y, RawCurrent.Y));

        if ((SelectMax - SelectMin).GetMin() > MinDragExtent)
        {
            // Use the full legal world height so no actors at extreme Z are missed.
            const FBox NewArea(
                FVector(SelectMin.X, SelectMin.Y, -HalfWorldMax),
                FVector(SelectMax.X, SelectMax.Y,  HalfWorldMax)
            );
            OnSweepAreaChanged.ExecuteIfBound(NewArea);
        }

        return FReply::Handled().ReleaseMouseCapture();
    }

    if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bIsPanning)
    {
        bIsPanning = false;
        return FReply::Handled().ReleaseMouseCapture();
    }

    return FReply::Unhandled();
}

FReply SWorldSweepMapView::OnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    const FVector2D LocalPos     = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
    const FVector2D LocalSize    = InGeometry.GetLocalSize();
    const FVector2D MouseWorld   = LocalToWorld(LocalPos, GetViewBox(InGeometry), LocalSize);

    const double Factor = InMouseEvent.GetWheelDelta() > 0.0f ? 1.2 : (1.0 / 1.2);
    ZoomLevel = FMath::Clamp(ZoomLevel * Factor, 0.5, 64.0);

    // Recentre so the world point under the cursor stays fixed.
    const FVector2D NewWorld = LocalToWorld(LocalPos, GetViewBox(InGeometry), LocalSize);
    ViewCenter -= (NewWorld - MouseWorld);

    return FReply::Handled();
}

FCursorReply SWorldSweepMapView::OnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) const
{
    if (bIsPanning)
    {
        return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
    }
    return FCursorReply::Cursor(EMouseCursor::Crosshairs);
}

FVector2D SWorldSweepMapView::ComputeDesiredSize(float InLayoutScaleMultiplier) const
{
    return FVector2D(400.0, 400.0);
}

void SWorldSweepMapView::ResolveWorldBounds()
{
    WorldBounds.Init();

    if (const UWorld* CurrentWorld = World.Get())
    {
        if (const UWorldPartition* Partition = CurrentWorld->GetWorldPartition())
        {
            WorldBounds = Partition->GetEditorWorldBounds();
        }
    }

    if (!WorldBounds.IsValid)
    {
        WorldBounds = FBox(
            FVector(-FallbackWorldHalfExtent, -FallbackWorldHalfExtent, 0.0),
            FVector( FallbackWorldHalfExtent,  FallbackWorldHalfExtent, 0.0)
        );
    }

    ViewCenter = FVector2D(WorldBounds.GetCenter().X, WorldBounds.GetCenter().Y);
    ZoomLevel  = 1.0;
}

void SWorldSweepMapView::ResetInteractionState()
{
    bIsDragging = false;
    bIsPanning  = false;
    bShiftHeld  = false;
}

void SWorldSweepMapView::OnEditorMapChanged(uint32 InChangeType)
{
    World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

    ResolveWorldBounds();
    ResetInteractionState();
}

SWorldSweepMapView::FWorldSweepViewBox SWorldSweepMapView::GetViewBox(const FGeometry& InGeometry) const
{
    const FVector2D LocalSize = InGeometry.GetLocalSize();
    if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
    {
        return { ViewCenter, ViewCenter };
    }

    // Use one uniform world-units-per-pixel scale for both axes so a non-square widget
    // does not distort the aspect ratio. The scale chosen makes the larger world
    // dimension just fit the widget at ZoomLevel 1.
    const double ScaleX       = WorldBounds.GetSize().X / LocalSize.X;
    const double ScaleY       = WorldBounds.GetSize().Y / LocalSize.Y;
    const double WorldPerPixel = FMath::Max(ScaleX, ScaleY) / ZoomLevel;

    const FVector2D HalfSize = LocalSize * 0.5 * WorldPerPixel;
    return { ViewCenter - HalfSize, ViewCenter + HalfSize };
}

FVector2D SWorldSweepMapView::WorldToLocal(const FVector2D& InWorldXY, const FWorldSweepViewBox& InView, const FVector2D& InLocalSize) const
{
    const FVector2D ViewSize = InView.Size();
    return FVector2D(
        (InWorldXY.X - InView.Min.X) / ViewSize.X * InLocalSize.X,
        (InWorldXY.Y - InView.Min.Y) / ViewSize.Y * InLocalSize.Y
    );
}

FVector2D SWorldSweepMapView::LocalToWorld(const FVector2D& InLocalPos, const FWorldSweepViewBox& InView, const FVector2D& InLocalSize) const
{
    const FVector2D ViewSize = InView.Size();
    return FVector2D(
        InView.Min.X + (InLocalPos.X / InLocalSize.X) * ViewSize.X,
        InView.Min.Y + (InLocalPos.Y / InLocalSize.Y) * ViewSize.Y
    );
}

FVector2D SWorldSweepMapView::SnapToCellGrid(const FVector2D& InWorldXY) const
{
    if (!bShiftHeld)
    {
        return InWorldXY;
    }

    const double GridSize = FMath::Max(static_cast<double>(CellSize.Get()), 1.0);
    return FVector2D(FMath::GridSnap(InWorldXY.X, GridSize), FMath::GridSnap(InWorldXY.Y, GridSize));
}

void SWorldSweepMapView::PaintOutlinedRect(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FVector2D& InMin, const FVector2D& InMax, const FLinearColor& InFillColor, const FLinearColor& InBorderColor, float InBorderThickness)
{
    const FVector2D RectSize = InMax - InMin;
    if (RectSize.X <= 0.0 || RectSize.Y <= 0.0)
    {
        return;
    }

    FSlateDrawElement::MakeBox(OutDrawElements, InOutLayerId,
        InGeometry.ToPaintGeometry(RectSize, FSlateLayoutTransform(InMin)),
        FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, InFillColor);
    ++InOutLayerId;

    FSlateDrawElement::MakeLines(OutDrawElements, InOutLayerId, InGeometry.ToPaintGeometry(),
        MakeRectBorder(InMin, InMax), ESlateDrawEffect::None, InBorderColor, true, InBorderThickness);
    ++InOutLayerId;
}

void SWorldSweepMapView::PaintBackground(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId) const
{
    FSlateDrawElement::MakeBox(OutDrawElements, InOutLayerId, InGeometry.ToPaintGeometry(),
        FCoreStyle::Get().GetBrush("WhiteBrush"),
        ESlateDrawEffect::None, FLinearColor(0.04f, 0.04f, 0.04f, 1.0f));
    ++InOutLayerId;

    const FWorldSweepViewBox View = GetViewBox(InGeometry);
    const FVector2D LocalSize     = InGeometry.GetLocalSize();
    const FVector2D BoundsMin     = WorldToLocal(FVector2D(WorldBounds.Min.X, WorldBounds.Min.Y), View, LocalSize);
    const FVector2D BoundsMax     = WorldToLocal(FVector2D(WorldBounds.Max.X, WorldBounds.Max.Y), View, LocalSize);

    PaintOutlinedRect(InGeometry, OutDrawElements, InOutLayerId, BoundsMin, BoundsMax,
        FLinearColor(0.08f, 0.08f, 0.08f, 1.0f), FLinearColor(0.25f, 0.25f, 0.25f, 1.0f), 1.0f);
}

void SWorldSweepMapView::PaintCellGrid(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const
{
    const double GridSize = FMath::Max(static_cast<double>(CellSize.Get()), 1.0);
    const FVector2D LocalSize = InGeometry.GetLocalSize();

    // Skip the grid entirely when it would be too dense to read.
    const int32 NumX = FMath::CeilToInt(WorldBounds.GetSize().X / GridSize) + 1;
    const int32 NumY = FMath::CeilToInt(WorldBounds.GetSize().Y / GridSize) + 1;
    if (NumX + NumY > MaxCellGridLines)
    {
        return;
    }

    const FLinearColor GridColor(0.15f, 0.15f, 0.15f, 1.0f);

    for (double X = FMath::GridSnap(WorldBounds.Min.X, GridSize); X <= WorldBounds.Max.X; X += GridSize)
    {
        const FVector2D LineStart = WorldToLocal(FVector2D(X, WorldBounds.Min.Y), InView, LocalSize);
        const FVector2D LineEnd   = WorldToLocal(FVector2D(X, WorldBounds.Max.Y), InView, LocalSize);
        FSlateDrawElement::MakeLines(OutDrawElements, InOutLayerId, InGeometry.ToPaintGeometry(),
            TArray<FVector2D>{ LineStart, LineEnd }, ESlateDrawEffect::None, GridColor, true, 0.5f);
    }

    for (double Y = FMath::GridSnap(WorldBounds.Min.Y, GridSize); Y <= WorldBounds.Max.Y; Y += GridSize)
    {
        const FVector2D LineStart = WorldToLocal(FVector2D(WorldBounds.Min.X, Y), InView, LocalSize);
        const FVector2D LineEnd   = WorldToLocal(FVector2D(WorldBounds.Max.X, Y), InView, LocalSize);
        FSlateDrawElement::MakeLines(OutDrawElements, InOutLayerId, InGeometry.ToPaintGeometry(),
            TArray<FVector2D>{ LineStart, LineEnd }, ESlateDrawEffect::None, GridColor, true, 0.5f);
    }

    ++InOutLayerId;
}

void SWorldSweepMapView::PaintLoadedRegions(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const
{
    const UWorld* CurrentWorld = World.Get();
    const UWorldPartition* Partition = CurrentWorld ? CurrentWorld->GetWorldPartition() : nullptr;
    if (!Partition)
    {
        return;
    }

    const FVector2D LocalSize = InGeometry.GetLocalSize();

    for (const FBox& Region : Partition->GetUserLoadedEditorRegions())
    {
        const FVector2D RegionMin = WorldToLocal(FVector2D(Region.Min.X, Region.Min.Y), InView, LocalSize);
        const FVector2D RegionMax = WorldToLocal(FVector2D(Region.Max.X, Region.Max.Y), InView, LocalSize);

        PaintOutlinedRect(InGeometry, OutDrawElements, InOutLayerId, RegionMin, RegionMax,
            FLinearColor(0.1f, 0.35f, 0.1f, 0.3f), FLinearColor(0.2f, 0.7f, 0.2f, 0.8f), 1.0f);
    }
}

void SWorldSweepMapView::PaintSweepArea(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const
{
    const FBox Current = SweepArea.Get();
    if (!Current.IsValid || bIsDragging)
    {
        return;
    }

    const FVector2D LocalSize = InGeometry.GetLocalSize();
    const FVector2D AreaMin   = WorldToLocal(FVector2D(Current.Min.X, Current.Min.Y), InView, LocalSize);
    const FVector2D AreaMax   = WorldToLocal(FVector2D(Current.Max.X, Current.Max.Y), InView, LocalSize);

    PaintOutlinedRect(InGeometry, OutDrawElements, InOutLayerId, AreaMin, AreaMax,
        FLinearColor(0.2f, 0.5f, 1.0f, 0.2f), FLinearColor(0.2f, 0.5f, 1.0f, 1.0f), 1.5f);
}

void SWorldSweepMapView::PaintDragPreview(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const
{
    if (!bIsDragging)
    {
        return;
    }

    const FVector2D LocalSize = InGeometry.GetLocalSize();
    const FVector2D PointA    = WorldToLocal(SnapToCellGrid(DragStartWorld),   InView, LocalSize);
    const FVector2D PointB    = WorldToLocal(SnapToCellGrid(DragCurrentWorld), InView, LocalSize);

    const FVector2D RectMin(FMath::Min(PointA.X, PointB.X), FMath::Min(PointA.Y, PointB.Y));
    const FVector2D RectMax(FMath::Max(PointA.X, PointB.X), FMath::Max(PointA.Y, PointB.Y));

    // Ignore sub-pixel drags so a stray click does not flash an outline.
    if ((RectMax - RectMin).GetMin() <= 1.0)
    {
        return;
    }

    PaintOutlinedRect(InGeometry, OutDrawElements, InOutLayerId, RectMin, RectMax,
        FLinearColor(1.0f, 0.75f, 0.1f, 0.15f), FLinearColor(1.0f, 0.75f, 0.1f, 1.0f), 1.5f);
}

void SWorldSweepMapView::PaintCursorCoordinates(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const
{
    const FVector2D WorldPos = LocalToWorld(LastMouseLocal, InView, InGeometry.GetLocalSize());
    const FString CoordText  = FString::Printf(TEXT("%.0f, %.0f"), WorldPos.X, WorldPos.Y);

    FSlateDrawElement::MakeText(OutDrawElements, InOutLayerId,
        InGeometry.ToPaintGeometry(FVector2D(200.0, 14.0), FSlateLayoutTransform(FVector2D(4.0, InGeometry.GetLocalSize().Y - 16.0))),
        FText::FromString(CoordText), FCoreStyle::GetDefaultFontStyle("Regular", 8),
        ESlateDrawEffect::None, FLinearColor(0.6f, 0.6f, 0.6f, 1.0f));
    ++InOutLayerId;
}
