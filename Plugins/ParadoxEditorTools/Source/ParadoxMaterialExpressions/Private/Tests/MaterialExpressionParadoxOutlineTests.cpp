#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Materials/MaterialExpressionParadoxOutline.h"

#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

namespace
{
    enum class ETestOutlineCategory : uint8
    {
        None,
        PuzzleInput,
        PuzzleOutput,
        Hover,
        Selection,
    };

    ETestOutlineCategory ClassifyStencilForContractTest(
        const float Stencil,
        const UMaterialExpressionParadoxOutline& Expression)
    {
        const float SelectionMinimum = FMath::Min(
            Expression.DefaultSelectionStencilMin,
            Expression.DefaultSelectionStencilMax);
        const float SelectionMaximum = FMath::Max(
            Expression.DefaultSelectionStencilMin,
            Expression.DefaultSelectionStencilMax);

        if (Stencil >= SelectionMinimum && Stencil <= SelectionMaximum)
        {
            return ETestOutlineCategory::Selection;
        }

        const float HoverMinimum = FMath::Min(
            Expression.DefaultHoverStencilMin,
            Expression.DefaultHoverStencilMax);
        const float HoverMaximum = FMath::Max(
            Expression.DefaultHoverStencilMin,
            Expression.DefaultHoverStencilMax);
        if (Stencil >= HoverMinimum && Stencil <= HoverMaximum)
        {
            return ETestOutlineCategory::Hover;
        }

        const float PuzzleOutputMinimum = FMath::Min(
            Expression.DefaultPuzzleOutputStencilMin,
            Expression.DefaultPuzzleOutputStencilMax);
        const float PuzzleOutputMaximum = FMath::Max(
            Expression.DefaultPuzzleOutputStencilMin,
            Expression.DefaultPuzzleOutputStencilMax);
        if (Stencil >= PuzzleOutputMinimum && Stencil <= PuzzleOutputMaximum)
        {
            return ETestOutlineCategory::PuzzleOutput;
        }

        const float PuzzleInputMinimum = FMath::Min(
            Expression.DefaultPuzzleInputStencilMin,
            Expression.DefaultPuzzleInputStencilMax);
        const float PuzzleInputMaximum = FMath::Max(
            Expression.DefaultPuzzleInputStencilMin,
            Expression.DefaultPuzzleInputStencilMax);

        return Stencil >= PuzzleInputMinimum && Stencil <= PuzzleInputMaximum
            ? ETestOutlineCategory::PuzzleInput
            : ETestOutlineCategory::None;
    }

    bool IsDepthSampleAllowedForContractTest(
        const EParadoxOutlineOcclusionMode Mode,
        const float CustomDepth,
        const float SceneDepth,
        const float Bias)
    {
        const bool bVisible = CustomDepth <= SceneDepth + FMath::Max(0.0f, Bias);
        switch (Mode)
        {
        case EParadoxOutlineOcclusionMode::ThroughWalls:
            return true;
        case EParadoxOutlineOcclusionMode::OccludedOnly:
            return !bVisible;
        case EParadoxOutlineOcclusionMode::VisibleOnly:
        default:
            return bVisible;
        }
    }

