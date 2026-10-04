#include "Environment/ParadoxAccretionExtension.h"
#include "Environment/ParadoxBlackHole.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"
#include "Paradox.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAccretionDebug(TEXT("Paradox.Accretion.Debug"), 0,
		TEXT("Enable accretion route visualization for instances with Enable Debug."));
	const int32 Counts[3][3] = { { 16, 24, 96 }, { 24, 40, 160 }, { 36, 64, 256 } };
	bool Finite(const FLinearColor& C) { return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) && FMath::IsFinite(C.B) && FMath::IsFinite(C.A); }
	bool Finite(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
	FLinearColor VectorColor(const FVector& V) { return FLinearColor(V.X, V.Y, V.Z, 0); }
}

AParadoxAccretionExtension::AParadoxAccretionExtension()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	GameplayPlane = CreateDefaultSubobject<USceneComponent>(TEXT("GameplayPlane"));
	GameplayPlane->SetupAttachment(Root);
	const TCHAR* Kinds[] = { TEXT("Gas"), TEXT("Filaments"), TEXT("Sparks") };
	for (int32 Layer = 0; Layer < 2; ++Layer)
		for (int32 Kind = 0; Kind < 3; ++Kind)
		{
			const FName Name(*FString::Printf(TEXT("%s_%s"), Layer ? TEXT("Foreground") : TEXT("Midground"), Kinds[Kind]));
			UNiagaraComponent* Component = CreateDefaultSubobject<UNiagaraComponent>(Name);
			Component->SetupAttachment(Root);
			Component->SetAutoActivate(false);
			Component->SetCastShadow(false);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			ParticleComponents.Add(Component);
		}
	for (const TCHAR* Kind : Kinds)
	{
		const FString SystemPath = FString(TEXT("/Game/Vfx/BlackHole/Accretion/NS_Accretion_")) + Kind;
		const FString MaterialPath = FString(TEXT("/Game/Vfx/BlackHole/Accretion/MI_Accretion_")) + Kind;
		ConstructorHelpers::FObjectFinderOptional<UNiagaraSystem> System(*SystemPath);
		ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(*MaterialPath);
		ParticleSystems.Add(System.Get());
		ParticleMaterials.Add(Material.Get());
	}
}

bool AParadoxAccretionExtension::ValidateTuning(const FParadoxAccretionTuning& V) const
{
	return Finite(V.LocalExtent) && V.LocalExtent.GetMin() >= 100 && V.LocalExtent.GetMax() <= 1000000
		&& Finite(V.BridgeControlA) && Finite(V.BridgeControlB)
		&& V.BridgeControlA.GetAbsMax() <= 4 && V.BridgeControlB.GetAbsMax() <= 4
		&& FMath::IsFinite(V.ForegroundIntensity) && V.ForegroundIntensity >= 0 && V.ForegroundIntensity <= 1
		&& FMath::IsFinite(V.MidgroundIntensity) && V.MidgroundIntensity >= 0 && V.MidgroundIntensity <= 2
		&& FMath::IsFinite(V.GasOpacity) && V.GasOpacity >= 0.005f && V.GasOpacity <= 0.25f
		&& FMath::IsFinite(V.Brightness) && V.Brightness >= 0 && V.Brightness <= 4
		&& FMath::IsFinite(V.LayerTransition) && V.LayerTransition >= 10 && V.LayerTransition <= 1000000
		&& FMath::IsFinite(V.SpeedMultiplier) && V.SpeedMultiplier >= 0 && V.SpeedMultiplier <= 4
		&& FMath::IsFinite(V.Lifetime) && V.Lifetime >= 2 && V.Lifetime <= 60
		&& FMath::IsFinite(V.Turbulence) && V.Turbulence >= 0 && V.Turbulence <= 1
		&& FMath::IsFinite(V.ParticleScale) && V.ParticleScale >= 0.1f && V.ParticleScale <= 3
		&& FMath::IsFinite(V.AnimationTimeOverride) && V.AnimationTimeOverride >= -1;
}

bool AParadoxAccretionExtension::Fail(const FString& Message)
{
	StopParticles();
	SetActorTickEnabled(false);
	if (StateMessage != Message)
	{
		PARADOX_LOG_WARNING(TEXT("Accretion extension '%s': %s"), *GetPathName(), *Message);
		StateMessage = Message;
	}
	return false;
}

