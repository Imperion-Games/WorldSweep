// Copyright © ToaGames. All Rights Reserved.

#include "UI/SWorldSweepMapView.h"
#include "WorldPartition/WorldPartition.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Engine/World.h"
#include "Editor.h"

// Maximum number of cell lines to draw before skipping the grid (avoids overdraw on huge worlds at low zoom).
static constexpr int32 MaxCellGridLines = 300;

void SWorldSweepMapView::Construct(const FArguments& InArgs)
{
    World              = InArgs._InWorld;
    CellSize           = InArgs._CellSize;
    SweepArea          = InArgs._SweepArea;
    OnSweepAreaChanged = InArgs._OnSweepAreaChanged;
    bIsDragging        = false;
    bIsPanning         = false;
    bShiftHeld         = false;
    WorldBounds.Init();

    if (UWorld* W = World.Get())
    {
        if (UWorldPartition* WP = W->GetWorldPartition())
        {
            WorldBounds = WP->GetEditorWorldBounds();
        }
    }

    if (!WorldBounds.IsValid)
    {
        WorldBounds = FBox(FVector(-512000.f, -512000.f, 0.f), FVector(512000.f, 512000.f, 0.f));
    }

    ViewCenter = FVector2D(WorldBounds.GetCenter().X, WorldBounds.GetCenter().Y);
    ZoomLevel  = 1.0f;

    SetClipping(EWidgetClipping::ClipToBounds);

    MapChangeHandle = FEditorDelegates::MapChange.AddRaw(this, &SWorldSweepMapView::OnEditorMapChanged);
}

SWorldSweepMapView::~SWorldSweepMapView()
{
    FEditorDelegates::MapChange.Remove(MapChangeHandle);
}

void SWorldSweepMapView::OnEditorMapChanged(uint32 /*ChangeType*/)
{
    UWorld* NewWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    World = NewWorld;
    WorldBounds.Init();

    if (NewWorld)
    {
        if (UWorldPartition* WP = NewWorld->GetWorldPartition())
        {
            WorldBounds = WP->GetEditorWorldBounds();
        }
    }

    if (!WorldBounds.IsValid)
    {
        WorldBounds = FBox(FVector(-512000.f, -512000.f, 0.f), FVector(512000.f, 512000.f, 0.f));
    }

    ViewCenter  = FVector2D(WorldBounds.GetCenter().X, WorldBounds.GetCenter().Y);
    ZoomLevel   = 1.0f;
    bIsDragging = false;
    bIsPanning  = false;
    bShiftHeld  = false;
}

// View helpers

SWorldSweepMapView::FViewBox SWorldSweepMapView::GetViewBox(const FGeometry& InGeometry) const
{
    const FVector2D LocalSz = InGeometry.GetLocalSize();
    if (LocalSz.X <= 0.f || LocalSz.Y <= 0.f)
    {
        return { ViewCenter, ViewCenter };
    }

    // Compute a single uniform world-units-per-pixel scale so both axes use the same
    // ratio — this prevents aspect-ratio distortion when the widget is non-square.
    // We pick the scale that makes the larger world dimension just fit the widget at ZoomLevel 1.
    const float ScaleX       = WorldBounds.GetSize().X / LocalSz.X;
    const float ScaleY       = WorldBounds.GetSize().Y / LocalSz.Y;
    const float WorldPerPixel = FMath::Max(ScaleX, ScaleY) / ZoomLevel;

    const FVector2D HalfSize = LocalSz * 0.5f * WorldPerPixel;
    return { ViewCenter - HalfSize, ViewCenter + HalfSize };
}

FVector2D SWorldSweepMapView::WorldToLocal(const FVector2D& InWorldXY, const FViewBox& InView, const FVector2D& InLocalSize) const
{
    const FVector2D ViewSize = InView.Size();
    return FVector2D(
        (InWorldXY.X - InView.Min.X) / ViewSize.X * InLocalSize.X,
        (InWorldXY.Y - InView.Min.Y) / ViewSize.Y * InLocalSize.Y
    );
}

