#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionParadoxOutline.generated.h"

UENUM()
enum class EParadoxOutlineOcclusionMode : uint8
{
    VisibleOnly UMETA(
        DisplayName = "Visible Only",
        ToolTip = "Suppress outline samples whose Custom Depth surface is behind opaque Scene Depth."),

    ThroughWalls UMETA(
        DisplayName = "Through Walls",
        ToolTip = "Keep outline samples visible when their Custom Depth surface is behind opaque geometry."),
};

/**
 * Generates separate Hover and Selection post-process outline masks from
 * Custom Depth and semantic Custom Stencil ranges.
 */
UCLASS(
    collapsecategories,
    hidecategories = (Object),
    meta = (DisplayName = "Paradox Outline"))
class PARADOXMATERIALEXPRESSIONS_API UMaterialExpressionParadoxOutline final
    : public UMaterialExpression
{
    GENERATED_BODY()

public:
    explicit UMaterialExpressionParadoxOutline(
        const FObjectInitializer& ObjectInitializer);

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Thickness property when unconnected."))
    FExpressionInput Thickness;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Softness property when unconnected."))
    FExpressionInput Softness;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Depth Threshold property when unconnected."))
    FExpressionInput DepthThreshold;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Stencil Boundary Strength property when unconnected."))
    FExpressionInput StencilBoundaryStrength;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Hover Intensity property when unconnected."))
    FExpressionInput HoverIntensity;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Selection Intensity property when unconnected."))
    FExpressionInput SelectionIntensity;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Hover Color property when unconnected."))
    FExpressionInput HoverColor;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Selection Color property when unconnected."))
    FExpressionInput SelectionColor;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Occlusion Bias property when unconnected."))
    FExpressionInput OcclusionBias;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Hover Stencil Min property when unconnected."))
    FExpressionInput HoverStencilMin;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Hover Stencil Max property when unconnected."))
    FExpressionInput HoverStencilMax;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Selection Stencil Min property when unconnected."))
    FExpressionInput SelectionStencilMin;

    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to the Selection Stencil Max property when unconnected."))
    FExpressionInput SelectionStencilMax;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Thickness", ClampMin = "0.0", ClampMax = "8.0", UIMin = "0.0", UIMax = "8.0"))
    float DefaultThickness = 1.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Softness", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float DefaultSoftness = 0.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Depth Threshold", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float DefaultDepthThreshold = 10.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Stencil Boundary Strength", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float DefaultStencilBoundaryStrength = 1.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Hover Intensity", ClampMin = "0.0", ClampMax = "16.0", UIMin = "0.0", UIMax = "4.0"))
    float DefaultHoverIntensity = 1.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Selection Intensity", ClampMin = "0.0", ClampMax = "16.0", UIMin = "0.0", UIMax = "4.0"))
    float DefaultSelectionIntensity = 1.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Hover Color"))
    FLinearColor DefaultHoverColor = FLinearColor::White;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Selection Color"))
    FLinearColor DefaultSelectionColor = FLinearColor::White;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Input Defaults",
        meta = (DisplayName = "Occlusion Bias", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float DefaultOcclusionBias = 0.1f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Stencil Ranges",
        meta = (DisplayName = "Hover Stencil Min", ClampMin = "0.0", ClampMax = "255.0", UIMin = "0.0", UIMax = "255.0"))
    float DefaultHoverStencilMin = 230.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Stencil Ranges",
        meta = (DisplayName = "Hover Stencil Max", ClampMin = "0.0", ClampMax = "255.0", UIMin = "0.0", UIMax = "255.0"))
    float DefaultHoverStencilMax = 239.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Stencil Ranges",
        meta = (DisplayName = "Selection Stencil Min", ClampMin = "0.0", ClampMax = "255.0", UIMin = "0.0", UIMax = "255.0"))
    float DefaultSelectionStencilMin = 240.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Stencil Ranges",
        meta = (DisplayName = "Selection Stencil Max", ClampMin = "0.0", ClampMax = "255.0", UIMin = "0.0", UIMax = "255.0"))
    float DefaultSelectionStencilMax = 249.0f;

    UPROPERTY(EditAnywhere, Category = "Paradox Outline")
    EParadoxOutlineOcclusionMode OcclusionMode =
        EParadoxOutlineOcclusionMode::VisibleOnly;

    UPROPERTY(
        EditAnywhere,
        Category = "Paradox Outline|Depth",
        meta = (
            DisplayName = "Enable Internal Depth Edges",
            ToolTip = "When enabled, Depth Threshold may add edges at real depth breaks inside one highlighted stencil category. Disabled by default to keep solid surfaces silhouette-only."))
    bool bEnableInternalDepthEdges = false;

    //~ Begin UMaterialExpression Interface
    virtual bool IsAllowedIn(const UObject* MaterialOrFunction) const override;

#if WITH_EDITOR
    virtual int32 Compile(
        FMaterialCompiler* Compiler,
        int32 OutputIndex) override;
    virtual EMaterialValueType GetInputValueType(int32 InputIndex) override;
    virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual FText GetCreationDescription() const override;
    virtual FText GetCreationName() const override;
    virtual FText GetKeywords() const override;
    virtual void GetConnectorToolTip(
        int32 InputIndex,
        int32 OutputIndex,
        TArray<FString>& OutToolTip) override;
#endif
    //~ End UMaterialExpression Interface
};