void AParadoxAccretionExtension::StopParticles()
{
	for (UNiagaraComponent* Component : ParticleComponents)
		if (IsValid(Component))
		{
			Component->DeactivateImmediate();
			Component->SetComponentTickEnabled(false);
		}
	bActive = false;
	ActiveParticleCount = 0;
	DetachCapture();
}

void AParadoxAccretionExtension::DetachCapture()
{
	if (USceneCaptureComponent2D* Capture = RegisteredCapture.Get())
		for (UNiagaraComponent* Component : ParticleComponents)
			Capture->HiddenComponents.Remove(Component);
	RegisteredCapture.Reset();
}

void AParadoxAccretionExtension::UpdateCapture()
{
	USceneCaptureComponent2D* Capture = IsValid(SourceBlackHole) && SourceBlackHole->CaptureState == EParadoxBlackHoleCaptureState::Ready
		? SourceBlackHole->BackgroundCapture.Get() : nullptr;
	if (RegisteredCapture.Get() != Capture)
	{
		DetachCapture();
		RegisteredCapture = Capture;
	}
	if (IsValid(Capture))
		for (int32 I = 3; I < 6; ++I) Capture->HideComponent(ParticleComponents[I]);
}

bool AParadoxAccretionExtension::RefreshExtension()
{
	if (IsTemplate() || bShuttingDown || bRefreshing || !GetWorld()) return false;
	TGuardValue<bool> Guard(bRefreshing, true);
	if (!bEnabled || (!GetWorld()->IsGameWorld() && !bEnableEditorPreview) || GetNetMode() == NM_DedicatedServer)
	{
		StopParticles();
		SetActorTickEnabled(false);
		StateMessage = TEXT("Disabled; particle simulation and rendering stopped.");
		return true;
	}
	if (!ValidateTuning(Tuning) || static_cast<uint8>(Quality) >= 3) return Fail(TEXT("Invalid tuning or quality; previous valid public setter values are retained."));
	if (!IsValid(SourceBlackHole) || !IsValid(SourceBlackHole->Sphere)) return Fail(TEXT("Assign Source Black Hole to a valid black-hole actor."));
	if (SourceBlackHole->GetWorld() != GetWorld()) return Fail(TEXT("Source Black Hole must belong to the same world."));
	if (ParticleSystems.Num() != 3 || ParticleMaterials.Num() != 3 || ParticleComponents.Num() != 6) return Fail(TEXT("Three particle systems/materials and six native components are required."));
	if (!IsValid(GameplayPlane) || ParticleComponents.ContainsByPredicate([](const UNiagaraComponent* Component) { return !IsValid(Component); }))
		return Fail(TEXT("A required native gameplay-plane or particle component was removed."));
	for (int32 I = 0; I < 3; ++I)
	{
#if WITH_EDITOR
		// Editor loads can defer Niagara compilation; resolve that asset state before activation.
		if (IsValid(ParticleSystems[I]) && !ParticleSystems[I]->IsReadyToRun()) ParticleSystems[I]->WaitForCompilationComplete(true, false);
#endif
		if (!IsValid(ParticleSystems[I]) || !ParticleSystems[I]->IsReadyToRun() || !IsValid(ParticleMaterials[I]))
			return Fail(FString::Printf(TEXT("Accretion asset group %d is missing or not compiled; build the supplied Niagara systems/materials."), I));
	}
	Materials.SetNum(6);
	const TArray<int32> PreviousCounts = ConfiguredCounts;
	ConfiguredCounts.SetNumZeroed(6);
	bFieldDirty = true;
	const bool Features[] = { bEnableGas, bEnableFilaments, bEnableSparks };
	for (int32 I = 0; I < 6; ++I)
	{
		UNiagaraComponent* Component = ParticleComponents[I];
		if (Component->GetAsset() != ParticleSystems[I % 3]) Component->SetAsset(ParticleSystems[I % 3]);
		if (!IsValid(Materials[I]) || Materials[I]->Parent != ParticleMaterials[I % 3])
			Materials[I] = UMaterialInstanceDynamic::Create(ParticleMaterials[I % 3], this);
		Component->SetVariableMaterial(TEXT("User.ParticleMaterial"), Materials[I]);
		Component->SetTranslucentSortPriority(SourceBlackHole->Sphere->TranslucencySortPriority + (I < 3 ? -1 : 1));
		const int32 Count = Features[I % 3] ? Counts[static_cast<uint8>(Quality)][I % 3] : 0;
		Component->SetVariableInt(TEXT("User.ParticleCount"), Count);
		ConfiguredCounts[I] = Count;
		Materials[I]->SetScalarParameterValue(TEXT("Layer"), I < 3 ? 0 : 1);
	}
	if (!UpdateField()) return false;
	ActiveParticleCount = 0;
	for (int32 I = 0; I < 6; ++I)
	{
		UNiagaraComponent* Component = ParticleComponents[I];
		if (ConfiguredCounts[I] && SourceBlackHole->bEnableAccretionDisk)
		{
			Component->SetComponentTickEnabled(true);
			if (!Component->IsActive() || !PreviousCounts.IsValidIndex(I) || PreviousCounts[I] != ConfiguredCounts[I])
			{
				// Populate once, then keep the immutable seeds. Material Time animates WPO and shading.
				Component->SetForceSolo(true);
				Component->SetPaused(false);
				// A reset activation can retain the old emitter buffer until a later tick.
				// Reinitialize before our synchronous seed warmup, including quality changes.
				Component->ReinitializeSystem();
				Component->Activate(true);
				Component->AdvanceSimulation(2, 1.f / 60.f);
				Component->SetPaused(true);
				Component->MarkRenderDynamicDataDirty();
			}
			Component->SetComponentTickEnabled(false);
			ActiveParticleCount += ConfiguredCounts[I];
		}
		else
		{
			Component->DeactivateImmediate();
			Component->SetComponentTickEnabled(false);
		}
	}
	bActive = ActiveParticleCount > 0;
	SetActorTickEnabled(true);
	UpdateCapture();
	StateMessage = bActive ? TEXT("World-space extension active; foreground stays outside the background capture.") : TEXT("Disk or all detail groups disabled; preferences retained.");
	return true;
}

