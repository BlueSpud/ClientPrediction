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

	// Add all the un-acked packets to the front buffer to be reused
	while (!BackBuffer.IsEmpty()) {
		FInputPacket Packet;
		BackBuffer.Dequeue(Packet);

		if (Packet.PacketNumber > PacketNumber) {
			FrontBuffer.Enqueue(Packet);
		}
	}

	// Add all the front buffer packets back into the queue
	while (!BackupFrontBuffer.IsEmpty()) {
		FInputPacket Packet;
		BackupFrontBuffer.Dequeue(Packet);
		FrontBuffer.Enqueue(Packet);
	}
}

void FInputBuffer::Ack(uint32 PacketNumber) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	Ack_Internal(PacketNumber);
}

void FInputBuffer::FastForward(uint32 PacketNumber) {
	std::lock_guard<std::mutex> Lock(ClientMutex);

	// Clear the back buffer up to the packet
	Ack_Internal(PacketNumber);
	
	while (!FrontBuffer.IsEmpty()) {
		if (FrontBuffer.Peek()->PacketNumber > PacketNumber) {
			break;
		}

		FInputPacket PoppedPacket;
		FrontBuffer.Dequeue(PoppedPacket);
	}
}

size_t FInputBuffer::ClientFrontBufferIsEmpty() {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	return FrontBuffer.IsEmpty();
	
}

size_t FInputBuffer::ServerBufferSize() {
	std::lock_guard<std::mutex> Lock(ServerMutex);
	return ServerBuffer.Num();
}

void FInputBuffer::QueueInputServer(const FInputPacket& Packet) {
	std::lock_guard<std::mutex> Lock(ServerMutex);
	ServerBuffer.Add(Packet);
	ServerBuffer.Sort();
}

void FInputBuffer::QueueInputClient(const FInputPacket& Packet) {
	std::lock_guard<std::mutex> Lock(ClientMutex);
	FrontBuffer.Enqueue(Packet);
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
	BackBuffer.Enqueue(OutPacket);
	return true;
}

void FInputBuffer::Ack_Internal(uint32 PacketNumber) {
	while (!BackBuffer.IsEmpty()) {
		if (BackBuffer.Peek()->PacketNumber > PacketNumber) {
			break;
		}

		FInputPacket PoppedPacket;
		BackBuffer.Dequeue(PoppedPacket);
	}
}
