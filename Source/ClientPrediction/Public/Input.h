#pragma once

#include <mutex>

template <typename InputPacket>
class FInputBuffer {

public:
	
	void Rewind(uint32 PacketNumber) {
		std::lock_guard<std::mutex> Lock(ClientMutex);

		// TQueue deletes the move / copy constructor so copy the existing frot buffer
		TQueue<InputPacket> BackupFrontBuffer;
		while (!FrontBuffer.IsEmpty()) {
			InputPacket Packet;
			FrontBuffer.Dequeue(Packet);
			BackupFrontBuffer.Enqueue(Packet);
		}

		ClientFrontBufferSize = 0;
	
		// Add all the un-acked packets to the front buffer to be reused
		while (!BackBuffer.IsEmpty()) {
			InputPacket Packet;
			BackBuffer.Dequeue(Packet);

			if (Packet.PacketNumber > PacketNumber) {
				FrontBuffer.Enqueue(Packet);
				++ClientFrontBufferSize;
			}
		}

		// Add all the front buffer packets back into the queue
		while (!BackupFrontBuffer.IsEmpty()) {
			InputPacket Packet;
			BackupFrontBuffer.Dequeue(Packet);
		
			FrontBuffer.Enqueue(Packet);
			++ClientFrontBufferSize;
		}
	}
	
	void Ack(uint32 PacketNumber) {
		std::lock_guard<std::mutex> Lock(ClientMutex);
		while (!BackBuffer.IsEmpty()) {
			if (BackBuffer.Peek()->PacketNumber > PacketNumber) {
				break;
			}

			InputPacket PoppedPacket;
			BackBuffer.Dequeue(PoppedPacket);
		}
	}

	uint32 RemoteBufferSize() {
		std::lock_guard<std::mutex> Lock(ClientMutex);
		return ClientFrontBufferSize;
	}
	
	uint32 AuthorityBufferSize() {
		std::lock_guard<std::mutex> Lock(ServerMutex);
		return ServerBuffer.Num();
	}

	void QueueInputAuthority(const InputPacket& Packet) {
		std::lock_guard<std::mutex> Lock(ServerMutex);
		ServerBuffer.Add(Packet);

		// Sort the input buffer in case packets have come in out of order
		//
		// Pop() on a TArray takes the last element, so we want the input buffer to be sorted from greatest to least.
		// Then when we take the last element that will be the lowest number (ie next) input packet.
		ServerBuffer.Sort([](const InputPacket& First, const InputPacket& Second) {
			return First.PacketNumber > Second.PacketNumber;
		});
	}
	
	void QueueInputRemote(const InputPacket& Packet) {
		std::lock_guard<std::mutex> Lock(ClientMutex);
		FrontBuffer.Enqueue(Packet);
		++ClientFrontBufferSize;
	}
	
	bool ConsumeInputAuthority(InputPacket& OutPacket) {
		std::lock_guard<std::mutex> Lock(ServerMutex);
		// TODO Make this handle missing packets
		if (!ServerBuffer.Num()) {
			return false;
		}

		OutPacket = ServerBuffer.Pop();
		return true;
	}
	
	bool ConsumeInputRemote(InputPacket& OutPacket) {
		std::lock_guard<std::mutex> Lock(ClientMutex);
		if (FrontBuffer.IsEmpty()) {
			return false;
		}

		FrontBuffer.Dequeue(OutPacket);
		--ClientFrontBufferSize;
	
		BackBuffer.Enqueue(OutPacket);
		return true;
	}
	
private:

	/** On the client the queued inputs. When rewinding the back buffer is swapped into the front buffer. */
    TQueue<InputPacket> FrontBuffer;

	/** The inputs that have already been used on the client */
	TQueue<InputPacket> BackBuffer;

	/** The inputs from the server */
	TArray<InputPacket> ServerBuffer;

	uint32 ClientFrontBufferSize = 0;
	
	std::mutex ServerMutex;
	std::mutex ClientMutex;

};
	