bool AParadoxAccretionExtension::UpdateField()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Paradox_Accretion_UpdateField);
	if (!IsValid(SourceBlackHole) || !IsValid(SourceBlackHole->Sphere)) return Fail(TEXT("Source Black Hole was removed."));
	if (!IsValid(GameplayPlane) || ParticleComponents.ContainsByPredicate([](const UNiagaraComponent* Component) { return !IsValid(Component); }))
		return Fail(TEXT("A required native gameplay-plane or particle component was removed."));
	const FVector Origin = GetActorLocation();
	const FVector Source = SourceBlackHole->Sphere->GetComponentLocation() - Origin;
	const FVector Plane = GameplayPlane->GetComponentLocation() - Origin;
	const FVector Axis = SourceBlackHole->Sphere->GetUpVector().GetSafeNormal();
	const FVector PlaneUp = GameplayPlane->GetUpVector().GetSafeNormal();
	const FVector Delta = Plane - Source;
	const double Radius = SourceBlackHole->Sphere->Bounds.SphereRadius;
	if (!Finite(Source) || !Finite(Plane) || !Finite(Axis) || !Finite(PlaneUp) || Axis.IsNearlyZero() || PlaneUp.IsNearlyZero()
		|| !Finite(GetActorScale3D()) || GetActorScale3D().GetAbsMin() <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Radius) || Radius <= 0 || Delta.Size() <= 1
		|| !FMath::IsFinite(SourceBlackHole->DiskOuterRadius) || !FMath::IsFinite(SourceBlackHole->RotationSpeed)
		|| !Finite(SourceBlackHole->InnerColor) || !Finite(SourceBlackHole->OuterColor))
		return Fail(TEXT("Source sphere radius/position and gameplay-plane position must define a finite nonzero route."));
	if (!bFieldDirty && CachedActorTransform.Equals(GetActorTransform())
		&& CachedSourceTransform.Equals(SourceBlackHole->Sphere->GetComponentTransform())
		&& CachedPlaneTransform.Equals(GameplayPlane->GetComponentTransform()) && CachedRadius == Radius
		&& CachedOuterRadius == SourceBlackHole->DiskOuterRadius && CachedRotationSpeed == SourceBlackHole->RotationSpeed
		&& CachedInnerColor == SourceBlackHole->InnerColor && CachedOuterColor == SourceBlackHole->OuterColor
		&& CachedStructureTexture.Get() == SourceBlackHole->DiskStructureTexture) return true;
	FVector Radial = FVector::VectorPlaneProject(Delta, Axis).GetSafeNormal();
	if (Radial.IsNearlyZero()) Radial = SourceBlackHole->Sphere->GetForwardVector();
	const FVector Start = Source + Radial * Radius * FMath::Clamp(SourceBlackHole->DiskOuterRadius, 0.01f, 0.95f);
	const FVector Route = Plane - Start;
	const double Distance = Route.Size();
	const FVector Along = Route.GetSafeNormal();
	FVector Side = FVector::CrossProduct(PlaneUp, Along).GetSafeNormal();
	if (Side.IsNearlyZero()) Side = GameplayPlane->GetRightVector();
	const auto Control = [&](const FVector& V) { return Start + Along * Distance * V.X + Side * Distance * V.Y + PlaneUp * Distance * V.Z; };
	const FVector A = Control(Tuning.BridgeControlA), B = Control(Tuning.BridgeControlB);
	FBox Bounds(ForceInit);
	for (const FVector& P : { Source, Start, A, B, Plane }) Bounds += P;
	Bounds = Bounds.ExpandBy(Tuning.LocalExtent.GetMax() * (1.5 + Tuning.ParticleScale * 0.35) + Distance * 0.20 * Tuning.ParticleScale);
	for (int32 I = 0; I < Materials.Num(); ++I)
	{
		UMaterialInstanceDynamic* M = Materials[I];
		if (!IsValid(M)) continue;
		M->SetVectorParameterValue(TEXT("RouteStart"), VectorColor(Start));
		M->SetVectorParameterValue(TEXT("RouteA"), VectorColor(A));
		M->SetVectorParameterValue(TEXT("RouteB"), VectorColor(B));
		M->SetVectorParameterValue(TEXT("PlaneOrigin"), VectorColor(Plane));
		M->SetVectorParameterValue(TEXT("PlaneUp"), VectorColor(PlaneUp));
		M->SetVectorParameterValue(TEXT("SourceCenter"), VectorColor(Source));
		M->SetVectorParameterValue(TEXT("SourceAxis"), VectorColor(Axis));
		M->SetVectorParameterValue(TEXT("LocalExtent"), VectorColor(Tuning.LocalExtent));
		M->SetVectorParameterValue(TEXT("InnerColor"), SourceBlackHole->InnerColor);
		M->SetVectorParameterValue(TEXT("OuterColor"), SourceBlackHole->OuterColor);
		M->SetScalarParameterValue(TEXT("SourceRadius"), Radius);
		M->SetScalarParameterValue(TEXT("RotationSpeed"), SourceBlackHole->RotationSpeed * Tuning.SpeedMultiplier);
		M->SetScalarParameterValue(TEXT("ForegroundIntensity"), Tuning.ForegroundIntensity);
		M->SetScalarParameterValue(TEXT("MidgroundIntensity"), Tuning.MidgroundIntensity);
		M->SetScalarParameterValue(TEXT("GasOpacity"), Tuning.GasOpacity);
		M->SetScalarParameterValue(TEXT("Brightness"), Tuning.Brightness);
		M->SetScalarParameterValue(TEXT("LayerTransition"), Tuning.LayerTransition);
		M->SetScalarParameterValue(TEXT("Lifetime"), Tuning.Lifetime);
		M->SetScalarParameterValue(TEXT("Turbulence"), Tuning.Turbulence);
		M->SetScalarParameterValue(TEXT("ParticleScale"), Tuning.ParticleScale);
		M->SetScalarParameterValue(TEXT("TimeOverride"), Tuning.AnimationTimeOverride);
		UTexture* Structure = SourceBlackHole->DiskStructureTexture;
		if (!IsValid(Structure)) ParticleMaterials[I % 3]->GetTextureParameterValue(FMaterialParameterInfo(TEXT("DiskStructure")), Structure);
		M->SetTextureParameterValue(TEXT("DiskStructure"), Structure);
		ParticleComponents[I]->SetSystemFixedBounds(Bounds.TransformBy(GetActorTransform().Inverse().ToMatrixWithScale()));
	}
	CachedActorTransform = GetActorTransform();
	CachedSourceTransform = SourceBlackHole->Sphere->GetComponentTransform();
	CachedPlaneTransform = GameplayPlane->GetComponentTransform();
	CachedRadius = Radius; CachedOuterRadius = SourceBlackHole->DiskOuterRadius; CachedRotationSpeed = SourceBlackHole->RotationSpeed;
	CachedInnerColor = SourceBlackHole->InnerColor; CachedOuterColor = SourceBlackHole->OuterColor;
	CachedStructureTexture = SourceBlackHole->DiskStructureTexture;
	CachedStart = Start; CachedA = A; CachedB = B; CachedPlane = Plane;
	bFieldDirty = false;
	return true;
}