FVector2D SWorldSweepMapView::LocalToWorld(const FVector2D& InLocalPos, const FViewBox& InView, const FVector2D& InLocalSize) const
{
    const FVector2D ViewSize = InView.Size();
    return FVector2D(
        InView.Min.X + (InLocalPos.X / InLocalSize.X) * ViewSize.X,
        InView.Min.Y + (InLocalPos.Y / InLocalSize.Y) * ViewSize.Y
    );
}

// Paint

int32 SWorldSweepMapView::OnPaint(
    const FPaintArgs& Args,
    const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32 LayerId,
    const FWidgetStyle& InWidgetStyle,
    bool bParentEnabled) const
{
    const FViewBox View = GetViewBox(AllottedGeometry);

    PaintBackground(AllottedGeometry, OutDrawElements, LayerId);
    PaintCellGrid(AllottedGeometry, OutDrawElements, LayerId, View);
    PaintLoadedRegions(AllottedGeometry, OutDrawElements, LayerId, View);
    PaintSweepArea(AllottedGeometry, OutDrawElements, LayerId, View);
    PaintDragPreview(AllottedGeometry, OutDrawElements, LayerId, View);
    PaintCursorCoordinates(AllottedGeometry, OutDrawElements, LayerId, View);

    return LayerId;
}

void SWorldSweepMapView::PaintBackground(const FGeometry& Geo, FSlateWindowElementList& Out, int32& LayerId) const
{
    const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");

    // Dark background
    FSlateDrawElement::MakeBox(Out, LayerId, Geo.ToPaintGeometry(), White,
        ESlateDrawEffect::None, FLinearColor(0.04f, 0.04f, 0.04f, 1.0f));
    ++LayerId;

    // World bounds fill (slightly lighter)
    const FViewBox View     = GetViewBox(Geo);
    const FVector2D LocalSz = Geo.GetLocalSize();
    const FVector2D WMin    = WorldToLocal(FVector2D(WorldBounds.Min.X, WorldBounds.Min.Y), View, LocalSz);
    const FVector2D WMax    = WorldToLocal(FVector2D(WorldBounds.Max.X, WorldBounds.Max.Y), View, LocalSz);
    const FVector2D WSize   = WMax - WMin;

    if (WSize.X > 0.f && WSize.Y > 0.f)
    {
        FSlateDrawElement::MakeBox(Out, LayerId,
            Geo.ToPaintGeometry(WSize, FSlateLayoutTransform(WMin)),
            White, ESlateDrawEffect::None, FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));
        ++LayerId;

        // World bounds outline
        const TArray<FVector2D> Border = { WMin, FVector2D(WMax.X, WMin.Y), WMax, FVector2D(WMin.X, WMax.Y), WMin };
        FSlateDrawElement::MakeLines(Out, LayerId, Geo.ToPaintGeometry(), Border,
            ESlateDrawEffect::None, FLinearColor(0.25f, 0.25f, 0.25f, 1.0f), true, 1.0f);
        ++LayerId;
    }
}

