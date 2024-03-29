﻿#pragma once

namespace ClientPrediction {
    /**
     * If multiple physics ticks are being run in one game tick, PrepareTickGameThread() will be called for each physics tick
     * before any actual physics work is done. For instance, if we run tick 4,5 and 6 the order will be:
     * Prepare 4,5,6
     * PreTick 4
     * PostTick 4
     * PreTick 5
     * PostTick 5
     * PreTick 6
     * PostTick 6
     * Post physics
     */
    class ITickCallback {
    public:
        virtual ~ITickCallback() = default;

        /**
         * Called to prepare for a tick on the game thread.
         * @param [in] TickNumber The index of the current tick.
         * @param [in] Dt The delta time for the tick that is about to run.
         */
        virtual void PrepareTickGameThread(int32 TickNumber, Chaos::FReal Dt) {}

        /**
         * Called before a tick on the physics thread.
         * @param [in] TickNumber The index of the current tick.
         * @param [in] Dt The delta time for the tick that is about to run.
         * @param [in] StartTime The absolute start time of the finished tick.
         * @param [in] EndTime The absolute end time of the finished tick.
         */
        virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt, Chaos::FReal StartTime, Chaos::FReal EndTime) {}

        /**
         * Called after a tick on the physics thread.
         * @param [in] TickNumber The index of the current tick.
         * @param [in] Dt The delta time for the tick that has just run.
         */
        virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {}

        /**
         * @brief Called after physics finishes on the game thread. It's possible that none, one or many physics ticks were executed.
         * At this point, physics results have been marshalled to the game thread.
         * @param [in] SimTime The absolute time of the simulation. This should be used for interpolation between two states generated on the physics thread.
         * @param [in] Dt The time between now and the last call to PostPhysicsGameThread.
         */
        virtual void PostPhysicsGameThread(Chaos::FReal SimTime, Chaos::FReal Dt) {}
    };

    class IRewindCallback {
    public:
        virtual ~IRewindCallback() = default;

        /**
         * Gets the tick to rewind to. Called on the physics thread.
         * @param [in] CurrentTickNumber The index of the current tick.
         * @param [in] RewindData The physics rewind data.
         * @return INDEX_NONE if no rewind is requested, otherwise the tick number of the tick to rewind to.
         */
        virtual int32 GetRewindTickNumber(int32 CurrentTickNumber, const class Chaos::FRewindData& RewindData) { return INDEX_NONE; }
    };
}
