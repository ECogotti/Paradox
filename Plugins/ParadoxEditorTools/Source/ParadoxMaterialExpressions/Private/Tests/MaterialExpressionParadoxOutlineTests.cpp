#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Materials/MaterialExpressionParadoxOutline.h"

#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

namespace
{
    enum class ETestOutlineCategory : uint8
    {
        None,
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

        return Stencil >= HoverMinimum && Stencil <= HoverMaximum
            ? ETestOutlineCategory::Hover
            : ETestOutlineCategory::None;
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

    TestEqual(TEXT("Input count"), Expression->CountInputs(), 13);
    TestEqual(TEXT("Output count"), Expression->Outputs.Num(), 4);
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
    TestEqual(
        TEXT("HoverColor input type"),
        Expression->GetInputValueType(6),
        MCT_Float3);
    TestEqual(
        TEXT("SelectionColor input type"),
        Expression->GetInputValueType(7),
        MCT_Float3);

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
    TestEqual(
        TEXT("Default occlusion mode"),
        Expression->OcclusionMode,
        EParadoxOutlineOcclusionMode::VisibleOnly);
    TestFalse(
        TEXT("Internal depth edges are disabled by default"),
        Expression->bEnableInternalDepthEdges);

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

    static const float OutsideValues[] =
    {
        0.0f,
        1.0f,
        100.0f,
        229.0f,
        250.0f,
        255.0f,
    };

    for (const float OutsideValue : OutsideValues)
    {
        TestEqual(
            FString::Printf(
                TEXT("Stencil %.0f is outside both ranges"),
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
