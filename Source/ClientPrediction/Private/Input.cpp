#include "Input.h"

void FInputBuffer::Rewind(uint32 PacketNumber) {
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

void FInputBuffer::Ack(uint32 PacketNumber) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	while (!BackBuffer.IsEmpty()) {
		if (BackBuffer.Peek()->PacketNumber > PacketNumber) {
			break;
		}

		FInputPacket PoppedPacket;
		BackBuffer.Dequeue(PoppedPacket);
	}
}

uint32 FInputBuffer::ClientBufferSize() {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	return ClientFrontBufferSize;
}

uint32 FInputBuffer::ServerBufferSize() {
	std::lock_guard<std::mutex> Lock(ServerMutex);
	return ServerBuffer.Num();
}

void FInputBuffer::QueueInputServer(const FInputPacket& Packet) {
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

void FInputBuffer::QueueInputClient(const FInputPacket& Packet) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	FrontBuffer.Enqueue(Packet);
	++ClientFrontBufferSize;
}

bool FInputBuffer::ConsumeInputServer(FInputPacket& OutPacket) {
	std::lock_guard<std::mutex> Lock(ServerMutex);
	// TODO Make this handle missing packets
	if (!ServerBuffer.Num()) {
		return false;
	}

	OutPacket = ServerBuffer.Pop();
	return true;
}

bool FInputBuffer::ConsumeInputClient(FInputPacket& OutPacket) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	if (FrontBuffer.IsEmpty()) {
		return false;
	}

	FrontBuffer.Dequeue(OutPacket);
	--ClientFrontBufferSize;
	
	BackBuffer.Enqueue(OutPacket);
	return true;
}