void SWorldSweepMapView::PaintCellGrid(const FGeometry& Geo, FSlateWindowElementList& Out, int32& LayerId, const FViewBox& View) const
{
    const float CS       = FMath::Max(CellSize.Get(), 1.0f);
    const FVector2D LocalSz = Geo.GetLocalSize();

    // Count how many lines would be visible — skip if too dense
    const float WorldW = WorldBounds.GetSize().X;
    const float WorldH = WorldBounds.GetSize().Y;
    const int32 NumX   = FMath::CeilToInt(WorldW / CS) + 1;
    const int32 NumY   = FMath::CeilToInt(WorldH / CS) + 1;
    if (NumX + NumY > MaxCellGridLines)
    {
        return;
    }

    const FLinearColor GridColor(0.15f, 0.15f, 0.15f, 1.0f);

    // Vertical lines
    const float StartX = FMath::GridSnap(WorldBounds.Min.X, CS);
    for (float X = StartX; X <= WorldBounds.Max.X; X += CS)
    {
        const FVector2D A = WorldToLocal(FVector2D(X, WorldBounds.Min.Y), View, LocalSz);
        const FVector2D B = WorldToLocal(FVector2D(X, WorldBounds.Max.Y), View, LocalSz);
        FSlateDrawElement::MakeLines(Out, LayerId, Geo.ToPaintGeometry(),
            TArray<FVector2D>{ A, B }, ESlateDrawEffect::None, GridColor, true, 0.5f);
    }

    // Horizontal lines
    const float StartY = FMath::GridSnap(WorldBounds.Min.Y, CS);
    for (float Y = StartY; Y <= WorldBounds.Max.Y; Y += CS)
    {
        const FVector2D A = WorldToLocal(FVector2D(WorldBounds.Min.X, Y), View, LocalSz);
        const FVector2D B = WorldToLocal(FVector2D(WorldBounds.Max.X, Y), View, LocalSz);
        FSlateDrawElement::MakeLines(Out, LayerId, Geo.ToPaintGeometry(),
            TArray<FVector2D>{ A, B }, ESlateDrawEffect::None, GridColor, true, 0.5f);
    }

    ++LayerId;
}

void SWorldSweepMapView::PaintLoadedRegions(const FGeometry& Geo, FSlateWindowElementList& Out, int32& LayerId, const FViewBox& View) const
{
    UWorld* W = World.Get();
    if (!W)
    {
        return;
    }

    UWorldPartition* WP = W->GetWorldPartition();
    if (!WP)
    {
        return;
    }

    const FSlateBrush* White    = FCoreStyle::Get().GetBrush("WhiteBrush");
    const FVector2D    LocalSz  = Geo.GetLocalSize();
    const FLinearColor FillColor(0.1f, 0.35f, 0.1f, 0.3f);
    const FLinearColor BorderColor(0.2f, 0.7f, 0.2f, 0.8f);

    for (const FBox& Region : WP->GetUserLoadedEditorRegions())
    {
        const FVector2D RMin  = WorldToLocal(FVector2D(Region.Min.X, Region.Min.Y), View, LocalSz);
        const FVector2D RMax  = WorldToLocal(FVector2D(Region.Max.X, Region.Max.Y), View, LocalSz);
        const FVector2D RSize = RMax - RMin;

        if (RSize.X <= 0.f || RSize.Y <= 0.f)
        {
            continue;
        }

        FSlateDrawElement::MakeBox(Out, LayerId,
            Geo.ToPaintGeometry(RSize, FSlateLayoutTransform(RMin)),
            White, ESlateDrawEffect::None, FillColor);
        ++LayerId;

        const TArray<FVector2D> Border = { RMin, FVector2D(RMax.X, RMin.Y), RMax, FVector2D(RMin.X, RMax.Y), RMin };
        FSlateDrawElement::MakeLines(Out, LayerId, Geo.ToPaintGeometry(), Border,
            ESlateDrawEffect::None, BorderColor, true, 1.0f);
        ++LayerId;
    }
}

void SWorldSweepMapView::PaintSweepArea(const FGeometry& Geo, FSlateWindowElementList& Out, int32& LayerId, const FViewBox& View) const
{
    const FBox Current = SweepArea.Get();
    if (!Current.IsValid || bIsDragging)
    {
        return;
    }

    const FSlateBrush* White   = FCoreStyle::Get().GetBrush("WhiteBrush");
    const FVector2D    LocalSz = Geo.GetLocalSize();
    const FVector2D    RMin    = WorldToLocal(FVector2D(Current.Min.X, Current.Min.Y), View, LocalSz);
    const FVector2D    RMax    = WorldToLocal(FVector2D(Current.Max.X, Current.Max.Y), View, LocalSz);
    const FVector2D    RSize   = RMax - RMin;

    if (RSize.X <= 0.f || RSize.Y <= 0.f)
    {
        return;
    }

    FSlateDrawElement::MakeBox(Out, LayerId,
        Geo.ToPaintGeometry(RSize, FSlateLayoutTransform(RMin)),
        White, ESlateDrawEffect::None, FLinearColor(0.2f, 0.5f, 1.0f, 0.2f));
    ++LayerId;

    const TArray<FVector2D> Border = { RMin, FVector2D(RMax.X, RMin.Y), RMax, FVector2D(RMin.X, RMax.Y), RMin };
    FSlateDrawElement::MakeLines(Out, LayerId, Geo.ToPaintGeometry(), Border,
        ESlateDrawEffect::None, FLinearColor(0.2f, 0.5f, 1.0f, 1.0f), true, 1.5f);
    ++LayerId;
}

