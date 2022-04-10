#pragma once

#include <mutex>

#include "Declares.h"

template <typename InputPacket>
class FInputBuffer {

public:
	
	void Rewind(uint32 PacketNumber) {
		std::lock_guard<std::mutex> Lock(RemoteMutex);

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
		std::lock_guard<std::mutex> Lock(RemoteMutex);
		while (!BackBuffer.IsEmpty()) {
			if (BackBuffer.Peek()->PacketNumber > PacketNumber) {
				break;
			}

			InputPacket PoppedPacket;
			BackBuffer.Dequeue(PoppedPacket);
		}
	}

	uint32 RemoteBufferSize() {
		std::lock_guard<std::mutex> Lock(RemoteMutex);
		return RemoteFrontBufferSize;
	}
	
	uint32 AuthorityBufferSize() {
		std::lock_guard<std::mutex> Lock(AuthorityMutex);
		return AuthorityBuffer.Num();
	}

	void QueueInputAuthority(const InputPacket& Packet) {
		std::lock_guard<std::mutex> Lock(AuthorityMutex);

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
		std::lock_guard<std::mutex> Lock(RemoteMutex);
		FrontBuffer.Enqueue(Packet);
		++RemoteFrontBufferSize;
	}
	
	void ConsumeInputAuthority(InputPacket& OutPacket) {
		std::lock_guard<std::mutex> Lock(AuthorityMutex);
		uint32 AuthorityBufferSize = AuthorityBuffer.Num();

		// Attempt to keep the buffer reasonably close to the target size. This will cause minor client de-syncs
		if (AuthorityBufferSize > 2 && AuthorityBufferSize > TargetAuthorityBufferSize * 1.75) {
			// Consume two packets because there are too many in the buffer
			OutPacket = AuthorityBuffer.Pop();
			OutPacket = AuthorityBuffer.Pop();
		} else if (AuthorityBufferSize <= TargetAuthorityBufferSize * 0.25) {
			// Consume no packets because there are not enough in the buffer
			OutPacket = LastAuthorityPacket;
		} else if (AuthorityInputPacketNumber + 1 != AuthorityBuffer[AuthorityBufferSize - 1].PacketNumber) {
			// If the next packet was not the next packet in the sequence, just assume that the missing packet was the same as the last one.
			// This can also lead to minor client de-syncs if the missing input packet on the client is different from the last packet.
			UE_LOG(LogTemp, Warning, TEXT("Did not use sequential input %i %i"), AuthorityInputPacketNumber + 1, AuthorityBuffer[AuthorityBufferSize - 1].PacketNumber);
			OutPacket = LastAuthorityPacket;
			OutPacket.PacketNumber = AuthorityInputPacketNumber + 1;
		} else {
			OutPacket = AuthorityBuffer.Pop();
		}
		
		AuthorityInputPacketNumber = OutPacket.PacketNumber;
		LastAuthorityPacket = OutPacket;
	}
	
	bool ConsumeInputRemote(InputPacket& OutPacket) {
		std::lock_guard<std::mutex> Lock(RemoteMutex);
		if (FrontBuffer.IsEmpty()) {
			return false;
		}

		FrontBuffer.Dequeue(OutPacket);
		--RemoteFrontBufferSize;
	
		BackBuffer.Enqueue(OutPacket);
		return true;
	}

	void SetAuthorityTargetBufferSize(uint32 TargetSize) {
		std::lock_guard<std::mutex> Lock(AuthorityMutex);
		TargetAuthorityBufferSize = TargetSize;
	}

	uint32 GetAuthorityTargetBufferSize() {
		std::lock_guard<std::mutex> Lock(AuthorityMutex);
		return TargetAuthorityBufferSize;
	}
	
private:

	/** On the remote the queued inputs. When rewinding the back buffer is swapped into the front buffer. */
    TQueue<InputPacket> FrontBuffer;

	/** The inputs that have already been used on the remote */
	TQueue<InputPacket> BackBuffer;

	/** The inputs from the authority */
	TArray<InputPacket> AuthorityBuffer;

	uint32 RemoteFrontBufferSize = 0;

	/** The frame number of the last input packet consumed on the authority */
	uint32 AuthorityInputPacketNumber = kInvalidFrame; 

	/** The target size for the remote buffer */
	uint32 TargetAuthorityBufferSize = 1;

	/** The last packet consumed by the authority */
	InputPacket LastAuthorityPacket;
	
	std::mutex AuthorityMutex;
	std::mutex RemoteMutex;

};
	
