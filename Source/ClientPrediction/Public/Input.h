#pragma once

#include <mutex>

#include "Declares.h"

template <typename InputPacket>
class FInputBuffer {

public:

	void Rewind(uint32 PacketNumber) {

		// TQueue deletes the move / copy constructor so copy the existing frot buffer
		TQueue<InputPacket> BackupFrontBuffer;
		while (!FrontBuffer.IsEmpty()) {
			InputPacket Packet;
			FrontBuffer.Dequeue(Packet);
			BackupFrontBuffer.Enqueue(Packet);
		}

		RemoteFrontBufferSize = 0;

		// Add all the un-acked packets to the front buffer to be reused
		while (!BackBuffer.IsEmpty()) {
			InputPacket Packet;
			BackBuffer.Dequeue(Packet);

			if (Packet.PacketNumber > PacketNumber) {
				FrontBuffer.Enqueue(Packet);
				++RemoteFrontBufferSize;
			}
		}

		// Add all the front buffer packets back into the queue
		while (!BackupFrontBuffer.IsEmpty()) {
			InputPacket Packet;
			BackupFrontBuffer.Dequeue(Packet);

			FrontBuffer.Enqueue(Packet);
			++RemoteFrontBufferSize;
		}
	}

	void Ack(uint32 PacketNumber) {
		while (!BackBuffer.IsEmpty()) {
			if (BackBuffer.Peek()->PacketNumber > PacketNumber) {
				break;
			}

			InputPacket PoppedPacket;
			BackBuffer.Dequeue(PoppedPacket);
		}
	}

	uint32 RemoteBufferSize() const {
		return RemoteFrontBufferSize;
	}

	uint32 AuthorityBufferSize() {
		return AuthorityBuffer.Num();
	}

	void QueueInputAuthority(const InputPacket& Packet) {

		// Ensure that the packet is not in the past
		if (AuthorityInputPacketNumber != kInvalidFrame && Packet.PacketNumber <= AuthorityInputPacketNumber) {
			return;
		}

		// Ensure that the packet is not already queued
		const InputPacket* ExistingPacket = AuthorityBuffer.FindByPredicate([&](const InputPacket& Candidate) {
			return Candidate.PacketNumber == Packet.PacketNumber;
		});

		if (ExistingPacket != nullptr) {
			return;
		}

		AuthorityBuffer.Add(Packet);

		// Sort the input buffer in case packets have come in out of order
		//
		// Pop() on a TArray takes the last element, so we want the input buffer to be sorted from greatest to least.
		// Then when we take the last element that will be the lowest number (ie next) input packet.
		AuthorityBuffer.Sort([](const InputPacket& First, const InputPacket& Second) {
			return First.PacketNumber > Second.PacketNumber;
		});
	}

	void QueueInputRemote(const InputPacket& Packet) {
		FrontBuffer.Enqueue(Packet);
		++RemoteFrontBufferSize;
	}

	bool ConsumeInputAuthority(InputPacket& OutPacket) {
		AuthorityInputPacketNumber = AuthorityInputPacketNumber == kInvalidFrame ? 0 : AuthorityInputPacketNumber + 1;
		if (!AuthorityBuffer.IsEmpty() && AuthorityBuffer.Last().PacketNumber == AuthorityInputPacketNumber) {
			LastAuthorityPacket = AuthorityBuffer.Pop();
			OutPacket = LastAuthorityPacket;

			return false;
		}
		
		
		OutPacket = LastAuthorityPacket;
		return true;
	}

	bool ConsumeInputRemote(InputPacket& OutPacket) {
		if (FrontBuffer.IsEmpty()) {
			return false;
		}

		FrontBuffer.Dequeue(OutPacket);
		--RemoteFrontBufferSize;

		BackBuffer.Enqueue(OutPacket);
		return true;
	}

private:

	/** On the remote the queued inputs. When rewinding the back buffer is swapped into the front buffer. */
    TQueue<InputPacket> FrontBuffer;

	uint32 RemoteFrontBufferSize = 0;
	
	/** The inputs that have already been used on the remote */
	TQueue<InputPacket> BackBuffer;

	/** The inputs from the authority */
	TArray<InputPacket> AuthorityBuffer;

	/** The frame number of the last input packet consumed on the authority */
	uint32 AuthorityInputPacketNumber = kInvalidFrame;

	/** The last packet consumed by the authority */
	InputPacket LastAuthorityPacket;

};

