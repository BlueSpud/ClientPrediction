#pragma once

#include "CoreMinimal.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

#include "ClientPredictionTick.h"

namespace ClientPrediction {
    struct FUtils {
    private:
        FUtils() = default;

    public:
        static inline APlayerController* GetPlayerController(const UWorld* World) {
            if (World == nullptr || !World->IsGameWorld()) { return nullptr; }

            return World->GetFirstPlayerController();
        }

        static inline FPhysScene* GetPhysScene(const UWorld* World) {
            if (World == nullptr || !World->IsGameWorld()) { return nullptr; }

            return World->GetPhysicsScene();
        }

        static inline Chaos::FPhysicsSolver* GetPhysSolver(const UWorld* World) {
            FPhysScene* PhysScene = GetPhysScene(World);
            if (PhysScene == nullptr) { return nullptr; }

            return PhysScene->GetSolver();
        }

        static inline bool FillTickInfo(FTickInfo& Info, int32 LocalTick, ENetRole Role, const UWorld* World) {
            Chaos::FPhysicsSolver* PhysSolver = GetPhysSolver(World);
            if (PhysSolver == nullptr) { return false; }

            Info.Dt = PhysSolver->GetAsyncDeltaTime();
            Info.bIsResim = PhysSolver->GetEvolution()->IsResimming();

            if (Role != ENetRole::ROLE_Authority) {
                APlayerController* PlayerController = GetPlayerController(World);
                if (PlayerController == nullptr || !PlayerController->GetNetworkPhysicsTickOffsetAssigned()) {
                    return false;
                }

                Info.LocalTick = LocalTick;
                Info.ServerTick = LocalTick + PlayerController->GetNetworkPhysicsTickOffset();
            }
            else {
                Info.LocalTick = LocalTick;
                Info.ServerTick = LocalTick;
            }

            return true;
        }
    };
}