void SWorldSweepMapView::PaintDragPreview(const FGeometry& Geo, FSlateWindowElementList& Out, int32& LayerId, const FViewBox& View) const
{
    if (!bIsDragging)
    {
        return;
    }

    const FSlateBrush* White   = FCoreStyle::Get().GetBrush("WhiteBrush");
    const FVector2D    LocalSz = Geo.GetLocalSize();

    FVector2D SnapStart   = DragStartWorld;
    FVector2D SnapCurrent = DragCurrentWorld;
    if (bShiftHeld)
    {
        const float CS = FMath::Max(CellSize.Get(), 1.0f);
        SnapStart.X   = FMath::GridSnap(SnapStart.X,   CS);
        SnapStart.Y   = FMath::GridSnap(SnapStart.Y,   CS);
        SnapCurrent.X = FMath::GridSnap(SnapCurrent.X, CS);
        SnapCurrent.Y = FMath::GridSnap(SnapCurrent.Y, CS);
    }

    const FVector2D    A       = WorldToLocal(SnapStart,   View, LocalSz);
    const FVector2D    B       = WorldToLocal(SnapCurrent, View, LocalSz);
    const FVector2D    RMin(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
    const FVector2D    RMax(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
    const FVector2D    RSize = RMax - RMin;

    if (RSize.X <= 1.f || RSize.Y <= 1.f)
    {
        return;
    }

    FSlateDrawElement::MakeBox(Out, LayerId,
        Geo.ToPaintGeometry(RSize, FSlateLayoutTransform(RMin)),
        White, ESlateDrawEffect::None, FLinearColor(1.0f, 0.75f, 0.1f, 0.15f));
    ++LayerId;

    const TArray<FVector2D> Border = { RMin, FVector2D(RMax.X, RMin.Y), RMax, FVector2D(RMin.X, RMax.Y), RMin };
    FSlateDrawElement::MakeLines(Out, LayerId, Geo.ToPaintGeometry(), Border,
        ESlateDrawEffect::None, FLinearColor(1.0f, 0.75f, 0.1f, 1.0f), true, 1.5f);
    ++LayerId;
}

void SWorldSweepMapView::PaintCursorCoordinates(const FGeometry& Geo, FSlateWindowElementList& Out, int32& LayerId, const FViewBox& View) const
{
    const FVector2D WorldPos = LocalToWorld(LastMouseLocal, View, Geo.GetLocalSize());
    const FString   Text     = FString::Printf(TEXT("%.0f, %.0f"), WorldPos.X, WorldPos.Y);

    const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
    FSlateDrawElement::MakeText(Out, LayerId,
        Geo.ToPaintGeometry(FVector2D(200.f, 14.f), FSlateLayoutTransform(FVector2D(4.f, Geo.GetLocalSize().Y - 16.f))),
        FText::FromString(Text), Font, ESlateDrawEffect::None,
        FLinearColor(0.6f, 0.6f, 0.6f, 1.0f));
    ++LayerId;
}

// Input

FReply SWorldSweepMapView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FViewBox View = GetViewBox(MyGeometry);
        DragStartWorld   = LocalToWorld(LocalPos, View, MyGeometry.GetLocalSize());
        DragCurrentWorld = DragStartWorld;
        bIsDragging      = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        LastPanMouseLocal = LocalPos;
        bIsPanning        = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    return FReply::Unhandled();
}

FReply SWorldSweepMapView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    LastMouseLocal = LocalPos;

    if (bIsDragging)
    {
        const FViewBox View  = GetViewBox(MyGeometry);
        DragCurrentWorld     = LocalToWorld(LocalPos, View, MyGeometry.GetLocalSize());
        bShiftHeld           = MouseEvent.IsShiftDown();
        return FReply::Handled();
    }

    if (bIsPanning)
    {
        const FViewBox   View      = GetViewBox(MyGeometry);
        const FVector2D  LocalSz   = MyGeometry.GetLocalSize();
        const FVector2D  WorldDelta = LocalToWorld(LocalPos, View, LocalSz) - LocalToWorld(LastPanMouseLocal, View, LocalSz);
        ViewCenter       -= WorldDelta;
        LastPanMouseLocal = LocalPos;
        return FReply::Handled();
    }

    return FReply::Handled(); // always consume to keep cursor coordinate updating
}

