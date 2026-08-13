// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UWorld;

DECLARE_DELEGATE_OneParam(FOnSweepAreaChanged, const FBox&)

/** Full-featured top-down map view for WorldSweep. Draws world bounds, the streaming cell grid, currently loaded WP regions, and the active sweep area rectangle. Supports drag-select to define the sweep area, zoom via mouse wheel, and pan via right mouse drag. Hold Shift while dragging to snap the selection to the cell grid. */
class WORLDSWEEP_API SWorldSweepMapView : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SWorldSweepMapView)
        : _InWorld(nullptr)
        , _CellSize(25600.0f)
    {}
        SLATE_ARGUMENT(UWorld*, InWorld)
        SLATE_ATTRIBUTE(float, CellSize)
        SLATE_ATTRIBUTE(FBox, SweepArea)
        SLATE_EVENT(FOnSweepAreaChanged, OnSweepAreaChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWorldSweepMapView();

    //~ Begin SWidget Interface
    virtual int32 OnPaint(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry, const FSlateRect& InCullingRect, FSlateWindowElementList& OutDrawElements, int32 InLayerId, const FWidgetStyle& InWidgetStyle, bool InParentEnabled) const override;
    virtual FReply OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply OnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply OnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FCursorReply OnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) const override;
    virtual FVector2D ComputeDesiredSize(float InLayoutScaleMultiplier) const override;
    virtual bool SupportsKeyboardFocus() const override { return false; }
    //~ End SWidget Interface

private:

    /** Visible world-space rectangle, in XY. Derived from the widget geometry, ViewCenter, and ZoomLevel. */
    struct FWorldSweepViewBox
    {
        FVector2D Min;
        FVector2D Max;
        FVector2D Size() const { return Max - Min; }
    };

    /** Re-read the world bounds from World Partition and recentre the view on them. Falls back to a fixed 1024 km square when the world exposes no bounds. */
    void ResolveWorldBounds();

    /** Reset the transient interaction state (drag, pan, shift snapping). */
    void ResetInteractionState();

    void OnEditorMapChanged(uint32 InChangeType);

    FWorldSweepViewBox GetViewBox(const FGeometry& InGeometry) const;
    FVector2D WorldToLocal(const FVector2D& InWorldXY, const FWorldSweepViewBox& InView, const FVector2D& InLocalSize) const;
    FVector2D LocalToWorld(const FVector2D& InLocalPos, const FWorldSweepViewBox& InView, const FVector2D& InLocalSize) const;

    /** Snap InWorldXY to the current cell grid. Returns it unchanged when the grid size is not positive. */
    FVector2D SnapToCellGrid(const FVector2D& InWorldXY) const;

    /** Draw a filled rectangle with a 1px outline in local widget space. Shared by the background, region, sweep-area, and drag-preview painters. */
    static void PaintOutlinedRect(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FVector2D& InMin, const FVector2D& InMax, const FLinearColor& InFillColor, const FLinearColor& InBorderColor, float InBorderThickness);

    void PaintBackground(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId) const;
    void PaintCellGrid(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const;
    void PaintLoadedRegions(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const;
    void PaintSweepArea(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const;
    void PaintDragPreview(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const;
    void PaintCursorCoordinates(const FGeometry& InGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FWorldSweepViewBox& InView) const;

private:

    TWeakObjectPtr<UWorld> World;
    TAttribute<float> CellSize;
    TAttribute<FBox> SweepArea;
    FOnSweepAreaChanged OnSweepAreaChanged;

    FBox WorldBounds;

    FVector2D ViewCenter;
    double ZoomLevel;

    bool bIsDragging;
    FVector2D DragStartWorld;
    FVector2D DragCurrentWorld;
    bool bShiftHeld;

    bool bIsPanning;
    FVector2D LastPanMouseLocal;

    FVector2D LastMouseLocal;

    FDelegateHandle MapChangeHandle;
};
