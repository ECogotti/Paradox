#include "StaticMeshVoxelBatch.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ParadoxEditorToolsModule.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "StaticMeshVoxelBatch"

namespace ParadoxEditorTools
{
    namespace
    {
        struct FPivotOption
        {
            EStaticMeshVoxelPivotPosition Position;
            FText Label;
        };

        TSharedRef<SWidget> MakeNumericRow(
            const FText& InLabel,
            const TSharedRef<FVector>& InScaleMultiplier,
            const int32 InAxisIndex)
        {
            return SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(InLabel)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(145.0f)
                    [
                        SNew(SSpinBox<double>)
                        .MinValue(0.001)
                        .MaxValue(100000.0)
                        .MinSliderValue(0.001)
                        .MaxSliderValue(1000.0)
                        .Delta(1.0)
                        .Value_Lambda([InScaleMultiplier, InAxisIndex]()
                        {
                            return (*InScaleMultiplier)[InAxisIndex];
                        })
                        .OnValueChanged_Lambda([InScaleMultiplier, InAxisIndex](const double InValue)
                        {
                            (*InScaleMultiplier)[InAxisIndex] = InValue;
                        })
                    ]
                ];
        }

        TSharedRef<SWidget> MakePivotRow(
            const FText& InLabel,
            TArray<TSharedPtr<FPivotOption>>& InPivotOptions,
            const TSharedRef<TSharedPtr<FPivotOption>>& InSelectedPivot)
        {
            return SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(InLabel)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(145.0f)
                    [
                        SNew(SComboBox<TSharedPtr<FPivotOption>>)
                        .OptionsSource(&InPivotOptions)
                        .InitiallySelectedItem(*InSelectedPivot)
                        .OnGenerateWidget_Lambda([](const TSharedPtr<FPivotOption>& InOption)
                        {
                            return SNew(STextBlock)
                                .Text(InOption.IsValid() ? InOption->Label : FText::GetEmpty());
                        })
                        .OnSelectionChanged_Lambda(
                            [InSelectedPivot](
                                const TSharedPtr<FPivotOption>& InOption,
                                ESelectInfo::Type)
                        {
                            if (InOption.IsValid())
                            {
                                *InSelectedPivot = InOption;
                            }
                        })
                        [
                            SNew(STextBlock)
                            .Text_Lambda([InSelectedPivot]()
                            {
                                const TSharedPtr<FPivotOption>& SelectedOption = *InSelectedPivot;
                                return SelectedOption.IsValid()
                                    ? SelectedOption->Label
                                    : FText::GetEmpty();
                            })
                        ]
                    ]
                ];
        }

        double ResolvePivotCoordinate(
            const double InMinimum,
            const double InCenter,
            const double InMaximum,
            const EStaticMeshVoxelPivotPosition InPosition)
        {
            switch (InPosition)
            {
            case EStaticMeshVoxelPivotPosition::Below:
                return InMinimum;

            case EStaticMeshVoxelPivotPosition::Top:
                return InMaximum;

            case EStaticMeshVoxelPivotPosition::Center:
            default:
                return InCenter;
            }
        }

        FVector CalculatePivotPoint(
            const FBox& InBounds,
            const FStaticMeshVoxelBatchOptions& InOptions)
        {
            const FVector Center = InBounds.GetCenter();

            return FVector(
                ResolvePivotCoordinate(
                    InBounds.Min.X,
                    Center.X,
                    InBounds.Max.X,
                    InOptions.PivotX),
                ResolvePivotCoordinate(
                    InBounds.Min.Y,
                    Center.Y,
                    InBounds.Max.Y,
                    InOptions.PivotY),
                ResolvePivotCoordinate(
                    InBounds.Min.Z,
                    Center.Z,
                    InBounds.Max.Z,
                    InOptions.PivotZ));
        }

        FVector TransformPoint(
            const FVector& InPoint,
            const FVector& InPivotPoint,
            const FVector& InScaleMultiplier)
        {
            return (InPoint - InPivotPoint) * InScaleMultiplier;
        }

        void TransformMeshDescription(
            FMeshDescription& InMeshDescription,
            const FVector& InPivotPoint,
            const FVector& InScaleMultiplier)
        {
            FStaticMeshAttributes Attributes(InMeshDescription);
            TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

            const FVector3f Pivot(
                static_cast<float>(InPivotPoint.X),
                static_cast<float>(InPivotPoint.Y),
                static_cast<float>(InPivotPoint.Z));

            const FVector3f ScaleMultiplier(
                static_cast<float>(InScaleMultiplier.X),
                static_cast<float>(InScaleMultiplier.Y),
                static_cast<float>(InScaleMultiplier.Z));

            const FVector3f InverseScale(
                1.0f / ScaleMultiplier.X,
                1.0f / ScaleMultiplier.Y,
                1.0f / ScaleMultiplier.Z);

            for (const FVertexID VertexID : InMeshDescription.Vertices().GetElementIDs())
            {
                const FVector3f RelativePosition = VertexPositions[VertexID] - Pivot;

                VertexPositions[VertexID] = FVector3f(
                    RelativePosition.X * ScaleMultiplier.X,
                    RelativePosition.Y * ScaleMultiplier.Y,
                    RelativePosition.Z * ScaleMultiplier.Z);
            }

            TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals =
                Attributes.GetVertexInstanceNormals();

            TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents =
                Attributes.GetVertexInstanceTangents();

            for (const FVertexInstanceID VertexInstanceID :
                InMeshDescription.VertexInstances().GetElementIDs())
            {
                FVector3f Normal = VertexInstanceNormals[VertexInstanceID];
                Normal = FVector3f(
                    Normal.X * InverseScale.X,
                    Normal.Y * InverseScale.Y,
                    Normal.Z * InverseScale.Z).GetSafeNormal();

                FVector3f Tangent = VertexInstanceTangents[VertexInstanceID];
                Tangent = FVector3f(
                    Tangent.X * ScaleMultiplier.X,
                    Tangent.Y * ScaleMultiplier.Y,
                    Tangent.Z * ScaleMultiplier.Z);

                Tangent = (
                    Tangent
                    - Normal * FVector3f::DotProduct(Tangent, Normal)
                ).GetSafeNormal();

                VertexInstanceNormals[VertexInstanceID] = Normal;
                VertexInstanceTangents[VertexInstanceID] = Tangent;
            }
        }

        void TransformSockets(
            UStaticMesh& InStaticMesh,
            const FVector& InPivotPoint,
            const FVector& InScaleMultiplier)
        {
            for (const TObjectPtr<UStaticMeshSocket>& SocketPtr : InStaticMesh.Sockets)
            {
                UStaticMeshSocket* Socket = SocketPtr.Get();
                if (!Socket)
                {
                    continue;
                }

                Socket->Modify();
                Socket->RelativeLocation = TransformPoint(
                    Socket->RelativeLocation,
                    InPivotPoint,
                    InScaleMultiplier);
            }
        }

        template <typename ShapeType>
        void TranslateShapeElements(
            TArray<ShapeType>& InElements,
            const FVector& InTranslation)
        {
            for (ShapeType& Element : InElements)
            {
                FTransform ElementTransform = Element.GetTransform();
                ElementTransform.AddToTranslation(InTranslation);
                Element.SetTransform(ElementTransform);
            }
        }

        void TransformSimpleCollision(
            UStaticMesh& InStaticMesh,
            const FVector& InPivotPoint,
            const FVector& InScaleMultiplier)
        {
            UBodySetup* BodySetup = InStaticMesh.GetBodySetup();
            if (!BodySetup)
            {
                return;
            }

            BodySetup->Modify();

            const FVector NewBuildScale =
                BodySetup->BuildScale3D * InScaleMultiplier;

            BodySetup->RescaleSimpleCollision(NewBuildScale);

            const FVector PivotTranslation =
                -(InPivotPoint * InScaleMultiplier);

            FKAggregateGeom& AggregateGeometry = BodySetup->AggGeom;

            TranslateShapeElements(
                AggregateGeometry.SphereElems,
                PivotTranslation);

            TranslateShapeElements(
                AggregateGeometry.BoxElems,
                PivotTranslation);

            TranslateShapeElements(
                AggregateGeometry.SphylElems,
                PivotTranslation);

            TranslateShapeElements(
                AggregateGeometry.TaperedCapsuleElems,
                PivotTranslation);

            TranslateShapeElements(
                AggregateGeometry.ConvexElems,
                PivotTranslation);

            if (!AggregateGeometry.LevelSetElems.IsEmpty()
                || !AggregateGeometry.MLLevelSetElems.IsEmpty()
                || !AggregateGeometry.SkinnedLevelSetElems.IsEmpty()
                || !AggregateGeometry.SkinnedTriangleMeshElems.IsEmpty())
            {
                PARADOXEDITORTOOLS_LOG_WARNING(
                    "Static Mesh '%s' contains advanced level-set or skinned collision. Those elements are not transformed by this tool.",
                    *InStaticMesh.GetPathName());
            }

            BodySetup->InvalidatePhysicsData();
        }

        bool GetSourceBounds(
            UStaticMesh& InStaticMesh,
            FBox& OutBounds,
            FString& OutFailureReason)
        {
            FMeshDescription* MeshDescription =
                InStaticMesh.GetMeshDescription(0);

            if (!MeshDescription)
            {
                OutFailureReason =
                    TEXT("LOD 0 does not expose a Mesh Description.");
                return false;
            }

            if (MeshDescription->Vertices().Num() == 0)
            {
                OutFailureReason = TEXT("LOD 0 has no vertices.");
                return false;
            }

            FStaticMeshAttributes Attributes(*MeshDescription);
            const TVertexAttributesRef<FVector3f> VertexPositions =
                Attributes.GetVertexPositions();

            FBox Bounds(EForceInit::ForceInit);

            for (const FVertexID VertexID :
                MeshDescription->Vertices().GetElementIDs())
            {
                const FVector3f Position = VertexPositions[VertexID];
                Bounds += FVector(Position.X, Position.Y, Position.Z);
            }

            OutBounds = Bounds;
            return true;
        }
    }

    void FStaticMeshVoxelBatch::OpenForAssets(
        const TArray<FAssetData>& InSelectedAssets)
    {
        FStaticMeshVoxelBatchOptions Options;
        if (!ShowOptionsDialog(false, Options))
        {
            return;
        }

        TArray<UStaticMesh*> Meshes;
        CollectMeshesFromAssetData(InSelectedAssets, Meshes);
        Execute(Meshes, Options);
    }

    void FStaticMeshVoxelBatch::OpenForFolders(
        const TArray<FString>& InSelectedPackagePaths)
    {
        FStaticMeshVoxelBatchOptions Options;
        if (!ShowOptionsDialog(true, Options))
        {
            return;
        }

        TArray<UStaticMesh*> Meshes;
        CollectMeshesFromFolders(
            InSelectedPackagePaths,
            Options.bIncludeSubfolders,
            Meshes);

        Execute(Meshes, Options);
    }

    bool FStaticMeshVoxelBatch::ShowOptionsDialog(
        const bool bFolderMode,
        FStaticMeshVoxelBatchOptions& OutOptions)
    {
        TSharedRef<FVector> ScaleMultiplier =
            MakeShared<FVector>(1.0, 1.0, 1.0);

        TSharedRef<bool> bIncludeSubfolders =
            MakeShared<bool>(true);

        TSharedRef<bool> bAccepted =
            MakeShared<bool>(false);

        TArray<TSharedPtr<FPivotOption>> PivotOptions;
        PivotOptions.Reserve(3);

        PivotOptions.Add(MakeShared<FPivotOption>(FPivotOption{
            EStaticMeshVoxelPivotPosition::Below,
            LOCTEXT("Pivot_Below", "Below")
        }));

        PivotOptions.Add(MakeShared<FPivotOption>(FPivotOption{
            EStaticMeshVoxelPivotPosition::Center,
            LOCTEXT("Pivot_Center", "Center")
        }));

        PivotOptions.Add(MakeShared<FPivotOption>(FPivotOption{
            EStaticMeshVoxelPivotPosition::Top,
            LOCTEXT("Pivot_Top", "Top")
        }));

        const TSharedPtr<FPivotOption> CenterOption = PivotOptions[1];

        TSharedRef<TSharedPtr<FPivotOption>> SelectedPivotX =
            MakeShared<TSharedPtr<FPivotOption>>(CenterOption);

        TSharedRef<TSharedPtr<FPivotOption>> SelectedPivotY =
            MakeShared<TSharedPtr<FPivotOption>>(CenterOption);

        TSharedRef<TSharedPtr<FPivotOption>> SelectedPivotZ =
            MakeShared<TSharedPtr<FPivotOption>>(CenterOption);

        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT(
                "OptionsWindowTitle",
                "Static Mesh Voxel Batch"))
            .ClientSize(FVector2D(
                540.0f,
                bFolderMode ? 545.0f : 500.0f))
            .SupportsMaximize(false)
            .SupportsMinimize(false)
            .SizingRule(ESizingRule::UserSized);

        const TWeakPtr<SWindow> WeakWindow = Window;

        TSharedRef<SVerticalBox> SettingsBox = SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Text(LOCTEXT(
                    "IntroText",
                    "Values are direct scale multipliers: 1 means unchanged size, 2 means twice the current size, and 100 means one hundred times the current size. The operation bakes the result into the Static Mesh source geometry and marks each asset dirty without saving it."))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                MakeNumericRow(
                    LOCTEXT("ScaleXLabel", "Scale X multiplier"),
                    ScaleMultiplier,
                    0)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                MakeNumericRow(
                    LOCTEXT("ScaleYLabel", "Scale Y multiplier"),
                    ScaleMultiplier,
                    1)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                MakeNumericRow(
                    LOCTEXT("ScaleZLabel", "Scale Z multiplier"),
                    ScaleMultiplier,
                    2)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 14.0f, 0.0f, 3.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT(
                    "PivotSectionLabel",
                    "Pivot position"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                MakePivotRow(
                    LOCTEXT("PivotXLabel", "Pivot X"),
                    PivotOptions,
                    SelectedPivotX)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                MakePivotRow(
                    LOCTEXT("PivotYLabel", "Pivot Y"),
                    PivotOptions,
                    SelectedPivotY)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                MakePivotRow(
                    LOCTEXT("PivotZLabel", "Pivot Z"),
                    PivotOptions,
                    SelectedPivotZ)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Text(LOCTEXT(
                    "PivotHelpText",
                    "Below uses the minimum bound, Center uses the bounds center, and Top uses the maximum bound independently for each axis."))
            ];

        if (bFolderMode)
        {
            SettingsBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 16.0f, 0.0f, 0.0f)
            [
                SNew(SCheckBox)
                .IsChecked(ECheckBoxState::Checked)
                .OnCheckStateChanged_Lambda(
                    [bIncludeSubfolders](const ECheckBoxState InState)
                {
                    *bIncludeSubfolders =
                        InState == ECheckBoxState::Checked;
                })
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "IncludeSubfoldersLabel",
                        "Include subfolders"))
                ]
            ];
        }

        Window->SetContent(
            SNew(SBorder)
            .Padding(16.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [
                        SettingsBox
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 14.0f, 0.0f, 0.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SSpacer)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("ApplyButton", "Apply"))
                        .OnClicked_Lambda([bAccepted, WeakWindow]()
                        {
                            *bAccepted = true;

                            if (const TSharedPtr<SWindow> PinnedWindow =
                                WeakWindow.Pin())
                            {
                                PinnedWindow->RequestDestroyWindow();
                            }

                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("CancelButton", "Cancel"))
                        .OnClicked_Lambda([WeakWindow]()
                        {
                            if (const TSharedPtr<SWindow> PinnedWindow =
                                WeakWindow.Pin())
                            {
                                PinnedWindow->RequestDestroyWindow();
                            }

                            return FReply::Handled();
                        })
                    ]
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(
            Window,
            FSlateApplication::Get().GetActiveTopLevelWindow(),
            false);

        const TSharedPtr<FPivotOption>& PivotXOption = *SelectedPivotX;
        const TSharedPtr<FPivotOption>& PivotYOption = *SelectedPivotY;
        const TSharedPtr<FPivotOption>& PivotZOption = *SelectedPivotZ;

        if (!*bAccepted
            || !PivotXOption.IsValid()
            || !PivotYOption.IsValid()
            || !PivotZOption.IsValid())
        {
            return false;
        }

        OutOptions.ScaleMultiplier = *ScaleMultiplier;
        OutOptions.PivotX = PivotXOption->Position;
        OutOptions.PivotY = PivotYOption->Position;
        OutOptions.PivotZ = PivotZOption->Position;
        OutOptions.bIncludeSubfolders = *bIncludeSubfolders;

        return true;
    }

    void FStaticMeshVoxelBatch::CollectMeshesFromAssetData(
        const TArray<FAssetData>& InAssetData,
        TArray<UStaticMesh*>& OutMeshes)
    {
        TSet<FSoftObjectPath> SeenAssets;

        for (const FAssetData& AssetData : InAssetData)
        {
            if (AssetData.AssetClassPath
                != UStaticMesh::StaticClass()->GetClassPathName())
            {
                continue;
            }

            const FSoftObjectPath AssetPath =
                AssetData.GetSoftObjectPath();

            if (SeenAssets.Contains(AssetPath))
            {
                continue;
            }

            if (UStaticMesh* StaticMesh =
                Cast<UStaticMesh>(AssetData.GetAsset()))
            {
                SeenAssets.Add(AssetPath);
                OutMeshes.Add(StaticMesh);
            }
            else
            {
                PARADOXEDITORTOOLS_LOG_WARNING(
                    "Failed to load selected Static Mesh asset '%s'.",
                    *AssetPath.ToString());
            }
        }
    }

    void FStaticMeshVoxelBatch::CollectMeshesFromFolders(
        const TArray<FString>& InSelectedPackagePaths,
        const bool bIncludeSubfolders,
        TArray<UStaticMesh*>& OutMeshes)
    {
        FARFilter Filter;
        Filter.ClassPaths.Add(
            UStaticMesh::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        Filter.bRecursivePaths = bIncludeSubfolders;

        for (const FString& PackagePath : InSelectedPackagePaths)
        {
            Filter.PackagePaths.Add(FName(*PackagePath));
        }

        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"));

        TArray<FAssetData> AssetData;
        AssetRegistryModule.Get().GetAssets(Filter, AssetData);

        AssetData.Sort([](
            const FAssetData& Left,
            const FAssetData& Right)
        {
            return Left.GetSoftObjectPath().ToString()
                < Right.GetSoftObjectPath().ToString();
        });

        CollectMeshesFromAssetData(AssetData, OutMeshes);
    }

    void FStaticMeshVoxelBatch::Execute(
        const TArray<UStaticMesh*>& InMeshes,
        const FStaticMeshVoxelBatchOptions& InOptions)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(
            ParadoxEditorTools_StaticMeshVoxelBatch_Execute);

        if (InMeshes.IsEmpty())
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                LOCTEXT(
                    "NoMeshesFound",
                    "No Static Mesh assets were found in the current selection."));
            return;
        }

        const FVector& ScaleMultiplier =
            InOptions.ScaleMultiplier;

        if (ScaleMultiplier.X <= UE_SMALL_NUMBER
            || ScaleMultiplier.Y <= UE_SMALL_NUMBER
            || ScaleMultiplier.Z <= UE_SMALL_NUMBER)
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                LOCTEXT(
                    "InvalidScale",
                    "Scale multipliers must be greater than zero on every axis."));
            return;
        }

        FScopedTransaction Transaction(FText::Format(
            LOCTEXT(
                "TransactionDescription",
                "Static Mesh Voxel Batch: transform {0} assets"),
            FText::AsNumber(InMeshes.Num())));

        FScopedSlowTask SlowTask(
            static_cast<float>(InMeshes.Num()),
            LOCTEXT(
                "SlowTaskText",
                "Correcting voxel Static Mesh scale and baking pivots..."));

        SlowTask.MakeDialog(false);

        int32 SuccessCount = 0;
        TArray<FString> Failures;

        for (UStaticMesh* StaticMesh : InMeshes)
        {
            SlowTask.EnterProgressFrame(
                1.0f,
                StaticMesh
                    ? FText::FromString(StaticMesh->GetPathName())
                    : LOCTEXT(
                        "InvalidMeshProgress",
                        "Invalid Static Mesh"));

            if (!StaticMesh)
            {
                Failures.Add(TEXT("Invalid Static Mesh pointer."));
                continue;
            }

            FString FailureReason;
            if (TransformStaticMesh(
                *StaticMesh,
                InOptions,
                FailureReason))
            {
                ++SuccessCount;
            }
            else
            {
                Failures.Add(FString::Printf(
                    TEXT("%s: %s"),
                    *StaticMesh->GetPathName(),
                    *FailureReason));
            }
        }

        if (SuccessCount == 0)
        {
            Transaction.Cancel();
        }

        FString Summary = FString::Printf(
            TEXT("Completed. Modified: %d. Failed: %d."),
            SuccessCount,
            Failures.Num());

        if (!Failures.IsEmpty())
        {
            Summary += TEXT("\n\nFailures:\n");

            constexpr int32 MaxDisplayedFailures = 12;
            const int32 DisplayedFailureCount =
                FMath::Min(Failures.Num(), MaxDisplayedFailures);

            for (int32 Index = 0;
                Index < DisplayedFailureCount;
                ++Index)
            {
                Summary += FString::Printf(
                    TEXT("- %s\n"),
                    *Failures[Index]);
            }

            if (Failures.Num() > MaxDisplayedFailures)
            {
                Summary += FString::Printf(
                    TEXT("...and %d more. See LogParadoxEditorTools."),
                    Failures.Num() - MaxDisplayedFailures);
            }

            for (const FString& Failure : Failures)
            {
                PARADOXEDITORTOOLS_LOG_ERROR("%s", *Failure);
            }
        }

        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::FromString(Summary));
    }

    bool FStaticMeshVoxelBatch::TransformStaticMesh(
        UStaticMesh& InStaticMesh,
        const FStaticMeshVoxelBatchOptions& InOptions,
        FString& OutFailureReason)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(
            ParadoxEditorTools_StaticMeshVoxelBatch_TransformStaticMesh);

        if (InStaticMesh.GetNumSourceModels() <= 0)
        {
            OutFailureReason = TEXT("The asset has no source models.");
            return false;
        }

        FBox SourceBounds;
        if (!GetSourceBounds(
            InStaticMesh,
            SourceBounds,
            OutFailureReason))
        {
            return false;
        }

        const FVector& ScaleMultiplier =
            InOptions.ScaleMultiplier;

        const FVector PivotPoint =
            CalculatePivotPoint(SourceBounds, InOptions);

        InStaticMesh.Modify();
        InStaticMesh.PreEditChange(nullptr);

        int32 ModifiedLodCount = 0;

        for (int32 LodIndex = 0;
            LodIndex < InStaticMesh.GetNumSourceModels();
            ++LodIndex)
        {
            FMeshDescription* MeshDescription =
                InStaticMesh.GetMeshDescription(LodIndex);

            if (!MeshDescription)
            {
                continue;
            }

            TransformMeshDescription(
                *MeshDescription,
                PivotPoint,
                ScaleMultiplier);

            InStaticMesh.CommitMeshDescription(
                LodIndex,
                UStaticMesh::FCommitMeshDescriptionParams{});

            ++ModifiedLodCount;
        }

        if (ModifiedLodCount == 0)
        {
            InStaticMesh.PostEditChange();
            OutFailureReason =
                TEXT("No editable Mesh Description was found in any source LOD.");
            return false;
        }

        TransformSockets(
            InStaticMesh,
            PivotPoint,
            ScaleMultiplier);

        TransformSimpleCollision(
            InStaticMesh,
            PivotPoint,
            ScaleMultiplier);

        InStaticMesh.Build(false, nullptr);
        InStaticMesh.PostEditChange();
        InStaticMesh.MarkPackageDirty();

        PARADOXEDITORTOOLS_LOG_INFO(
            "Transformed Static Mesh '%s' with scale multipliers (%.3f, %.3f, %.3f).",
            *InStaticMesh.GetPathName(),
            InOptions.ScaleMultiplier.X,
            InOptions.ScaleMultiplier.Y,
            InOptions.ScaleMultiplier.Z);

        return true;
    }
}

#undef LOCTEXT_NAMESPACE