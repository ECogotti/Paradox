#include "Perception/ParadoxSemanticNoiseSphere.h"

#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Paradox.h"
#include "UObject/ConstructorHelpers.h"

AParadoxSemanticNoiseSphere::AParadoxSemanticNoiseSphere()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(75.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	SetRootComponent(Trigger);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
		Mesh->SetRelativeScale3D(FVector(1.5f));
	}

	PerceptionSource =
		CreateDefaultSubobject<UPerceptionKnowledgeSourceComponent>(
			TEXT("PerceptionKnowledgeSource"));
	EventTag = ParadoxGameplayTags::Test_Event_Noise.GetTag();
	Trigger->OnComponentBeginOverlap.AddUniqueDynamic(
		this,
		&ThisClass::HandleOverlap);
}

FPerceptionKnowledgeOperationResult
AParadoxSemanticNoiseSphere::EmitSemanticNoise(AActor* InstigatorActor)
{
	const double CurrentTime = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: 0.0;
	if (bOneShot && bHasEmitted)
	{
		return MakeSuppressedResult(
			TEXT("The one-shot semantic noise has already been emitted."));
	}
	if (CurrentTime - LastEmissionWorldTime
		< FMath::Max(0.0f, CooldownSeconds))
	{
		return MakeSuppressedResult(
			TEXT("The semantic noise cooldown is still active."));
	}

	FPerceptionKnowledgeNoiseRequest Request;
	Request.EventTag = EventTag;
	Request.Instigator = InstigatorActor ? InstigatorActor : this;
	Request.bUseSourceLocation = true;
	Request.Loudness = Loudness;
	Request.MaxRange = MaxRange;
	Request.Strength = Loudness;
	const FPerceptionKnowledgeOperationResult Result =
		PerceptionSource
			? PerceptionSource->EmitSemanticNoise(Request)
			: FPerceptionKnowledgeOperationResult();
	if (Result.IsSuccess())
	{
		LastEmissionWorldTime = CurrentTime;
		bHasEmitted = true;
	}
	return Result;
}

void AParadoxSemanticNoiseSphere::ResetEmission()
{
	bHasEmitted = false;
	LastEmissionWorldTime = -DBL_MAX;
}

void AParadoxSemanticNoiseSphere::HandleOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (bEmitOnOverlap && OtherActor && OtherActor != this)
	{
		EmitSemanticNoise(OtherActor);
	}
}

FPerceptionKnowledgeOperationResult
AParadoxSemanticNoiseSphere::MakeSuppressedResult(
	const FString& Diagnostic) const
{
	FPerceptionKnowledgeOperationResult Result;
	Result.Status = EPerceptionKnowledgeOperationStatus::Unchanged;
	Result.Message = Diagnostic;
	return Result;
}
