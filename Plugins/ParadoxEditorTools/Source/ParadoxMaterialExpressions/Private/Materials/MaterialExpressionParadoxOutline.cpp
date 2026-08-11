#include "Materials/MaterialExpressionParadoxOutline.h"

#include "MaterialDomain.h"
#include "Materials/Material.h"

#if WITH_EDITOR
#include "Containers/StaticArray.h"
#include "MaterialCompiler.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialSceneTextureId.h"
#endif

#define LOCTEXT_NAMESPACE "MaterialExpressionParadoxOutline"

namespace
{
    constexpr int32 InputCount = 21;
    constexpr int32 OutputCount = 6;

#if WITH_EDITOR
    enum class EParadoxOutlineInput : int32
    {
        Thickness,
        Softness,
        DepthThreshold,
        StencilBoundaryStrength,
        HoverIntensity,
        SelectionIntensity,
        HoverColor,
        SelectionColor,
        OcclusionBias,
        HoverStencilMin,
        HoverStencilMax,
        SelectionStencilMin,
        SelectionStencilMax,
        PuzzleInputIntensity,
        PuzzleOutputIntensity,
        PuzzleInputColor,
        PuzzleOutputColor,
        PuzzleInputStencilMin,
        PuzzleInputStencilMax,
        PuzzleOutputStencilMin,
        PuzzleOutputStencilMax,
    };

    enum class EParadoxOutlineOutput : int32
    {
        HoverMask,
        SelectionMask,
        CombinedMask,
        OutlineColor,
        PuzzleInputMask,
        PuzzleOutputMask,
    };

    int32 CompileScalarInput(
        FExpressionInput& Input,
        FMaterialCompiler& Compiler,
        const float DefaultValue)
    {
        const int32 Value = Input.Expression
            ? Input.Compile(&Compiler)
            : Compiler.Constant(DefaultValue);

        return Value == INDEX_NONE
            ? INDEX_NONE
            : Compiler.ForceCast(Value, MCT_Float1, MFCF_ForceCast);
    }

    int32 CompileColorInput(
        FExpressionInput& Input,
        FMaterialCompiler& Compiler,
        const FLinearColor& DefaultValue)
    {
        const int32 Value = Input.Expression
            ? Input.Compile(&Compiler)
            : Compiler.Constant3(
                DefaultValue.R,
                DefaultValue.G,
                DefaultValue.B);

        return Value == INDEX_NONE
            ? INDEX_NONE
            : Compiler.ForceCast(Value, MCT_Float3, MFCF_ForceCast);
    }

    bool UsesReservedEditorSelectionColorParameter(
        const FExpressionInput& Input)
    {
        if (!Input.Expression)
        {
            return false;
        }

        const FExpressionInput TracedInput = Input.GetTracedInput();
        const UMaterialExpressionVectorParameter* VectorParameter =
            Cast<UMaterialExpressionVectorParameter>(
                TracedInput.Expression);

        static const FName ReservedParameterName(TEXT("SelectionColor"));
        return VectorParameter
            && VectorParameter->ParameterName == ReservedParameterName;
    }

    int32 CompileInclusiveRangeMask(
        FMaterialCompiler& Compiler,
        const int32 Value,
        const int32 Minimum,
        const int32 Maximum)
    {
        return Compiler.Mul(
            Compiler.Step(Minimum, Value),
            Compiler.Step(Value, Maximum));
    }

    struct FCompiledSample
    {
        int32 CustomDepth = INDEX_NONE;
        int32 SemanticCategory = INDEX_NONE;
        int32 StandardVisibility = INDEX_NONE;
        int32 PuzzleVisibility = INDEX_NONE;
    };

    struct FCompiledOutline
    {
        int32 HoverMask = INDEX_NONE;
        int32 SelectionMask = INDEX_NONE;
        int32 PuzzleInputMask = INDEX_NONE;
        int32 PuzzleOutputMask = INDEX_NONE;
        int32 CombinedMask = INDEX_NONE;
    };

