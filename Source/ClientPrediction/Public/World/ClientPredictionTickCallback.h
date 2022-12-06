#pragma once

namespace ClientPrediction {
	class ITickCallback {
	public:
		virtual ~ITickCallback() = default;

		/**
		 * @brief Called before a tick on the game thread.
		 * @param [in] TickNumber The index of the current tick.
		 * @param [in] Dt The delta time for the tick that is about to run.
		 */
		virtual void PreTickGameThread(int32 TickNumber, Chaos::FReal Dt) {}

		/**
		 * @brief Called before a tick on the physics thread.
		 * @param [in] TickNumber The index of the current tick.
		 * @param [in] Dt The delta time for the tick that is about to run.
		 */
		virtual void PreTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {}

		/**
		 * @brief Called after a tick on the physics thread.
		 * @param [in] TickNumber The index of the current tick.
		 * @param [in] Dt The delta time for the tick that has just run.
		 */
		virtual void PostTickPhysicsThread(int32 TickNumber, Chaos::FReal Dt) {}

		/**
		 * @brief Gets the tick to rewind to.
		 * @param [in] CurrentTickNumber The index of the current tick.
		 * @param [in] Dt The delta time for the tick.
		 * @return INDEX_NONE if no rewind is requested, otherwise the tick number of the tick to rewind to.
		 */
		virtual int32 GetRewindTickNumber(int32 CurrentTickNumber, Chaos::FReal Dt) { return INDEX_NONE; }
	};
}