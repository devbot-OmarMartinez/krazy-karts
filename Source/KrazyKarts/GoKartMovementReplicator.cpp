// Fill out your copyright notice in the Description page of Project Settings.


#include "GoKartMovementReplicator.h"

// Sets default values
AGoKartMovementReplicator::AGoKartMovementReplicator()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AGoKartMovementReplicator::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AGoKartMovementReplicator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