    FCompiledOutline CompileOutlineMasks(
        UMaterialExpressionParadoxOutline& Expression,
        FMaterialCompiler& Compiler)
    {
        const int32 Zero = Compiler.Constant(0.0f);
        const int32 One = Compiler.Constant(1.0f);
        const int32 Two = Compiler.Constant(2.0f);
        const int32 Three = Compiler.Constant(3.0f);
        const int32 Four = Compiler.Constant(4.0f);
        const int32 Half = Compiler.Constant(0.5f);
        const int32 OneAndHalf = Compiler.Constant(1.5f);
        const int32 TwoAndHalf = Compiler.Constant(2.5f);
        const int32 ThreeAndHalf = Compiler.Constant(3.5f);
        const int32 MaxThickness = Compiler.Constant(8.0f);
        const int32 MaxIntensity = Compiler.Constant(16.0f);
        const int32 MaxStencil = Compiler.Constant(255.0f);

        int32 Thickness = CompileScalarInput(
            Expression.Thickness,
            Compiler,
            Expression.DefaultThickness);
        int32 Softness = CompileScalarInput(
            Expression.Softness,
            Compiler,
            Expression.DefaultSoftness);
        int32 DepthThreshold = CompileScalarInput(
            Expression.DepthThreshold,
            Compiler,
            Expression.DefaultDepthThreshold);
        int32 StencilBoundaryStrength = CompileScalarInput(
            Expression.StencilBoundaryStrength,
            Compiler,
            Expression.DefaultStencilBoundaryStrength);
        int32 HoverIntensity = CompileScalarInput(
            Expression.HoverIntensity,
            Compiler,
            Expression.DefaultHoverIntensity);
        int32 SelectionIntensity = CompileScalarInput(
            Expression.SelectionIntensity,
            Compiler,
            Expression.DefaultSelectionIntensity);
        int32 PuzzleInputIntensity = CompileScalarInput(
            Expression.PuzzleInputIntensity,
            Compiler,
            Expression.DefaultPuzzleInputIntensity);
        int32 PuzzleOutputIntensity = CompileScalarInput(
            Expression.PuzzleOutputIntensity,
            Compiler,
            Expression.DefaultPuzzleOutputIntensity);
        int32 OcclusionBias = CompileScalarInput(
            Expression.OcclusionBias,
            Compiler,
            Expression.DefaultOcclusionBias);
        int32 HoverStencilMin = CompileScalarInput(
            Expression.HoverStencilMin,
            Compiler,
            Expression.DefaultHoverStencilMin);
        int32 HoverStencilMax = CompileScalarInput(
            Expression.HoverStencilMax,
            Compiler,
            Expression.DefaultHoverStencilMax);
        int32 SelectionStencilMin = CompileScalarInput(
            Expression.SelectionStencilMin,
            Compiler,
            Expression.DefaultSelectionStencilMin);
        int32 SelectionStencilMax = CompileScalarInput(
            Expression.SelectionStencilMax,
            Compiler,
            Expression.DefaultSelectionStencilMax);
        int32 PuzzleInputStencilMin = CompileScalarInput(
            Expression.PuzzleInputStencilMin,
            Compiler,
            Expression.DefaultPuzzleInputStencilMin);
        int32 PuzzleInputStencilMax = CompileScalarInput(
            Expression.PuzzleInputStencilMax,
            Compiler,
            Expression.DefaultPuzzleInputStencilMax);
        int32 PuzzleOutputStencilMin = CompileScalarInput(
            Expression.PuzzleOutputStencilMin,
            Compiler,
            Expression.DefaultPuzzleOutputStencilMin);
        int32 PuzzleOutputStencilMax = CompileScalarInput(
            Expression.PuzzleOutputStencilMax,
            Compiler,
            Expression.DefaultPuzzleOutputStencilMax);

        if (Thickness == INDEX_NONE
            || Softness == INDEX_NONE
            || DepthThreshold == INDEX_NONE
            || StencilBoundaryStrength == INDEX_NONE
            || HoverIntensity == INDEX_NONE
            || SelectionIntensity == INDEX_NONE
            || PuzzleInputIntensity == INDEX_NONE
            || PuzzleOutputIntensity == INDEX_NONE
            || OcclusionBias == INDEX_NONE
            || HoverStencilMin == INDEX_NONE
            || HoverStencilMax == INDEX_NONE
            || SelectionStencilMin == INDEX_NONE
            || SelectionStencilMax == INDEX_NONE
            || PuzzleInputStencilMin == INDEX_NONE
            || PuzzleInputStencilMax == INDEX_NONE
            || PuzzleOutputStencilMin == INDEX_NONE
            || PuzzleOutputStencilMax == INDEX_NONE)
        {
            return {};
        }

        Thickness = Compiler.Clamp(Thickness, Zero, MaxThickness);
        Softness = Compiler.Saturate(Softness);
        DepthThreshold = Compiler.Max(DepthThreshold, Zero);
        StencilBoundaryStrength = Compiler.Saturate(
            StencilBoundaryStrength);
        HoverIntensity = Compiler.Clamp(
            HoverIntensity,
            Zero,
            MaxIntensity);
        SelectionIntensity = Compiler.Clamp(
            SelectionIntensity,
            Zero,
            MaxIntensity);
        PuzzleInputIntensity = Compiler.Clamp(
            PuzzleInputIntensity,
            Zero,
            MaxIntensity);
        PuzzleOutputIntensity = Compiler.Clamp(
            PuzzleOutputIntensity,
            Zero,
            MaxIntensity);
        OcclusionBias = Compiler.Max(OcclusionBias, Zero);

        HoverStencilMin = Compiler.Clamp(
            HoverStencilMin,
            Zero,
            MaxStencil);
        HoverStencilMax = Compiler.Clamp(
            HoverStencilMax,
            Zero,
            MaxStencil);
        SelectionStencilMin = Compiler.Clamp(
            SelectionStencilMin,
            Zero,
            MaxStencil);
        SelectionStencilMax = Compiler.Clamp(
            SelectionStencilMax,
            Zero,
            MaxStencil);
        PuzzleInputStencilMin = Compiler.Clamp(PuzzleInputStencilMin, Zero, MaxStencil);
        PuzzleInputStencilMax = Compiler.Clamp(PuzzleInputStencilMax, Zero, MaxStencil);
        PuzzleOutputStencilMin = Compiler.Clamp(PuzzleOutputStencilMin, Zero, MaxStencil);
        PuzzleOutputStencilMax = Compiler.Clamp(PuzzleOutputStencilMax, Zero, MaxStencil);

        const int32 HoverMinimum = Compiler.Min(
            HoverStencilMin,
            HoverStencilMax);
        const int32 HoverMaximum = Compiler.Max(
            HoverStencilMin,
            HoverStencilMax);
        const int32 SelectionMinimum = Compiler.Min(
            SelectionStencilMin,
            SelectionStencilMax);
        const int32 SelectionMaximum = Compiler.Max(
            SelectionStencilMin,
            SelectionStencilMax);
        const int32 PuzzleInputMinimum = Compiler.Min(PuzzleInputStencilMin, PuzzleInputStencilMax);
        const int32 PuzzleInputMaximum = Compiler.Max(PuzzleInputStencilMin, PuzzleInputStencilMax);
        const int32 PuzzleOutputMinimum = Compiler.Min(PuzzleOutputStencilMin, PuzzleOutputStencilMax);
        const int32 PuzzleOutputMaximum = Compiler.Max(PuzzleOutputStencilMin, PuzzleOutputStencilMax);

        const int32 ViewportUv = Compiler.GetViewportUV();
        const int32 InverseViewSize = Compiler.ViewProperty(
            MEVP_ViewSize,
            true);

        auto CompileSample = [
            &Compiler,
            ViewportUv,
            InverseViewSize,
            Thickness,
            HoverMinimum,
            HoverMaximum,
            SelectionMinimum,
            SelectionMaximum,
            PuzzleInputMinimum,
            PuzzleInputMaximum,
            PuzzleOutputMinimum,
            PuzzleOutputMaximum,
            OcclusionBias,
            Zero,
            One,
            Two,
            Three,
            Four,
            &Expression](const FVector2f& Direction)
        {
            const int32 DirectionValue = Compiler.Constant2(
                Direction.X,
                Direction.Y);
            const int32 PixelOffset = Compiler.Mul(
                DirectionValue,
                Thickness);
            const int32 UvOffset = Compiler.Mul(
                PixelOffset,
                InverseViewSize);
            const int32 SampleUv = Compiler.Add(
                ViewportUv,
                UvOffset);

            const int32 StencilSample = Compiler.ComponentMask(
                Compiler.SceneTextureLookup(
                    SampleUv,
                    PPI_CustomStencil,
                    false,
                    true,
                    false),
                true,
                false,
                false,
                false);

            const int32 CustomDepthSample = Compiler.ComponentMask(
                Compiler.SceneTextureLookup(
                    SampleUv,
                    PPI_CustomDepth,
                    false,
                    true,
                    false),
                true,
                false,
                false,
                false);

            const int32 IsSelection = CompileInclusiveRangeMask(
                Compiler,
                StencilSample,
                SelectionMinimum,
                SelectionMaximum);
            const int32 IsHoverRange = CompileInclusiveRangeMask(
                Compiler,
                StencilSample,
                HoverMinimum,
                HoverMaximum);
            const int32 IsHover = Compiler.Mul(
                IsHoverRange,
                Compiler.Sub(One, IsSelection));
            const int32 IsPuzzleOutputRange = CompileInclusiveRangeMask(
                Compiler,
                StencilSample,
                PuzzleOutputMinimum,
                PuzzleOutputMaximum);
            const int32 HigherThanPuzzleOutput = Compiler.Saturate(Compiler.Add(IsSelection, IsHover));
            const int32 IsPuzzleOutput = Compiler.Mul(
                IsPuzzleOutputRange,
                Compiler.Sub(One, HigherThanPuzzleOutput));
            const int32 IsPuzzleInputRange = CompileInclusiveRangeMask(
                Compiler,
                StencilSample,
                PuzzleInputMinimum,
                PuzzleInputMaximum);
            const int32 HigherThanPuzzleInput = Compiler.Saturate(
                Compiler.Add(HigherThanPuzzleOutput, IsPuzzleOutput));
            const int32 IsPuzzleInput = Compiler.Mul(
                IsPuzzleInputRange,
                Compiler.Sub(One, HigherThanPuzzleInput));

            int32 VisibleOnlyMask = One;
            const bool bNeedsSceneDepth =
                Expression.OcclusionMode != EParadoxOutlineOcclusionMode::ThroughWalls
                || Expression.PuzzleWireOcclusionMode != EParadoxOutlineOcclusionMode::ThroughWalls;

            if (bNeedsSceneDepth)
            {
                const int32 SceneDepthSample = Compiler.ComponentMask(
                    Compiler.SceneTextureLookup(
                        SampleUv,
                        PPI_SceneDepth,
                        false,
                        true,
                        false),
                    true,
                    false,
                    false,
                    false);

                const int32 DistanceScaledTolerance = Compiler.Mul(
                    CustomDepthSample,
                    Compiler.Constant(0.0001f));
                const int32 EffectiveOcclusionBias = Compiler.Max(
                    OcclusionBias,
                    DistanceScaledTolerance);

                VisibleOnlyMask = Compiler.Step(
                    CustomDepthSample,
                    Compiler.Add(
                        SceneDepthSample,
                        EffectiveOcclusionBias));
            }

            const auto ResolveOcclusionMode = [
                &Compiler,
                One,
                VisibleOnlyMask](const EParadoxOutlineOcclusionMode Mode)
            {
                switch (Mode)
                {
                case EParadoxOutlineOcclusionMode::ThroughWalls:
                    return One;
                case EParadoxOutlineOcclusionMode::OccludedOnly:
                    return Compiler.Sub(One, VisibleOnlyMask);
                case EParadoxOutlineOcclusionMode::VisibleOnly:
                default:
                    return VisibleOnlyMask;
                }
            };

            const int32 StandardVisibility = ResolveOcclusionMode(
                Expression.OcclusionMode);
            const int32 PuzzleVisibility = ResolveOcclusionMode(
                Expression.PuzzleWireOcclusionMode);

            const int32 SemanticCategory = Compiler.Add(
                IsPuzzleInput,
                Compiler.Add(
                    Compiler.Mul(IsPuzzleOutput, Two),
                    Compiler.Add(
                        Compiler.Mul(IsHover, Three),
                        Compiler.Mul(IsSelection, Four))));

            return FCompiledSample{
                CustomDepthSample,
                SemanticCategory,
                StandardVisibility,
                PuzzleVisibility,
            };
        };

        const FCompiledSample CurrentSample = CompileSample(
            FVector2f::ZeroVector);

        static const FVector2f SampleDirections[] =
        {
            FVector2f(1.0f, 0.0f),
            FVector2f(-1.0f, 0.0f),
            FVector2f(0.0f, 1.0f),
            FVector2f(0.0f, -1.0f),
            FVector2f(0.70710678f, 0.70710678f),
            FVector2f(-0.70710678f, 0.70710678f),
            FVector2f(0.70710678f, -0.70710678f),
            FVector2f(-0.70710678f, -0.70710678f),
        };

        static_assert(UE_ARRAY_COUNT(SampleDirections) == 8);

        TStaticArray<FCompiledSample, 8> NeighborSamples;

        for (int32 SampleIndex = 0;
            SampleIndex < NeighborSamples.Num();
            ++SampleIndex)
        {
            NeighborSamples[SampleIndex] = CompileSample(
                SampleDirections[SampleIndex]);
        }

        int32 HoverMaximumBoundary = Zero;
        int32 SelectionMaximumBoundary = Zero;
        int32 PuzzleInputMaximumBoundary = Zero;
        int32 PuzzleOutputMaximumBoundary = Zero;
        int32 HoverBoundarySum = Zero;
        int32 SelectionBoundarySum = Zero;
        int32 PuzzleInputBoundarySum = Zero;
        int32 PuzzleOutputBoundarySum = Zero;

        const int32 CurrentIsHighlighted = Compiler.Step(
            Half,
            CurrentSample.SemanticCategory);

        for (const FCompiledSample& NeighborSample : NeighborSamples)
        {
            const int32 CategoryDifference = Compiler.Step(
                Half,
                Compiler.Abs(Compiler.Sub(
                    CurrentSample.SemanticCategory,
                    NeighborSample.SemanticCategory)));
            const int32 MaximumCategory = Compiler.Max(
                CurrentSample.SemanticCategory,
                NeighborSample.SemanticCategory);
            const int32 HasHighlightedCategory = Compiler.Step(
                Half,
                MaximumCategory);
            const int32 NeighborIsHighlighted = Compiler.Step(
                Half,
                NeighborSample.SemanticCategory);
            const int32 StandardCategoryVisibility = Compiler.Max(
                Compiler.Mul(
                    CurrentIsHighlighted,
                    CurrentSample.StandardVisibility),
                Compiler.Mul(
                    NeighborIsHighlighted,
                    NeighborSample.StandardVisibility));
            const int32 PuzzleCategoryVisibility = Compiler.Max(
                Compiler.Mul(
                    CurrentIsHighlighted,
                    CurrentSample.PuzzleVisibility),
                Compiler.Mul(
                    NeighborIsHighlighted,
                    NeighborSample.PuzzleVisibility));
            const int32 HasSelection = Compiler.Step(ThreeAndHalf, MaximumCategory);
            const int32 HasHover = Compiler.Mul(
                Compiler.Step(TwoAndHalf, MaximumCategory),
                Compiler.Sub(One, HasSelection));
            const int32 HasPuzzleOutput = Compiler.Mul(
                Compiler.Step(OneAndHalf, MaximumCategory),
                Compiler.Sub(One, Compiler.Saturate(Compiler.Add(HasSelection, HasHover))));
            const int32 HasPuzzleInput = Compiler.Mul(
                Compiler.Step(Half, MaximumCategory),
                Compiler.Sub(One, Compiler.Saturate(Compiler.Add(
                    Compiler.Add(HasSelection, HasHover), HasPuzzleOutput))));
            const int32 HasPuzzleCategory = Compiler.Saturate(
                Compiler.Add(HasPuzzleInput, HasPuzzleOutput));
            const int32 CategoryVisibility = Compiler.Lerp(
                StandardCategoryVisibility,
                PuzzleCategoryVisibility,
                HasPuzzleCategory);
            const int32 CategoryBoundary = Compiler.Mul(
                Compiler.Mul(
                    CategoryDifference,
                    HasHighlightedCategory),
                CategoryVisibility);
            const int32 Boundary = Compiler.Mul(
                CategoryBoundary,
                StencilBoundaryStrength);

            const int32 HoverBoundary = Compiler.Mul(
                Boundary,
                HasHover);
            const int32 SelectionBoundary = Compiler.Mul(
                Boundary,
                HasSelection);
            const int32 PuzzleInputBoundary = Compiler.Mul(Boundary, HasPuzzleInput);
            const int32 PuzzleOutputBoundary = Compiler.Mul(Boundary, HasPuzzleOutput);

            HoverMaximumBoundary = Compiler.Max(
                HoverMaximumBoundary,
                HoverBoundary);
            SelectionMaximumBoundary = Compiler.Max(
                SelectionMaximumBoundary,
                SelectionBoundary);
            HoverBoundarySum = Compiler.Add(
                HoverBoundarySum,
                HoverBoundary);
            SelectionBoundarySum = Compiler.Add(
                SelectionBoundarySum,
                SelectionBoundary);
            PuzzleInputMaximumBoundary = Compiler.Max(PuzzleInputMaximumBoundary, PuzzleInputBoundary);
            PuzzleOutputMaximumBoundary = Compiler.Max(PuzzleOutputMaximumBoundary, PuzzleOutputBoundary);
            PuzzleInputBoundarySum = Compiler.Add(PuzzleInputBoundarySum, PuzzleInputBoundary);
            PuzzleOutputBoundarySum = Compiler.Add(PuzzleOutputBoundarySum, PuzzleOutputBoundary);
        }

        static const int32 OpposingSamplePairs[][2] =
        {
            {0, 1},
            {2, 3},
            {4, 7},
            {5, 6},
        };

        if (Expression.bEnableInternalDepthEdges)
        {
            const int32 EffectiveDepthThreshold = Compiler.Add(
                Compiler.Add(
                    DepthThreshold,
                    Compiler.Mul(
                        CurrentSample.CustomDepth,
                        Compiler.Constant(0.0001f))),
                Compiler.Constant(0.0001f));
            const int32 SafeCurrentDepth = Compiler.Max(
                CurrentSample.CustomDepth,
                One);
            const int32 CurrentInverseDepth = Compiler.Div(
                One,
                SafeCurrentDepth);
            const int32 CurrentDepthSquared = Compiler.Mul(
                SafeCurrentDepth,
                SafeCurrentDepth);

            for (const int32 (&Pair)[2] : OpposingSamplePairs)
            {
                const FCompiledSample& FirstSample =
                    NeighborSamples[Pair[0]];
                const FCompiledSample& SecondSample =
                    NeighborSamples[Pair[1]];

                const int32 FirstIsSameCategory = Compiler.Sub(
                    One,
                    Compiler.Step(
                        Half,
                        Compiler.Abs(Compiler.Sub(
                            CurrentSample.SemanticCategory,
                            FirstSample.SemanticCategory))));
                const int32 SecondIsSameCategory = Compiler.Sub(
                    One,
                    Compiler.Step(
                        Half,
                        Compiler.Abs(Compiler.Sub(
                            CurrentSample.SemanticCategory,
                            SecondSample.SemanticCategory))));
                const int32 AllSamplesShareHighlightedCategory = Compiler.Mul(
                    CurrentIsHighlighted,
                    Compiler.Mul(
                        FirstIsSameCategory,
                        SecondIsSameCategory));
                const int32 StandardDepthVisibility = Compiler.Min(
                    CurrentSample.StandardVisibility,
                    Compiler.Min(
                        FirstSample.StandardVisibility,
                        SecondSample.StandardVisibility));
                const int32 PuzzleDepthVisibility = Compiler.Min(
                    CurrentSample.PuzzleVisibility,
                    Compiler.Min(
                        FirstSample.PuzzleVisibility,
                        SecondSample.PuzzleVisibility));
                const int32 CurrentIsHoverOrSelection = Compiler.Step(
                    TwoAndHalf,
                    CurrentSample.SemanticCategory);
                const int32 CurrentIsPuzzleCategory = Compiler.Mul(
                    CurrentIsHighlighted,
                    Compiler.Sub(One, CurrentIsHoverOrSelection));
                const int32 DepthVisibility = Compiler.Lerp(
                    StandardDepthVisibility,
                    PuzzleDepthVisibility,
                    CurrentIsPuzzleCategory);
                const int32 FirstInverseDepth = Compiler.Div(
                    One,
                    Compiler.Max(FirstSample.CustomDepth, One));
                const int32 SecondInverseDepth = Compiler.Div(
                    One,
                    Compiler.Max(SecondSample.CustomDepth, One));
                const int32 ReciprocalDepthCurvature = Compiler.Abs(
                    Compiler.Sub(
                        Compiler.Add(
                            FirstInverseDepth,
                            SecondInverseDepth),
                        Compiler.Mul(
                            CurrentInverseDepth,
                            Two)));
                const int32 SymmetricDepthDifference = Compiler.Mul(
                    ReciprocalDepthCurvature,
                    CurrentDepthSquared);
                const int32 DepthBoundary = Compiler.Mul(
                    Compiler.Mul(
                        AllSamplesShareHighlightedCategory,
                        DepthVisibility),
                    Compiler.Step(
                        EffectiveDepthThreshold,
                        SymmetricDepthDifference));
                const int32 IsSelection = Compiler.Step(ThreeAndHalf, CurrentSample.SemanticCategory);
                const int32 IsHover = Compiler.Mul(
                    Compiler.Step(TwoAndHalf, CurrentSample.SemanticCategory),
                    Compiler.Sub(One, IsSelection));
                const int32 IsPuzzleOutput = Compiler.Mul(
                    Compiler.Step(OneAndHalf, CurrentSample.SemanticCategory),
                    Compiler.Sub(One, Compiler.Saturate(Compiler.Add(IsSelection, IsHover))));
                const int32 IsPuzzleInput = Compiler.Mul(
                    CurrentIsHighlighted,
                    Compiler.Sub(One, Compiler.Saturate(Compiler.Add(
                        Compiler.Add(IsSelection, IsHover), IsPuzzleOutput))));
                const int32 HoverDepthBoundary = Compiler.Mul(
                    DepthBoundary,
                    IsHover);
                const int32 SelectionDepthBoundary = Compiler.Mul(
                    DepthBoundary,
                    IsSelection);
                const int32 PuzzleInputDepthBoundary = Compiler.Mul(DepthBoundary, IsPuzzleInput);
                const int32 PuzzleOutputDepthBoundary = Compiler.Mul(DepthBoundary, IsPuzzleOutput);

                HoverMaximumBoundary = Compiler.Max(
                    HoverMaximumBoundary,
                    HoverDepthBoundary);
                SelectionMaximumBoundary = Compiler.Max(
                    SelectionMaximumBoundary,
                    SelectionDepthBoundary);
                HoverBoundarySum = Compiler.Add(
                    HoverBoundarySum,
                    Compiler.Mul(HoverDepthBoundary, Two));
                SelectionBoundarySum = Compiler.Add(
                    SelectionBoundarySum,
                    Compiler.Mul(SelectionDepthBoundary, Two));
                PuzzleInputMaximumBoundary = Compiler.Max(PuzzleInputMaximumBoundary, PuzzleInputDepthBoundary);
                PuzzleOutputMaximumBoundary = Compiler.Max(PuzzleOutputMaximumBoundary, PuzzleOutputDepthBoundary);
                PuzzleInputBoundarySum = Compiler.Add(PuzzleInputBoundarySum, Compiler.Mul(PuzzleInputDepthBoundary, Two));
                PuzzleOutputBoundarySum = Compiler.Add(PuzzleOutputBoundarySum, Compiler.Mul(PuzzleOutputDepthBoundary, Two));
            }
        }

        const int32 OneEighth = Compiler.Constant(0.125f);
        const int32 HoverCoverage = Compiler.Lerp(
            HoverMaximumBoundary,
            Compiler.Mul(HoverBoundarySum, OneEighth),
            Softness);
        const int32 SelectionCoverage = Compiler.Lerp(
            SelectionMaximumBoundary,
            Compiler.Mul(SelectionBoundarySum, OneEighth),
            Softness);
        const int32 PuzzleInputCoverage = Compiler.Lerp(
            PuzzleInputMaximumBoundary,
            Compiler.Mul(PuzzleInputBoundarySum, OneEighth),
            Softness);
        const int32 PuzzleOutputCoverage = Compiler.Lerp(
            PuzzleOutputMaximumBoundary,
            Compiler.Mul(PuzzleOutputBoundarySum, OneEighth),
            Softness);

        const int32 SelectionMask = Compiler.Saturate(
            Compiler.Mul(
                SelectionCoverage,
                SelectionIntensity));
        const int32 HoverMask = Compiler.Mul(
            Compiler.Saturate(Compiler.Mul(
                HoverCoverage,
                HoverIntensity)),
            Compiler.Sub(One, SelectionMask));
        const int32 HigherThanPuzzleOutputMask = Compiler.Saturate(Compiler.Max(SelectionMask, HoverMask));
        const int32 PuzzleOutputMask = Compiler.Mul(
            Compiler.Saturate(Compiler.Mul(PuzzleOutputCoverage, PuzzleOutputIntensity)),
            Compiler.Sub(One, HigherThanPuzzleOutputMask));
        const int32 HigherThanPuzzleInputMask = Compiler.Saturate(
            Compiler.Max(HigherThanPuzzleOutputMask, PuzzleOutputMask));
        const int32 PuzzleInputMask = Compiler.Mul(
            Compiler.Saturate(Compiler.Mul(PuzzleInputCoverage, PuzzleInputIntensity)),
            Compiler.Sub(One, HigherThanPuzzleInputMask));

        return FCompiledOutline{
            HoverMask,
            SelectionMask,
            PuzzleInputMask,
            PuzzleOutputMask,
            Compiler.Saturate(Compiler.Max(
                Compiler.Max(HoverMask, SelectionMask),
                Compiler.Max(PuzzleInputMask, PuzzleOutputMask))),
        };
    }
#endif
}

