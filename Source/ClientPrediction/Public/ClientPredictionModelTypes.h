#pragma once

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ClientPrediction {
    extern CLIENTPREDICTION_API float ClientPredictionPositionTolerance;
    extern CLIENTPREDICTION_API float ClientPredictionVelocityTolerance;
    extern CLIENTPREDICTION_API float ClientPredictionRotationTolerance;
    extern CLIENTPREDICTION_API float ClientPredictionAngularVelTolerance;

    struct FPhysicsState {
        /** These mirror the Chaos properties for a particle */
        Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;

        Chaos::FVec3 X = Chaos::FVec3::ZeroVector;
        Chaos::FVec3 V = Chaos::FVec3::ZeroVector;

        Chaos::FRotation3 R = Chaos::FRotation3::Identity;
        Chaos::FVec3 W = Chaos::FVec3::ZeroVector;
    };

    template <typename StateType>
    struct FStateWrapper {
        /** The tick number that the state was generated on. So if receiving from the authority, this is the index according to the authority. */
        int32 TickNumber = INDEX_NONE;

        /** This is the tick index the input was sampled ON THE AUTO PROXY. */
        int32 InputPacketTickNumber = INDEX_NONE;

        FPhysicsState PhysicsState{};
        StateType Body{};

        uint8 Events = 0;

        // These are not sent over the network, they're used for local interpolation only
        Chaos::FReal StartTime = 0.0;
        Chaos::FReal EndTime = 0.0;

        /** This contains the estimated time elapsed in seconds since the auto proxy simulated a tick with TickNumber. This is only calculated on the authority. */
        Chaos::FReal EstimatedDelayFromClient = 0.0;

        /**
         * This contains the estimated time between when this state was generated and the point in time that the auto proxy was seeing for the simulated proxies.
         * This value is useful for hit registration since going back in time by this amount will show the world as the auto proxy saw it when it was generating input.
         * This is only calculated on the authority.
         */
        Chaos::FReal EstimatedClientSimProxyDelay = 0.0;

        void NetSerialize(FArchive& Ar, bool bSerializeFullState);
        bool ShouldReconcile(const FStateWrapper& State) const;

        void FillState(const class Chaos::FRigidBodyHandle_Internal* Handle);
        void Reconcile(class Chaos::FRigidBodyHandle_Internal* Handle) const;

        void Interpolate(const FStateWrapper Other, const Chaos::FReal Alpha);
    };

    inline void SerializeHalfPrecision(Chaos::FVec3& Vec, FArchive& Ar) {
        Chaos::FVec3f HalfVec;
        if (Ar.IsSaving()) {
            HalfVec = Vec;
        }

        Ar << HalfVec.X;
        Ar << HalfVec.Y;
        Ar << HalfVec.Z;

        if (Ar.IsLoading()) {
            Vec = HalfVec;
        }
    }

    inline void SerializeHalfPrecision(Chaos::FRotation3& Rot, FArchive& Ar) {
        Chaos::FRotation3f HalfRot;

        if (Ar.IsSaving()) {
            HalfRot = Rot;
        }

        Ar << HalfRot.X;
        Ar << HalfRot.Y;
        Ar << HalfRot.Z;
        Ar << HalfRot.W;

        if (Ar.IsLoading()) {
            Rot = HalfRot;
        }
    }

    template <typename StateType>
    void FStateWrapper<StateType>::NetSerialize(FArchive& Ar, bool bSerializeFullState) {
        Ar << TickNumber;

        if (bSerializeFullState) {
            Ar << InputPacketTickNumber;
        }

        Ar << PhysicsState.ObjectState;

        if (bSerializeFullState) {
            // Serialize manually to make sure that they are serialized as doubles
            Ar << PhysicsState.X.X;
            Ar << PhysicsState.X.Y;
            Ar << PhysicsState.X.Z;

            Ar << PhysicsState.V.X;
            Ar << PhysicsState.V.Y;
            Ar << PhysicsState.V.Z;

            Ar << PhysicsState.R.X;
            Ar << PhysicsState.R.Y;
            Ar << PhysicsState.R.Z;
            Ar << PhysicsState.R.W;

            Ar << PhysicsState.W.X;
            Ar << PhysicsState.W.Y;
            Ar << PhysicsState.W.Z;

            Ar << Events;
        }
        else {
            SerializeHalfPrecision(PhysicsState.X, Ar);
            SerializeHalfPrecision(PhysicsState.V, Ar);
            SerializeHalfPrecision(PhysicsState.R, Ar);
            SerializeHalfPrecision(PhysicsState.W, Ar);
        }

        Body.NetSerialize(Ar, bSerializeFullState);
    }

    template <typename StateType>
    bool FStateWrapper<StateType>::ShouldReconcile(const FStateWrapper& State) const {
        if (State.PhysicsState.ObjectState != PhysicsState.ObjectState) { return true; }
        if ((State.PhysicsState.X - PhysicsState.X).Size() > ClientPredictionPositionTolerance) { return true; }
        if ((State.PhysicsState.V - PhysicsState.V).Size() > ClientPredictionVelocityTolerance) { return true; }
        if ((State.PhysicsState.R - PhysicsState.R).Size() > ClientPredictionRotationTolerance) { return true; }
        if ((State.PhysicsState.W - PhysicsState.W).Size() > ClientPredictionAngularVelTolerance) { return true; }
        if (State.Events != Events) { return true; }

        return Body.ShouldReconcile(State.Body);
    }

    template <typename StateType>
    void FStateWrapper<StateType>::FillState(const Chaos::FRigidBodyHandle_Internal* Handle) {
        PhysicsState.X = Handle->X();
        PhysicsState.V = Handle->V();
        PhysicsState.R = Handle->R();
        PhysicsState.W = Handle->W();
        PhysicsState.ObjectState = Handle->ObjectState();
    }

    template <typename StateType>
    void FStateWrapper<StateType>::Reconcile(Chaos::FRigidBodyHandle_Internal* Handle) const {
        Handle->SetObjectState(PhysicsState.ObjectState);
        Handle->SetX(PhysicsState.X);
        Handle->SetV(PhysicsState.V);
        Handle->SetR(PhysicsState.R);
        Handle->SetW(PhysicsState.W);
    }

    template <typename StateType>
    void FStateWrapper<StateType>::Interpolate(const FStateWrapper Other, const Chaos::FReal Alpha) {
        PhysicsState.ObjectState = Other.PhysicsState.ObjectState;
        PhysicsState.X = FMath::Lerp<FVector>({PhysicsState.X}, {Other.PhysicsState.X}, Alpha);
        PhysicsState.V = FMath::Lerp<FVector>({PhysicsState.V}, {Other.PhysicsState.V}, Alpha);
        PhysicsState.R = FMath::Lerp<FQuat>({PhysicsState.R}, {Other.PhysicsState.R}, Alpha);
        PhysicsState.W = FMath::Lerp<FVector>({PhysicsState.W}, {Other.PhysicsState.W}, Alpha);

        Body.Interpolate(Other.Body, Alpha);
    }

    struct FControlPacket {
        void SetTimeDilation(const Chaos::FReal Dilation) {
            const int32 Rounded = FMath::RoundToInt(Dilation * 127.0);
            TimeDilation = FMath::Clamp(Rounded, -127, 127);
        }

        Chaos::FReal GetTimeDilation() const {
            return static_cast<Chaos::FReal>(TimeDilation) / static_cast<Chaos::FReal>(TNumericLimits<int8>::Max());
        }

        void NetSerialize(FArchive& Ar) {
            Ar << TimeDilation;
        }

    private:
        /** This stores time dilation in the range [-127, 127] (since 128 can't be represented with an int8)*/
        int8 TimeDilation = 0;
    };

    struct FNetworkConditions {
        Chaos::FReal Latency = 0.0;
        Chaos::FReal Jitter = 0.0;
    };
}
