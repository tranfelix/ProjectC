#include "Shotgun.h"

#include "Engine/SkeletalMeshSocket.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Particles/ParticleSystemComponent.h"
#include "ProjectC/Character/MannequinCharacter.h"
#include "ProjectC/Components/LagCompensationComponent.h"
#include "ProjectC/PlayerController/MannequinPlayerController.h"

void AShotgun::FireShotgun(const TArray<FVector_NetQuantize>& TraceHitTargets)
{
	AWeapon::Fire(FVector());
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn == nullptr) return;
	AController* InstigatorController = OwnerPawn->GetController();

	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");
	if (MuzzleFlashSocket)
	{
		const FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());
		const FVector Start = SocketTransform.GetLocation();

		TMap<AMannequinCharacter*, uint32> HitMap;
		TMap<AMannequinCharacter*, uint32> HeadshotHitMap;
		for (FVector_NetQuantize HitTarget : TraceHitTargets)
		{
			FHitResult FireHit;
			WeaponTraceHit(Start, HitTarget, FireHit);

			AMannequinCharacter* MannequinCharacter = Cast<AMannequinCharacter>(FireHit.GetActor());
			if (MannequinCharacter)
			{
				const bool bHeadShot = FireHit.BoneName.ToString() == FString("head");

				if (bHeadShot)
				{
					if (HeadshotHitMap.Contains(MannequinCharacter)) HeadshotHitMap[MannequinCharacter]++;
					else HeadshotHitMap.Emplace(MannequinCharacter, 1);
				}
				else
				{
					if (HitMap.Contains(MannequinCharacter)) HitMap[MannequinCharacter]++;
					else HitMap.Emplace(MannequinCharacter, 1);
				}
				
				if (ImpactParticles)
				{
					UGameplayStatics::SpawnEmitterAtLocation(
						GetWorld(),
						ImpactParticles,
						FireHit.ImpactPoint,
						FireHit.ImpactNormal.Rotation()
					);
				}
				if (HitSound)
				{
					UGameplayStatics::PlaySoundAtLocation(
						this,
						HitSound,
						FireHit.ImpactPoint,
						.5f,
						FMath::FRandRange(-.5f, .5f)
					);
				}
			}
		}
		TArray<AMannequinCharacter*> HitCharacters;
		TMap<AMannequinCharacter*, float> DamageMap;
		for (auto HitPair : HitMap)
		{
			if (HitPair.Key)
			{
				DamageMap.Emplace(HitPair.Key, HitPair.Value * Damage);
				HitCharacters.AddUnique(HitPair.Key);
			}
		}
		for (auto HeadshotHitPair : HeadshotHitMap) 
		{
			if (HeadshotHitPair.Key)
			{
				if (DamageMap.Contains(HeadshotHitPair.Key)) DamageMap[HeadshotHitPair.Key] += HeadshotHitPair.Value * HeadShotDamage;
				else DamageMap.Emplace(HeadshotHitPair.Key, HeadshotHitPair.Value * HeadShotDamage);
				HitCharacters.AddUnique(HeadshotHitPair.Key);
			}
		}
		for (auto DamagePair : DamageMap)
		{
			if (DamagePair.Key && InstigatorController)
			{
				bool bCauseAuthDamage = !bUseServerSideRewind || OwnerPawn->IsLocallyControlled();
				if (HasAuthority() && bCauseAuthDamage)
				{
					UGameplayStatics::ApplyDamage(
						DamagePair.Key,
						DamagePair.Value,
						InstigatorController,
						this,
						UDamageType::StaticClass()
					);
				}
			}
		}
		if (!HasAuthority() && bUseServerSideRewind)
		{
			WeaponOwnerCharacter = WeaponOwnerCharacter == nullptr ? Cast<AMannequinCharacter>(OwnerPawn) : WeaponOwnerCharacter;
			WeaponOwnerController = WeaponOwnerController == nullptr ? Cast<AMannequinPlayerController>(InstigatorController) : WeaponOwnerController;
			if (WeaponOwnerCharacter && WeaponOwnerController && WeaponOwnerCharacter->GetLagCompensation() && WeaponOwnerCharacter->IsLocallyControlled())
			{
				WeaponOwnerCharacter->GetLagCompensation()->ShotgunServerScoreRequest(
					HitCharacters,
					Start,
					TraceHitTargets,
					WeaponOwnerController->GetServerTime() - WeaponOwnerController->SingleTripTime
				);
			}
		}
	}
}

void AShotgun::ShotgunTraceEndWithScatter(const FVector& HitTarget, TArray<FVector_NetQuantize>& HitTargets)
{
	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");
	if (MuzzleFlashSocket == nullptr) return;

	const FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());
	const FVector TraceStart = SocketTransform.GetLocation();
	const FVector ToTargetNormalized = (HitTarget - TraceStart).GetSafeNormal();
	const FVector SphereCenter = TraceStart + ToTargetNormalized * DistanceToSphere;

	for (uint32 i = 0; i < NumberOfPellets; i++)
	{
		const FVector RandVec = UKismetMathLibrary::RandomUnitVector() * FMath::FRandRange(0.f, SphereRadius);
		const FVector EndLoc = SphereCenter + RandVec;
		const FVector ToEndLoc = EndLoc - TraceStart;
		HitTargets.Add(TraceStart + ToEndLoc * TRACE_LENGTH / ToEndLoc.Size());
	}
}
