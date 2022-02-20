#include "Input.h"

template <typename InputPacket>
void FInputBuffer<InputPacket>::Rewind(uint32 PacketNumber) {
	std::lock_guard<std::mutex> Lock(ClientMutex);

	// TQueue deletes the move / copy constructor so copy the existing frot buffer
	TQueue<FInputPacket> BackupFrontBuffer;
	while (!FrontBuffer.IsEmpty()) {
		FInputPacket Packet;
		FrontBuffer.Dequeue(Packet);
		BackupFrontBuffer.Enqueue(Packet);
	}

	ClientFrontBufferSize = 0;
	
	// Add all the un-acked packets to the front buffer to be reused
	while (!BackBuffer.IsEmpty()) {
		FInputPacket Packet;
		BackBuffer.Dequeue(Packet);

		if (Packet.PacketNumber > PacketNumber) {
			FrontBuffer.Enqueue(Packet);
			++ClientFrontBufferSize;
		}
	}

	// Add all the front buffer packets back into the queue
	while (!BackupFrontBuffer.IsEmpty()) {
		FInputPacket Packet;
		BackupFrontBuffer.Dequeue(Packet);
		
		FrontBuffer.Enqueue(Packet);
		++ClientFrontBufferSize;
	}
}

template <typename InputPacket>
void FInputBuffer<InputPacket>::Ack(uint32 PacketNumber) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	while (!BackBuffer.IsEmpty()) {
		if (BackBuffer.Peek()->PacketNumber > PacketNumber) {
			break;
		}

		FInputPacket PoppedPacket;
		BackBuffer.Dequeue(PoppedPacket);
	}
}

template <typename InputPacket>
uint32 FInputBuffer<InputPacket>::RemoteBufferSize() {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	return ClientFrontBufferSize;
}

template <typename InputPacket>
uint32 FInputBuffer<InputPacket>::AuthorityBufferSize() {
	std::lock_guard<std::mutex> Lock(ServerMutex);
	return ServerBuffer.Num();
}

template <typename InputPacket>
void FInputBuffer<InputPacket>::QueueInputAuthority(const FInputPacket& Packet) {
	std::lock_guard<std::mutex> Lock(ServerMutex);
	ServerBuffer.Add(Packet);

	// Sort the input buffer in case packets have come in out of order
	//
	// Pop() on a TArray takes the last element, so we want the input buffer to be sorted from greatest to least.
	// Then when we take the last element that will be the lowest number (ie next) input packet.
	ServerBuffer.Sort([](const FInputPacket& First, const FInputPacket& Second) {
		return First.PacketNumber > Second.PacketNumber;
	});
}

template <typename InputPacket>
void FInputBuffer<InputPacket>::QueueInputRemote(const FInputPacket& Packet) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	FrontBuffer.Enqueue(Packet);
	++ClientFrontBufferSize;
}

template <typename InputPacket>
bool FInputBuffer<InputPacket>::ConsumeInputAuthority(FInputPacket& OutPacket) {
	std::lock_guard<std::mutex> Lock(ServerMutex);
	// TODO Make this handle missing packets
	if (!ServerBuffer.Num()) {
		return false;
	}

	OutPacket = ServerBuffer.Pop();
	return true;
}

template <typename InputPacket>
bool FInputBuffer<InputPacket>::ConsumeInputRemote(FInputPacket& OutPacket) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	if (FrontBuffer.IsEmpty()) {
		return false;
	}

	FrontBuffer.Dequeue(OutPacket);
	--ClientFrontBufferSize;
	
	BackBuffer.Enqueue(OutPacket);
	return true;
}
