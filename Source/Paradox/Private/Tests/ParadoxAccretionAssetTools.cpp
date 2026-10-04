// Explicit editor asset tools; no runtime or Shipping work.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraEmitter.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraScriptSource.h"
#include "NiagaraEditorUtilities.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Paradox.h"
#include "Environment/ParadoxAccretionExtension.h"
#include "Environment/ParadoxBlackHole.h"
#include "Components/SceneCaptureComponent2D.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataSetAccessor.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	UEdGraphPin& Override(UNiagaraNodeFunctionCall& Node, const TCHAR* Input, const FNiagaraTypeDefinition& Type)
	{
		return FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(Node,
			FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(FName(Input)), &Node),
			Type, FGuid(), FGuid());
	}

	bool SetFloat(UNiagaraNodeFunctionCall& Node, const TCHAR* Input, float Value)
	{
		UEdGraphPin& Pin = Override(Node, Input, FNiagaraTypeDefinition::GetFloatDef());
		FNiagaraVariable Variable(FNiagaraTypeDefinition::GetFloatDef(), FName(Input)); Variable.SetValue<float>(Value);
		const UEdGraphSchema_Niagara* Schema = Cast<UEdGraphSchema_Niagara>(Node.GetSchema());
		FString Default;
		if (!Schema || !Schema->TryGetPinDefaultValueFromNiagaraVariable(Variable, Default)) return false;
		Pin.BreakAllPinLinks(true);
		Schema->TrySetDefaultValue(Pin, Default);
		PARADOX_LOG_INFO(TEXT("Accretion seed input %s = %s"), Input, *Pin.DefaultValue);
		return true;
	}

	bool SetInfiniteLoop(UNiagaraNodeFunctionCall& Node)
	{
		UEdGraphPin* Pin = Node.FindPin(TEXT("Loop Behavior"));
		UEnum* Enum = Pin ? Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()) : nullptr;
		if (!Enum) return false;
		for (int32 I = 0; I < Enum->NumEnums(); ++I)
		{
			const FString Label = Enum->GetDisplayNameTextByIndex(I).ToString();
			PARADOX_LOG_INFO(TEXT("Accretion loop option %s = %s"), *Enum->GetNameStringByIndex(I), *Label);
			if (Label.Equals(TEXT("Infinite"), ESearchCase::IgnoreCase))
			{
				Node.GetSchema()->TrySetDefaultValue(*Pin, Enum->GetNameStringByIndex(I));
				return true;
			}
		}
		return false;
	}

	bool SetDeterministicRandom(UNiagaraNodeFunctionCall& Node)
	{
		UEdGraphPin* Pin = Node.FindPin(TEXT("Randomness Mode"));
		UEnum* Enum = Pin ? Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()) : nullptr;
		if (!Enum || Enum->NumEnums() <= 1) return false;
		// The installed asset's deterministic label has a spelling error; entry 1 was verified in UE 5.8.
		const FString Value = Enum->GetNameStringByIndex(1);
		Node.GetSchema()->TrySetDefaultValue(*Pin, Value);
		return Pin->DefaultValue == Value;
	}

	FAutoConsoleCommand BuildAccretionSystems(
		TEXT("Paradox.Accretion.BuildSystems"), TEXT("Create/update only the three project-owned accretion Niagara seed systems."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const FString ResultPath = FPaths::ProjectSavedDir() / TEXT("CodexBlackHoleValidation/accretion_system_build.txt");
			if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultPath), true)
				|| !FFileHelper::SaveStringToFile(TEXT("Building"), *ResultPath))
			{
				PARADOX_LOG_ERROR(TEXT("Cannot initialize accretion system build report."));
				return;
			}
			UNiagaraEmitter* Template = LoadObject<UNiagaraEmitter>(nullptr,
				TEXT("/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle.SingleLoopingParticle"));
			UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
			UNiagaraScript* Random = LoadObject<UNiagaraScript>(nullptr,
				TEXT("/Niagara/DynamicInputs/UniformRange/V2/RandomRangeFloat.RandomRangeFloat"));
			if (!Template || !Plane || !Random)
			{
				PARADOX_LOG_ERROR(TEXT("Accretion system creation failed: installed seed assets are unavailable."));
				return;
			}
			for (const TCHAR* Kind : { TEXT("Gas"), TEXT("Filaments"), TEXT("Sparks") })
			{
				const FString Name = FString(TEXT("NS_Accretion_")) + Kind;
				const FString PackagePath = TEXT("/Game/Vfx/BlackHole/Accretion/") + Name;
				UPackage* Package = CreatePackage(*PackagePath);
				UNiagaraSystem* System = FindObject<UNiagaraSystem>(Package, *Name);
				if (!System)
				{
					System = NewObject<UNiagaraSystem>(Package, *Name, RF_Public | RF_Standalone);
					UNiagaraSystemFactoryNew::InitializeSystem(System, true);
					FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
					FAssetRegistryModule::AssetCreated(System);
				}
				UNiagaraScriptSource* UpdateSource = Cast<UNiagaraScriptSource>(System->GetSystemUpdateScript()->GetLatestSource());
				const bool bHasEmitterCalls = UpdateSource && UpdateSource->NodeGraph->Nodes.ContainsByPredicate([](UEdGraphNode* Node) { return Node->GetClass()->GetFName() == TEXT("NiagaraNodeEmitter"); });
				if (!bHasEmitterCalls)
				{
					// Editor insertion also connects emitter spawn/update calls in the system graph.
					for (const FNiagaraEmitterHandle& Existing : TArray<FNiagaraEmitterHandle>(System->GetEmitterHandles())) System->RemoveEmitterHandle(Existing);
					FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
				}
				if (System->GetEmitterHandles().Num() != 1)
				{
					PARADOX_LOG_ERROR(TEXT("Accretion system '%s' must have exactly one seed emitter."), *PackagePath);
					return;
				}
				FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[0];
				FVersionedNiagaraEmitter Instance = Handle.GetInstance();
				FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
				UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
				Data->bLocalSpace = true;
				Data->bDeterminism = true;
				Data->RandomSeed = 240104;
				Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
				Data->FixedBounds = FBox(FVector(-1000000), FVector(1000000));
				FNiagaraVariable Count(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.ParticleCount"));
				Count.SetValue<int32>(32);
				System->GetExposedParameters().AddParameter(Count);
				System->GetExposedParameters().SetParameterValue<int32>(32, Count);
				System->GetExposedParameters().RemoveParameter(FNiagaraVariable(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()), TEXT("User.ParticleMaterial")));
				FNiagaraVariable Material(FNiagaraTypeDefinition::GetUMaterialDef(), TEXT("User.ParticleMaterial"));
				System->GetExposedParameters().AddParameter(Material);
				const TArray<TObjectPtr<UEdGraphNode>> GraphNodes = Source->NodeGraph->Nodes;
				for (UEdGraphNode* GraphNode : GraphNodes)
				{
					UNiagaraNodeFunctionCall* Function = Cast<UNiagaraNodeFunctionCall>(GraphNode);
					if (!Function || !Function->FunctionScript) continue;
					const FString ScriptName = Function->FunctionScript->GetName();
					if (ScriptName == TEXT("RandomRangeFloat"))
					{
						if (!SetDeterministicRandom(*Function))
						{
							PARADOX_LOG_ERROR(TEXT("Cannot configure deterministic accretion seeds.")); return;
						}
					}
					if (ScriptName == TEXT("SpawnBurst_Instantaneous"))
					{
						UEdGraphPin& Pin = Override(*Function, TEXT("Module.Spawn Count"), FNiagaraTypeDefinition::GetIntDef());
						if (Pin.LinkedTo.IsEmpty()) FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Count, { Count });
					}
					else if (ScriptName == TEXT("EmitterState"))
					{
						if (!SetInfiniteLoop(*Function) || !SetFloat(*Function, TEXT("Module.Loop Duration"), 1000000.f)) { PARADOX_LOG_ERROR(TEXT("Cannot configure seed loop duration.")); return; }
					}
					else if (ScriptName == TEXT("InitializeParticle"))
					{
						if (!SetFloat(*Function, TEXT("Module.Lifetime"), 1000000.f)) { PARADOX_LOG_ERROR(TEXT("Cannot configure seed lifetime.")); return; }
						UEdGraphPin& Pin = Override(*Function, TEXT("Module.Material Random"), FNiagaraTypeDefinition::GetFloatDef());
						if (Pin.LinkedTo.IsEmpty())
						{
							UNiagaraNodeFunctionCall* RandomNode = nullptr;
							FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(Pin, Random, RandomNode);
							if (!RandomNode || !SetDeterministicRandom(*RandomNode))
							{
								PARADOX_LOG_ERROR(TEXT("Cannot initialize deterministic accretion seeds.")); return;
							}
						}
					}
				}
				for (UNiagaraRendererProperties* Renderer : TArray<UNiagaraRendererProperties*>(Data->GetRenderers()))
					Instance.Emitter->RemoveRenderer(Renderer, Instance.Version);
				UNiagaraMeshRendererProperties* Mesh = NewObject<UNiagaraMeshRendererProperties>(Instance.Emitter);
				Mesh->Meshes.AddDefaulted();
				Mesh->Meshes[0].Mesh = Plane;
				Mesh->bOverrideMaterials = true;
				Mesh->bCastShadows = false;
				Mesh->bEnableFrustumCulling = false;
				Mesh->bEnableCameraDistanceCulling = false;
				Mesh->bIncludeInHitProxy = false;
				Mesh->OverrideMaterials.AddDefaulted();
				Mesh->OverrideMaterials[0].UserParamBinding.Parameter = Material;
				Mesh->OverrideMaterials[0].ExplicitMat = LoadObject<UMaterialInterface>(nullptr,
					*(FString(TEXT("/Game/Vfx/BlackHole/Accretion/MI_Accretion_")) + Kind));
				if (!Mesh->OverrideMaterials[0].ExplicitMat)
				{
					PARADOX_LOG_ERROR(TEXT("Create the accretion materials before '%s'."), *Name);
					return;
				}
				Instance.Emitter->AddRenderer(Mesh, Instance.Version);
				UNiagaraScriptSource* SystemSource = Cast<UNiagaraScriptSource>(System->GetSystemUpdateScript()->GetLatestSource());
				FString GraphReport;
				for (UEdGraphNode* Node : SystemSource->NodeGraph->Nodes)
				{
					GraphReport += Node->GetClass()->GetName() + TEXT(" ") + Node->GetName() + TEXT("\n");
					if (UNiagaraNodeFunctionCall* Function = Cast<UNiagaraNodeFunctionCall>(Node))
						if (Function->FunctionScript)
						{
							GraphReport += TEXT("SCRIPT ") + Function->FunctionScript->GetPathName() + TEXT("\n");
							if (Function->FunctionScript->GetName() == TEXT("SystemState"))
							{
								if (!SetInfiniteLoop(*Function) || !SetFloat(*Function, TEXT("Module.Loop Duration"), 1000000.f))
								{ PARADOX_LOG_ERROR(TEXT("Cannot configure seed system lifetime.")); return; }
							}
						}
				}
				if (!FFileHelper::SaveStringToFile(GraphReport, *(FPaths::ProjectSavedDir() / TEXT("CodexBlackHoleValidation/accretion_system_graph.txt"))))
				{
					PARADOX_LOG_WARNING(TEXT("Could not save accretion system graph diagnostic."));
				}
				Data->GraphSource->ForceGraphToRecompileOnNextCheck();
				System->GetSystemUpdateScript()->GetLatestSource()->ForceGraphToRecompileOnNextCheck();
				System->RequestCompile(true);
				System->WaitForCompilationComplete(true, false);
				if (!System->IsReadyToRun())
				{
					PARADOX_LOG_ERROR(TEXT("Accretion system '%s' did not compile."), *Name);
					return;
				}
				FSavePackageArgs Args;
				Args.TopLevelFlags = RF_Public | RF_Standalone;
				const FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
				if (!UPackage::SavePackage(Package, System, *Filename, Args))
				{
					PARADOX_LOG_ERROR(TEXT("Accretion system '%s' could not be saved."), *Name);
					return;
				}
				PARADOX_LOG_INFO(TEXT("Accretion system compiled and saved: %s"), *PackagePath);
			}
			if (!FFileHelper::SaveStringToFile(TEXT("Success: 3 systems compiled and saved"), *ResultPath))
			{
				PARADOX_LOG_ERROR(TEXT("Cannot save accretion system build result."));
			}
		}));
	FAutoConsoleCommand InspectAccretionScene(TEXT("Paradox.Accretion.InspectScene"), TEXT("Inspect extension materials and capture exclusions without changing the scene."), FConsoleCommandDelegate::CreateLambda([]()
	{
		FString Result;
		for (TObjectIterator<AParadoxAccretionExtension> It; It; ++It)
		{
			AParadoxAccretionExtension* Actor = *It;
			if (Actor->IsTemplate() || !IsValid(Actor->SourceBlackHole)) continue;
			USceneCaptureComponent2D* Capture = Actor->SourceBlackHole->BackgroundCapture;
			const TArray<UNiagaraComponent*> Components = Actor->GetParticleComponents();
			bool bMaterialsValid = true, bExclusionsValid = true;
			int32 TotalParticles = 0;
			const bool bCaptureReady = Actor->SourceBlackHole->CaptureState == EParadoxBlackHoleCaptureState::Ready;
			for (int32 I = 0; I < Components.Num(); ++I)
			{
				TArray<UMaterialInterface*> Used;
				Components[I]->GetUsedMaterials(Used);
				for (UMaterialInterface* Material : Used)
				{
					FLinearColor Start;
					Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("RouteStart")), Start);
					Result += FString::Printf(TEXT("%s material %s outer %s start %s\n"), *Components[I]->GetName(), *Material->GetPathName(), *Material->GetOuter()->GetName(), *Start.ToString());
					bMaterialsValid &= Material->GetOuter() == Actor;
				}
				bMaterialsValid &= !Used.IsEmpty();
				const FNiagaraSystemInstanceControllerPtr Controller = Components[I]->GetSystemInstanceController();
				if (Controller.IsValid() && Controller->IsValid() && Controller->IsSolo())
				{
					FNiagaraSystemInstance* Instance = Controller->GetSoloSystemInstance();
					for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
					{
						const int32 Count = Emitter->GetNumParticles(); TotalParticles += Count;
						FNiagaraDataSet& Data = Emitter->GetParticleData();
						const auto Reader = FNiagaraDataSetAccessor<float>(Data, TEXT("MaterialRandom")).GetReader(Data.GetCurrentData());
						Result += FString::Printf(TEXT("%s count=%d deterministic=%d seeds="), *Components[I]->GetName(), Count, Emitter->IsDeterministic());
						for (int32 P = 0; P < Count; ++P) Result += FString::Printf(TEXT("%.7f,"), Reader.GetSafe(P, -1));
						Result += TEXT("\n");
					}
				}
				bExclusionsValid &= Capture && Capture->HiddenComponents.Contains(Components[I]) == (bCaptureReady && I >= 3);
			}
			Result += FString::Printf(TEXT("materials_valid=%d exclusions_valid=%d capture_ready=%d particles=%d expected=%d\n"), bMaterialsValid, bExclusionsValid, bCaptureReady, TotalParticles, Actor->ActiveParticleCount);
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("CodexBlackHoleValidation/Accretion/scene_inspection.txt");
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) || !FFileHelper::SaveStringToFile(Result, *Path)) { PARADOX_LOG_ERROR(TEXT("Could not save accretion scene inspection.")); }
		else { PARADOX_LOG_INFO(TEXT("Accretion scene inspection saved: %s"), *Path); }
	}));

	FAutoConsoleCommand InspectAccretionTemplate(
		TEXT("Paradox.Accretion.InspectTemplate"),
		TEXT("Inspect the installed Niagara seed emitter's module inputs; does not change assets."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr,
				TEXT("/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle.SingleLoopingParticle"));
			FVersionedNiagaraEmitterData* Data = Emitter ? Emitter->GetLatestEmitterData() : nullptr;
			UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
			if (!Source || !Source->NodeGraph)
			{
				PARADOX_LOG_ERROR(TEXT("Accretion template inspection failed: installed emitter graph is unavailable."));
				return;
			}
			FString Result;
			for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
			{
				UNiagaraNodeFunctionCall* Function = Cast<UNiagaraNodeFunctionCall>(Node);
				if (!Function || !Function->FunctionScript) continue;
				Result += FString::Printf(TEXT("\nFUNCTION %s %s\n"), *Function->GetFunctionName(), *Function->FunctionScript->GetPathName());
				for (UEdGraphPin* Pin : Function->Pins)
					Result += FString::Printf(TEXT("CALL_PIN %s %s %s\n"), *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(), *Pin->DefaultValue);
				UNiagaraScriptSource* Module = Function->GetFunctionScriptSource();
				if (!Module || !Module->NodeGraph) continue;
				for (UEdGraphNode* Inner : Module->NodeGraph->Nodes)
					for (UEdGraphPin* Pin : Inner->Pins)
						if (Pin->PinName.ToString().StartsWith(TEXT("Module.")))
							Result += FString::Printf(TEXT("%s %s %s\n"), *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(), *Pin->DefaultValue);
			}
			const FString Path = FPaths::ProjectSavedDir() / TEXT("CodexBlackHoleValidation/accretion_template.txt");
			if (!FFileHelper::SaveStringToFile(Result, *Path))
			{
				PARADOX_LOG_ERROR(TEXT("Could not write accretion template report '%s'."), *Path);
			}
			else
			{
				PARADOX_LOG_INFO(TEXT("Accretion template report: %s"), *Path);
			}
		}));
}
#endif