FReply SWorldSweepMapView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
    {
        bIsDragging = false;

        FVector2D RawStart   = DragStartWorld;
        FVector2D RawCurrent = DragCurrentWorld;

        if (bShiftHeld)
        {
            const float CS = FMath::Max(CellSize.Get(), 1.0f);
            RawStart.X   = FMath::GridSnap(RawStart.X,   CS);
            RawStart.Y   = FMath::GridSnap(RawStart.Y,   CS);
            RawCurrent.X = FMath::GridSnap(RawCurrent.X, CS);
            RawCurrent.Y = FMath::GridSnap(RawCurrent.Y, CS);
        }

        const FVector2D SelectMin(FMath::Min(RawStart.X, RawCurrent.X), FMath::Min(RawStart.Y, RawCurrent.Y));
        const FVector2D SelectMax(FMath::Max(RawStart.X, RawCurrent.X), FMath::Max(RawStart.Y, RawCurrent.Y));

        bShiftHeld = false;

        if ((SelectMax - SelectMin).GetMin() > 100.f) // ignore tiny accidental drags
        {
            // Use the full legal world height so no actors at extreme Z are missed.
            static constexpr float HalfWorldMax = 1048576.0f;
            const FBox NewArea(
                FVector(SelectMin.X, SelectMin.Y, -HalfWorldMax),
                FVector(SelectMax.X, SelectMax.Y,  HalfWorldMax)
            );
            OnSweepAreaChanged.ExecuteIfBound(NewArea);
        }

        return FReply::Handled().ReleaseMouseCapture();
    }

    if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bIsPanning)
    {
        bIsPanning = false;
        return FReply::Handled().ReleaseMouseCapture();
    }

    return FReply::Unhandled();
}

FReply SWorldSweepMapView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    const FVector2D LocalPos  = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    const FViewBox  OldView   = GetViewBox(MyGeometry);
    const FVector2D LocalSz   = MyGeometry.GetLocalSize();
    const FVector2D MouseWorld = LocalToWorld(LocalPos, OldView, LocalSz);

    const float Factor = MouseEvent.GetWheelDelta() > 0.f ? 1.2f : (1.0f / 1.2f);
    ZoomLevel = FMath::Clamp(ZoomLevel * Factor, 0.5f, 64.0f);

    // Recenter so the point under the cursor stays fixed
    const FViewBox NewView   = GetViewBox(MyGeometry);
    const FVector2D NewWorld = LocalToWorld(LocalPos, NewView, LocalSz);
    ViewCenter              -= (NewWorld - MouseWorld);

    return FReply::Handled();
}

FCursorReply SWorldSweepMapView::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
    if (bIsPanning)
    {
        return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
    }
    if (bIsDragging)
    {
        return FCursorReply::Cursor(EMouseCursor::Crosshairs);
    }
    return FCursorReply::Cursor(EMouseCursor::Crosshairs);
}

FVector2D SWorldSweepMapView::ComputeDesiredSize(float) const
{
    return FVector2D(400.0f, 400.0f);
}
