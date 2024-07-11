#include "GoKartMovementReplicatorA.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

// Sets default values for this component's properties
UGoKartMovementReplicatorA::UGoKartMovementReplicatorA()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	SetIsReplicated(true);
}


// Called when the game starts
void UGoKartMovementReplicatorA::BeginPlay()
{
	Super::BeginPlay();

	MovementComponent = GetOwner()->FindComponentByClass<UGoKartMovementComponentA>();

}


// Called every frame
void UGoKartMovementReplicatorA::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (MovementComponent == nullptr) return;

	FGoKartMove LastMove = MovementComponent->GetLastMove();

	if (GetOwnerRole() == ROLE_AutonomousProxy)
	{
		UnacknowledgedMoves.Add(LastMove);
		Server_SendMove(LastMove);
	}

	// We are the server and in control of the pawn.
	// if (GetOwner()->GetRemoteRole() == ROLE_SimulatedProxy)
	if (IsLocallyControlled())
	{
		UpdateServerState(LastMove);
	}

	if (GetOwnerRole() == ROLE_SimulatedProxy)
	{
		ClientTick(DeltaTime);
	}
}

void UGoKartMovementReplicatorA::UpdateServerState(const FGoKartMove& Move)
{
	ServerState.LastMove = Move;
	ServerState.Tranform = GetOwner()->GetActorTransform();
	ServerState.Velocity = MovementComponent->GetVelocity();
}

void UGoKartMovementReplicatorA::ClientTick(float DeltaTime)
{
	ClientTimeSinceUpdate += DeltaTime;

	if (ClientTimeBetweenLastUpdates < KINDA_SMALL_NUMBER) return;
	if (MovementComponent == nullptr) return;

	float LerpRatio = ClientTimeSinceUpdate / ClientTimeBetweenLastUpdates;
	FHermiteCubicSpline Spline = CreateSpline();

	InterpolateLocation(Spline, LerpRatio);
	InterpolateVelocity(Spline, LerpRatio);
	InterpolateRotation(LerpRatio);
}


FHermiteCubicSpline UGoKartMovementReplicatorA::CreateSpline()
{
	FHermiteCubicSpline Spline;
	Spline.TargetLocation = ServerState.Tranform.GetLocation();
	Spline.StartLocation = ClientStartTransform.GetLocation();
	Spline.StartDerivative = ClientStartVelocity * VelocityToDerivative();
	Spline.TargetDerivative = ServerState.Velocity * VelocityToDerivative();
	return Spline;
}

void UGoKartMovementReplicatorA::InterpolateLocation(const FHermiteCubicSpline& Spline, float LerpRatio)
{
	FVector NewLocation = Spline.InterpolateLocation(LerpRatio);
	if (MeshOffsetRoot != nullptr)
	{
		MeshOffsetRoot->SetWorldLocation(NewLocation);
	}
}

void UGoKartMovementReplicatorA::InterpolateVelocity(const FHermiteCubicSpline& Spline, float LerpRatio)
{
	FVector NewDerivative = Spline.InterpolateDerivative(LerpRatio);
	FVector NewVelocity = NewDerivative / VelocityToDerivative();
	MovementComponent->SetVelocity(NewVelocity);
}

void UGoKartMovementReplicatorA::InterpolateRotation(float LerpRatio)
{
	FQuat TargetRotation = ServerState.Tranform.GetRotation();
	FQuat StartRotation = ClientStartTransform.GetRotation();

	FQuat NewRotation = FQuat::Slerp(StartRotation, TargetRotation, LerpRatio);

	if (MeshOffsetRoot != nullptr)
	{
		MeshOffsetRoot->SetWorldRotation(NewRotation);
	}
}

float UGoKartMovementReplicatorA::VelocityToDerivative()
{
	return ClientTimeBetweenLastUpdates * 100;
}

void UGoKartMovementReplicatorA::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGoKartMovementReplicatorA, ServerState);
}

void UGoKartMovementReplicatorA::OnRep_ServerState()
{
	switch (GetOwnerRole())
	{
	case ROLE_AutonomousProxy:
		AutonomousProxy_OnRep_ServerState();
		break;
	case ROLE_SimulatedProxy:
		SimulatedProxy_OnRep_ServerState();
		break;
	default:
		break;
	}
}

void UGoKartMovementReplicatorA::AutonomousProxy_OnRep_ServerState()
{
	if (MovementComponent == nullptr) return;
	GetOwner()->SetActorTransform(ServerState.Tranform);
	MovementComponent->SetVelocity(ServerState.Velocity);
	ClearAcknowledgeMoves(ServerState.LastMove);
	for (const FGoKartMove& Move : UnacknowledgedMoves)
	{
		MovementComponent->SimulateMove(Move);
	}
}

void UGoKartMovementReplicatorA::SimulatedProxy_OnRep_ServerState()
{
	if (MovementComponent == nullptr) return;

	ClientTimeBetweenLastUpdates = ClientTimeSinceUpdate;
	ClientTimeSinceUpdate = 0;

	if (MeshOffsetRoot != nullptr)
	{
		ClientStartTransform.SetLocation(MeshOffsetRoot->GetComponentLocation());
		ClientStartTransform.SetRotation(MeshOffsetRoot->GetComponentQuat());
	}
	ClientStartVelocity = MovementComponent->GetVelocity();

	GetOwner()->SetActorTransform(ServerState.Tranform);
}

void UGoKartMovementReplicatorA::ClearAcknowledgeMoves(FGoKartMove LastMove)
{
	TArray<FGoKartMove> NewMoves;

	for (const FGoKartMove& Move : UnacknowledgedMoves)
	{
		if (Move.Time > LastMove.Time)
		{
			NewMoves.Add(Move);
		}
	}

	UnacknowledgedMoves = NewMoves;
}

void UGoKartMovementReplicatorA::Server_SendMove_Implementation(FGoKartMove Move)
{
	if (MovementComponent == nullptr) return;

	ClientSimulatedTime += Move.DeltaTime;
	MovementComponent->SimulateMove(Move);

	UpdateServerState(Move);
}

bool UGoKartMovementReplicatorA::Server_SendMove_Validate(FGoKartMove Move)
{
	float ProposedTime = ClientSimulatedTime + Move.DeltaTime;
	bool ClientNotRunningAhead = ProposedTime < GetWorld()->TimeSeconds;
	if (!ClientNotRunningAhead) {
		UE_LOG(LogTemp, Error, TEXT("Client is running too fast."))
		return false;
	}

	if (!Move.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Received invalid move."))
		return false;
	}

	return true;
}

bool UGoKartMovementReplicatorA::IsLocallyControlled()
{
	auto* Owner = Cast<APawn>(GetOwner());

	if (!Owner)
	{
		return false;
	}

	return Owner->IsLocallyControlled();
}