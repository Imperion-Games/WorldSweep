// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

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
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual bool SupportsKeyboardFocus() const override { return false; }
    //~ End SWidget Interface

private:

    void OnEditorMapChanged(uint32 ChangeType);

    struct FViewBox
    {
        FVector2D Min;
        FVector2D Max;
        FVector2D Size() const { return Max - Min; }
    };

    FViewBox GetViewBox(const FGeometry& InGeometry) const;
    FVector2D WorldToLocal(const FVector2D& InWorldXY, const FViewBox& InView, const FVector2D& InLocalSize) const;
    FVector2D LocalToWorld(const FVector2D& InLocalPos, const FViewBox& InView, const FVector2D& InLocalSize) const;

    void PaintBackground(const FGeometry& Geo, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;
    void PaintCellGrid(const FGeometry& Geo, FSlateWindowElementList& OutDrawElements, int32& LayerId, const FViewBox& View) const;
    void PaintLoadedRegions(const FGeometry& Geo, FSlateWindowElementList& OutDrawElements, int32& LayerId, const FViewBox& View) const;
    void PaintSweepArea(const FGeometry& Geo, FSlateWindowElementList& OutDrawElements, int32& LayerId, const FViewBox& View) const;
    void PaintDragPreview(const FGeometry& Geo, FSlateWindowElementList& OutDrawElements, int32& LayerId, const FViewBox& View) const;
    void PaintCursorCoordinates(const FGeometry& Geo, FSlateWindowElementList& OutDrawElements, int32& LayerId, const FViewBox& View) const;

private:

    TWeakObjectPtr<UWorld> World;
    TAttribute<float> CellSize;
    TAttribute<FBox> SweepArea;
    FOnSweepAreaChanged OnSweepAreaChanged;

    FBox WorldBounds;

    FVector2D ViewCenter;
    float ZoomLevel;

    bool bIsDragging;
    FVector2D DragStartWorld;
    FVector2D DragCurrentWorld;
    bool bShiftHeld;

    bool bIsPanning;
    FVector2D LastPanMouseLocal;

    FVector2D LastMouseLocal;

    FDelegateHandle MapChangeHandle;
};