UMaterialExpressionParadoxOutline::UMaterialExpressionParadoxOutline(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    Outputs.Reset(OutputCount);
    Outputs.Add(FExpressionOutput(TEXT("HoverMask")));
    Outputs.Add(FExpressionOutput(TEXT("SelectionMask")));
    Outputs.Add(FExpressionOutput(TEXT("CombinedMask")));
    Outputs.Add(FExpressionOutput(TEXT("OutlineColor")));
    Outputs.Add(FExpressionOutput(TEXT("PuzzleInputMask")));
    Outputs.Add(FExpressionOutput(TEXT("PuzzleOutputMask")));

    bShowOutputNameOnPin = true;
    bShaderInputData = true;
    MenuCategories.Reset();
    MenuCategories.Add(LOCTEXT(
        "ParadoxPostProcessCategory",
        "Paradox | Post Process"));
}

bool UMaterialExpressionParadoxOutline::IsAllowedIn(
    const UObject* MaterialOrFunction) const
{
    const UMaterial* TargetMaterial = Cast<UMaterial>(MaterialOrFunction);
    return TargetMaterial && TargetMaterial->MaterialDomain == MD_PostProcess;
}

#if WITH_EDITOR
int32 UMaterialExpressionParadoxOutline::Compile(
    FMaterialCompiler* Compiler,
    const int32 OutputIndex)
{
    if (!Compiler)
    {
        return INDEX_NONE;
    }

    if (OutputIndex < 0 || OutputIndex >= OutputCount)
    {
        return Compiler->Error(TEXT("Invalid Paradox Outline output."));
    }

    const FCompiledOutline Outline = CompileOutlineMasks(*this, *Compiler);

    if (Outline.HoverMask == INDEX_NONE
        || Outline.SelectionMask == INDEX_NONE
        || Outline.PuzzleInputMask == INDEX_NONE
        || Outline.PuzzleOutputMask == INDEX_NONE
        || Outline.CombinedMask == INDEX_NONE)
    {
        return INDEX_NONE;
    }

    switch (static_cast<EParadoxOutlineOutput>(OutputIndex))
    {
    case EParadoxOutlineOutput::HoverMask:
        return Outline.HoverMask;

    case EParadoxOutlineOutput::SelectionMask:
        return Outline.SelectionMask;

    case EParadoxOutlineOutput::CombinedMask:
        return Outline.CombinedMask;

    case EParadoxOutlineOutput::OutlineColor:
    {
        if (UsesReservedEditorSelectionColorParameter(HoverColor)
            || UsesReservedEditorSelectionColorParameter(SelectionColor)
            || UsesReservedEditorSelectionColorParameter(PuzzleInputColor)
            || UsesReservedEditorSelectionColorParameter(PuzzleOutputColor))
        {
            return Compiler->Error(TEXT(
                "Paradox Outline: a connected Vector Parameter cannot be named 'SelectionColor'. Unreal reserves that name for its editor selection overlay; rename the Material parameter, for example to 'ParadoxSelectionOutlineColor'."));
        }

        const int32 CompiledHoverColor = CompileColorInput(
            HoverColor,
            *Compiler,
            DefaultHoverColor);
        const int32 CompiledSelectionColor = CompileColorInput(
            SelectionColor,
            *Compiler,
            DefaultSelectionColor);
        const int32 CompiledPuzzleInputColor = CompileColorInput(
            PuzzleInputColor,
            *Compiler,
            DefaultPuzzleInputColor);
        const int32 CompiledPuzzleOutputColor = CompileColorInput(
            PuzzleOutputColor,
            *Compiler,
            DefaultPuzzleOutputColor);

        if (CompiledHoverColor == INDEX_NONE
            || CompiledSelectionColor == INDEX_NONE
            || CompiledPuzzleInputColor == INDEX_NONE
            || CompiledPuzzleOutputColor == INDEX_NONE)
        {
            return INDEX_NONE;
        }

        const int32 Black = Compiler->Constant3(0.0f, 0.0f, 0.0f);
        const int32 HoverContribution = Compiler->Lerp(
            Black,
            CompiledHoverColor,
            Outline.HoverMask);
        const int32 SelectionContribution = Compiler->Lerp(
            Black,
            CompiledSelectionColor,
            Outline.SelectionMask);
        const int32 PuzzleInputContribution = Compiler->Lerp(
            Black,
            CompiledPuzzleInputColor,
            Outline.PuzzleInputMask);
        const int32 PuzzleOutputContribution = Compiler->Lerp(
            Black,
            CompiledPuzzleOutputColor,
            Outline.PuzzleOutputMask);

        return Compiler->Add(
            Compiler->Add(HoverContribution, SelectionContribution),
            Compiler->Add(PuzzleInputContribution, PuzzleOutputContribution));
    }

    case EParadoxOutlineOutput::PuzzleInputMask:
        return Outline.PuzzleInputMask;

    case EParadoxOutlineOutput::PuzzleOutputMask:
        return Outline.PuzzleOutputMask;

    default:
        return Compiler->Error(TEXT("Invalid Paradox Outline output."));
    }
}