    float ResolveCenterCoverageForContractTest(
        const ETestOutlineCategory Category,
        const float BoundaryCoverage,
        const bool bCategoryVisible)
    {
        const bool bFillInterior = Category == ETestOutlineCategory::PuzzleInput
            || Category == ETestOutlineCategory::PuzzleOutput;
        return FMath::Max(
            BoundaryCoverage,
            bFillInterior && bCategoryVisible ? 1.0f : 0.0f);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FParadoxOutlineMaterialExpressionContractTest,
    "Paradox.MaterialExpressions.Outline.Contract",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FParadoxOutlineMaterialExpressionContractTest::RunTest(
    const FString& Parameters)
{
    UMaterialExpressionParadoxOutline* Expression =
        NewObject<UMaterialExpressionParadoxOutline>();

    TestNotNull(TEXT("The expression can be constructed"), Expression);
    if (!Expression)
    {
        return false;
    }

    TestEqual(TEXT("Input count"), Expression->CountInputs(), 21);
    TestEqual(TEXT("Output count"), Expression->Outputs.Num(), 6);
    TestEqual(
        TEXT("Creation name"),
        Expression->GetCreationName().ToString(),
        FString(TEXT("Paradox Outline")));

    static const TCHAR* ExpectedInputNames[] =
    {
        TEXT("Thickness"),
        TEXT("Softness"),
        TEXT("DepthThreshold"),
        TEXT("StencilBoundaryStrength"),
        TEXT("HoverIntensity"),
        TEXT("SelectionIntensity"),
        TEXT("HoverColor"),
        TEXT("SelectionColor"),
        TEXT("OcclusionBias"),
        TEXT("HoverStencilMin"),
        TEXT("HoverStencilMax"),
        TEXT("SelectionStencilMin"),
        TEXT("SelectionStencilMax"),
        TEXT("PuzzleInputIntensity"),
        TEXT("PuzzleOutputIntensity"),
        TEXT("PuzzleInputColor"),
        TEXT("PuzzleOutputColor"),
        TEXT("PuzzleInputStencilMin"),
        TEXT("PuzzleInputStencilMax"),
        TEXT("PuzzleOutputStencilMin"),
        TEXT("PuzzleOutputStencilMax"),
    };

    for (int32 InputIndex = 0;
        InputIndex < UE_ARRAY_COUNT(ExpectedInputNames);
        ++InputIndex)
    {
        TestEqual(
            FString::Printf(TEXT("Input %d name"), InputIndex),
            Expression->GetInputName(InputIndex).ToString(),
            FString(ExpectedInputNames[InputIndex]));
    }

    static const TCHAR* ExpectedOutputNames[] =
    {
        TEXT("HoverMask"),
        TEXT("SelectionMask"),
        TEXT("CombinedMask"),
        TEXT("OutlineColor"),
        TEXT("PuzzleInputMask"),
        TEXT("PuzzleOutputMask"),
    };

    for (int32 OutputIndex = 0;
        OutputIndex < UE_ARRAY_COUNT(ExpectedOutputNames);
        ++OutputIndex)
    {
        TestEqual(
            FString::Printf(TEXT("Output %d name"), OutputIndex),
            Expression->Outputs[OutputIndex].OutputName.ToString(),
            FString(ExpectedOutputNames[OutputIndex]));
    }

    TestEqual(
        TEXT("HoverMask output type"),
        Expression->GetOutputValueType(0),
        MCT_Float1);
    TestEqual(
        TEXT("SelectionMask output type"),
        Expression->GetOutputValueType(1),
        MCT_Float1);
    TestEqual(
        TEXT("CombinedMask output type"),
        Expression->GetOutputValueType(2),
        MCT_Float1);
    TestEqual(
        TEXT("OutlineColor output type"),
        Expression->GetOutputValueType(3),
        MCT_Float3);
    TestEqual(TEXT("PuzzleInputMask output type"), Expression->GetOutputValueType(4), MCT_Float1);
    TestEqual(TEXT("PuzzleOutputMask output type"), Expression->GetOutputValueType(5), MCT_Float1);
    TestEqual(
        TEXT("HoverColor input type"),
        Expression->GetInputValueType(6),
        MCT_Float3);
    TestEqual(
        TEXT("SelectionColor input type"),
        Expression->GetInputValueType(7),
        MCT_Float3);
    TestEqual(TEXT("PuzzleInputColor input type"), Expression->GetInputValueType(15), MCT_Float3);
    TestEqual(TEXT("PuzzleOutputColor input type"), Expression->GetInputValueType(16), MCT_Float3);

    TestEqual(TEXT("Default thickness"), Expression->DefaultThickness, 1.0f);
    TestEqual(TEXT("Default softness"), Expression->DefaultSoftness, 0.0f);
    TestEqual(
        TEXT("Default stencil boundary strength"),
        Expression->DefaultStencilBoundaryStrength,
        1.0f);
    TestEqual(
        TEXT("Default Hover minimum"),
        Expression->DefaultHoverStencilMin,
        230.0f);
    TestEqual(
        TEXT("Default Hover maximum"),
        Expression->DefaultHoverStencilMax,
        239.0f);
    TestEqual(
        TEXT("Default Selection minimum"),
        Expression->DefaultSelectionStencilMin,
        240.0f);
    TestEqual(
        TEXT("Default Selection maximum"),
        Expression->DefaultSelectionStencilMax,
        249.0f);
    TestEqual(TEXT("Default Puzzle Input minimum"), Expression->DefaultPuzzleInputStencilMin, 210.0f);
    TestEqual(TEXT("Default Puzzle Input maximum"), Expression->DefaultPuzzleInputStencilMax, 219.0f);
    TestEqual(TEXT("Default Puzzle Output minimum"), Expression->DefaultPuzzleOutputStencilMin, 220.0f);
    TestEqual(TEXT("Default Puzzle Output maximum"), Expression->DefaultPuzzleOutputStencilMax, 229.0f);
    TestEqual(
        TEXT("Default occlusion mode"),
        Expression->OcclusionMode,
        EParadoxOutlineOcclusionMode::VisibleOnly);
    TestEqual(
        TEXT("Puzzle wires default to occluded-only outline"),
        Expression->PuzzleWireOcclusionMode,
        EParadoxOutlineOcclusionMode::OccludedOnly);
    TestEqual(TEXT("VisibleOnly serialized ordinal remains zero"),
        static_cast<uint8>(EParadoxOutlineOcclusionMode::VisibleOnly), uint8(0));
    TestEqual(TEXT("ThroughWalls serialized ordinal remains one"),
        static_cast<uint8>(EParadoxOutlineOcclusionMode::ThroughWalls), uint8(1));
    TestEqual(TEXT("OccludedOnly is append-only"),
        static_cast<uint8>(EParadoxOutlineOcclusionMode::OccludedOnly), uint8(2));
    TestFalse(TEXT("Visible wire sample is suppressed by OccludedOnly"),
        IsDepthSampleAllowedForContractTest(
            Expression->PuzzleWireOcclusionMode, 100.0f, 100.0f, Expression->DefaultOcclusionBias));
    TestTrue(TEXT("Wire behind opaque Scene Depth is retained by OccludedOnly"),
        IsDepthSampleAllowedForContractTest(
            Expression->PuzzleWireOcclusionMode, 200.0f, 100.0f, Expression->DefaultOcclusionBias));
    TestTrue(TEXT("Hover/Selection VisibleOnly remains visible when unobstructed"),
        IsDepthSampleAllowedForContractTest(
            Expression->OcclusionMode, 100.0f, 100.0f, Expression->DefaultOcclusionBias));
    TestFalse(TEXT("Hover/Selection VisibleOnly remains hidden behind opaque depth"),
        IsDepthSampleAllowedForContractTest(
            Expression->OcclusionMode, 200.0f, 100.0f, Expression->DefaultOcclusionBias));
    TestFalse(
        TEXT("Internal depth edges are disabled by default"),
        Expression->bEnableInternalDepthEdges);
    TestEqual(
        TEXT("Puzzle Input center is filled without requiring an edge"),
        ResolveCenterCoverageForContractTest(
            ETestOutlineCategory::PuzzleInput,
            0.0f,
            true),
        1.0f);
    TestEqual(
        TEXT("Puzzle Output center is filled without requiring an edge"),
        ResolveCenterCoverageForContractTest(
            ETestOutlineCategory::PuzzleOutput,
            0.0f,
            true),
        1.0f);
    TestEqual(
        TEXT("Hover center remains outline-only"),
        ResolveCenterCoverageForContractTest(
            ETestOutlineCategory::Hover,
            0.0f,
            true),
        0.0f);
    TestEqual(
        TEXT("Selection center remains outline-only"),
        ResolveCenterCoverageForContractTest(
            ETestOutlineCategory::Selection,
            0.0f,
            true),
        0.0f);
    TestEqual(
        TEXT("Wire center fill obeys its occlusion policy"),
        ResolveCenterCoverageForContractTest(
            ETestOutlineCategory::PuzzleInput,
            0.0f,
            false),
        0.0f);

    TestEqual(
        TEXT("Stencil 230 is Hover"),
        ClassifyStencilForContractTest(230.0f, *Expression),
        ETestOutlineCategory::Hover);
    TestEqual(
        TEXT("Stencil 239 is Hover"),
        ClassifyStencilForContractTest(239.0f, *Expression),
        ETestOutlineCategory::Hover);
    TestEqual(
        TEXT("Stencil 240 is Selection"),
        ClassifyStencilForContractTest(240.0f, *Expression),
        ETestOutlineCategory::Selection);
    TestEqual(
        TEXT("Stencil 249 is Selection"),
        ClassifyStencilForContractTest(249.0f, *Expression),
        ETestOutlineCategory::Selection);
    TestEqual(TEXT("Stencil 210 is Puzzle Input"), ClassifyStencilForContractTest(210.0f, *Expression), ETestOutlineCategory::PuzzleInput);
    TestEqual(TEXT("Stencil 219 is Puzzle Input"), ClassifyStencilForContractTest(219.0f, *Expression), ETestOutlineCategory::PuzzleInput);
    TestEqual(TEXT("Stencil 220 is Puzzle Output"), ClassifyStencilForContractTest(220.0f, *Expression), ETestOutlineCategory::PuzzleOutput);
    TestEqual(TEXT("Stencil 229 is Puzzle Output"), ClassifyStencilForContractTest(229.0f, *Expression), ETestOutlineCategory::PuzzleOutput);

    static const float OutsideValues[] =
    {
        0.0f,
        1.0f,
        100.0f,
        250.0f,
        255.0f,
    };

    for (const float OutsideValue : OutsideValues)
    {
        TestEqual(
            FString::Printf(
                TEXT("Stencil %.0f is outside all ranges"),
                OutsideValue),
            ClassifyStencilForContractTest(OutsideValue, *Expression),
            ETestOutlineCategory::None);
    }

    TestEqual(
        TEXT("Different Hover IDs share one semantic category"),
        ClassifyStencilForContractTest(230.0f, *Expression),
        ClassifyStencilForContractTest(231.0f, *Expression));
    TestEqual(
        TEXT("Different Selection IDs share one semantic category"),
        ClassifyStencilForContractTest(240.0f, *Expression),
        ClassifyStencilForContractTest(241.0f, *Expression));

    Expression->DefaultHoverStencilMin = 240.0f;
    Expression->DefaultHoverStencilMax = 245.0f;
    TestEqual(
        TEXT("Selection has priority when custom ranges overlap"),
        ClassifyStencilForContractTest(242.0f, *Expression),
        ETestOutlineCategory::Selection);

    Expression->DefaultHoverStencilMin = 220.0f;
    Expression->DefaultHoverStencilMax = 225.0f;
    TestEqual(
        TEXT("Hover has priority when custom ranges overlap Puzzle Output"),
        ClassifyStencilForContractTest(222.0f, *Expression),
        ETestOutlineCategory::Hover);
    Expression->DefaultHoverStencilMin = 230.0f;
    Expression->DefaultHoverStencilMax = 239.0f;
    Expression->DefaultPuzzleOutputStencilMin = 210.0f;
    Expression->DefaultPuzzleOutputStencilMax = 215.0f;
    TestEqual(
        TEXT("Puzzle Output has priority when custom ranges overlap Puzzle Input"),
        ClassifyStencilForContractTest(212.0f, *Expression),
        ETestOutlineCategory::PuzzleOutput);

    UMaterial* PostProcessMaterial = NewObject<UMaterial>();
    PostProcessMaterial->MaterialDomain = MD_PostProcess;
    TestTrue(
        TEXT("The node is allowed in Post Process materials"),
        Expression->IsAllowedIn(PostProcessMaterial));

    UMaterial* SurfaceMaterial = NewObject<UMaterial>();
    SurfaceMaterial->MaterialDomain = MD_Surface;
    TestFalse(
        TEXT("The node is rejected in Surface materials"),
        Expression->IsAllowedIn(SurfaceMaterial));

    return true;
}

#endif