void AParadoxAccretionExtension::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bEnabled || bShuttingDown) return;
	if (!UpdateField()) return;
#if ENABLE_DRAW_DEBUG
	if (bEnableDebug && CVarAccretionDebug.GetValueOnGameThread() != 0)
	{
		FVector Previous = CachedStart;
		for (int32 I = 1; I <= 24; ++I)
		{
			const double T = I / 24.0, U = 1 - T;
			const FVector P = CachedStart * U*U*U + CachedA * 3*U*U*T + CachedB * 3*U*T*T + CachedPlane * T*T*T;
			DrawDebugLine(GetWorld(), GetActorLocation() + Previous, GetActorLocation() + P, FColor::Cyan, false, 0, 0, 3);
			Previous = P;
		}
		DrawDebugBox(GetWorld(), GetActorLocation() + CachedPlane, Tuning.LocalExtent, FColor::Silver, false, 0);
	}
#endif
	if (bActive != (SourceBlackHole->bEnableAccretionDisk && (bEnableGas || bEnableFilaments || bEnableSparks))) RefreshExtension();
	else UpdateCapture();
}

void AParadoxAccretionExtension::SetSourceBlackHole(AParadoxBlackHole* InSource) { DetachCapture(); SourceBlackHole = InSource; RefreshExtension(); }
void AParadoxAccretionExtension::SetEnabled(bool V) { bEnabled = V; RefreshExtension(); }
void AParadoxAccretionExtension::SetGasEnabled(bool V) { bEnableGas = V; RefreshExtension(); }
void AParadoxAccretionExtension::SetFilamentsEnabled(bool V) { bEnableFilaments = V; RefreshExtension(); }
void AParadoxAccretionExtension::SetSparksEnabled(bool V) { bEnableSparks = V; RefreshExtension(); }
void AParadoxAccretionExtension::SetQuality(EParadoxAccretionQuality V)
{
	if (static_cast<uint8>(V) >= 3) { PARADOX_LOG_WARNING(TEXT("Accretion '%s' rejected invalid quality; previous selection retained."), *GetPathName()); return; }
	Quality = V; RefreshExtension();
}
bool AParadoxAccretionExtension::SetTuning(const FParadoxAccretionTuning& V)
{
	if (!ValidateTuning(V)) { PARADOX_LOG_WARNING(TEXT("Accretion '%s' rejected invalid tuning."), *GetPathName()); return false; }
	Tuning = V; return IsTemplate() || !GetWorld() || RefreshExtension();
}
TArray<UNiagaraComponent*> AParadoxAccretionExtension::GetParticleComponents() const
{
	TArray<UNiagaraComponent*> Result; for (UNiagaraComponent* C : ParticleComponents) Result.Add(C); return Result;
}
void AParadoxAccretionExtension::OnConstruction(const FTransform& T) { Super::OnConstruction(T); RefreshExtension(); }
void AParadoxAccretionExtension::BeginPlay() { Super::BeginPlay(); RefreshExtension(); }
bool AParadoxAccretionExtension::ShouldTickIfViewportsOnly() const { return bEnableEditorPreview && bEnabled; }
#if WITH_EDITOR
void AParadoxAccretionExtension::PostEditChangeProperty(FPropertyChangedEvent& E) { Super::PostEditChangeProperty(E); RefreshExtension(); }
#endif
void AParadoxAccretionExtension::EndPlay(const EEndPlayReason::Type R) { bShuttingDown = true; StopParticles(); Super::EndPlay(R); }
void AParadoxAccretionExtension::Destroyed() { bShuttingDown = true; StopParticles(); Super::Destroyed(); }