EMaterialValueType UMaterialExpressionParadoxOutline::GetInputValueType(
    const int32 InputIndex)
{
    if (InputIndex < 0 || InputIndex >= InputCount)
    {
        return MCT_Unknown;
    }

    const EParadoxOutlineInput Input =
        static_cast<EParadoxOutlineInput>(InputIndex);

    return Input == EParadoxOutlineInput::HoverColor
        || Input == EParadoxOutlineInput::SelectionColor
        || Input == EParadoxOutlineInput::PuzzleInputColor
        || Input == EParadoxOutlineInput::PuzzleOutputColor
        ? MCT_Float3
        : MCT_Float1;
}

EMaterialValueType UMaterialExpressionParadoxOutline::GetOutputValueType(
    const int32 OutputIndex)
{
    return OutputIndex == static_cast<int32>(
        EParadoxOutlineOutput::OutlineColor)
        ? MCT_Float3
        : MCT_Float1;
}

void UMaterialExpressionParadoxOutline::GetCaption(
    TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(TEXT("Paradox Outline"));
}

FText UMaterialExpressionParadoxOutline::GetCreationDescription() const
{
    return LOCTEXT(
        "CreationDescription",
        "Generate Hover, Selection, Puzzle Input, and Puzzle Output silhouette masks from Custom Depth and semantic Custom Stencil ranges in a Post Process material.");
}

FText UMaterialExpressionParadoxOutline::GetCreationName() const
{
    return LOCTEXT("CreationName", "Paradox Outline");
}

FText UMaterialExpressionParadoxOutline::GetKeywords() const
{
    return LOCTEXT(
        "Keywords",
        "outline hover selection puzzle input output circuit stencil custom depth post process paradox");
}

void UMaterialExpressionParadoxOutline::GetConnectorToolTip(
    const int32 InputIndex,
    const int32 OutputIndex,
    TArray<FString>& OutToolTip)
{
    static const TCHAR* InputToolTips[InputCount] =
    {
        TEXT("Outline radius in screen-space pixels. Clamped to 0-8."),
        TEXT("Blends a hard maximum response toward inexpensive 8-sample coverage feathering. Clamped to 0-1."),
        TEXT("When Enable Internal Depth Edges is active, sets the minimum reciprocal-Custom-Depth curvature that creates a same-category edge."),
        TEXT("Strength of transitions between unrelated, Hover, and Selection stencil categories."),
        TEXT("Multiplier applied only to HoverMask."),
        TEXT("Multiplier applied only to SelectionMask."),
        TEXT("RGB color used by OutlineColor for Hover pixels."),
        TEXT("RGB color used by OutlineColor for Selection pixels. A connected Vector Parameter must not be named SelectionColor because Unreal reserves that name for its editor selection overlay."),
        TEXT("Non-negative Scene Depth tolerance, in centimeters, used by Visible Only occlusion."),
        TEXT("Inclusive lower bound of the Hover Custom Stencil range."),
        TEXT("Inclusive upper bound of the Hover Custom Stencil range."),
        TEXT("Inclusive lower bound of the Selection Custom Stencil range."),
        TEXT("Inclusive upper bound of the Selection Custom Stencil range."),
        TEXT("Multiplier applied only to PuzzleInputMask."),
        TEXT("Multiplier applied only to PuzzleOutputMask."),
        TEXT("RGB color used by OutlineColor for Puzzle Input pixels."),
        TEXT("RGB color used by OutlineColor for Puzzle Output pixels."),
        TEXT("Inclusive lower bound of the Puzzle Input Custom Stencil range."),
        TEXT("Inclusive upper bound of the Puzzle Input Custom Stencil range."),
        TEXT("Inclusive lower bound of the Puzzle Output Custom Stencil range."),
        TEXT("Inclusive upper bound of the Puzzle Output Custom Stencil range."),
    };

    static const TCHAR* OutputToolTips[OutputCount] =
    {
        TEXT("Hover outline mask after Selection priority has been applied."),
        TEXT("Selection outline mask. Selection has visual priority over Hover."),
        TEXT("Maximum of Hover, Selection, Puzzle Input, and Puzzle Output masks."),
        TEXT("Sum of the four category colors multiplied by their priority-resolved masks."),
        TEXT("Puzzle Input outline mask after higher-priority categories have been applied."),
        TEXT("Puzzle Output outline mask after Hover and Selection priority has been applied."),
    };

    if (InputIndex >= 0 && InputIndex < InputCount)
    {
        OutToolTip.Add(InputToolTips[InputIndex]);
        return;
    }

    if (OutputIndex >= 0 && OutputIndex < OutputCount)
    {
        OutToolTip.Add(OutputToolTips[OutputIndex]);
        return;
    }

    Super::GetConnectorToolTip(
        InputIndex,
        OutputIndex,
        OutToolTip);
}
#endif

#undef LOCTEXT_NAMESPACE
