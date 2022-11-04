#pragma once

#include "Declares.h"

template <typename InputPacket>
class FAutoProxyInputBuffer {

public:

	void Rewind(uint32 PacketNumber) {

		// TQueue deletes the move / copy constructor so copy the existing front buffer
		TQueue<InputPacket> BackupFrontBuffer;
		while (!FrontBuffer.IsEmpty()) {
			InputPacket Packet;
			FrontBuffer.Dequeue(Packet);
			BackupFrontBuffer.Enqueue(Packet);
		}

		FrontBufferSize = 0;

		// Add all the un-acked packets to the front buffer to be reused
		while (!BackBuffer.IsEmpty()) {
			InputPacket Packet;
			BackBuffer.Dequeue(Packet);

			if (Packet.PacketNumber > PacketNumber) {
				FrontBuffer.Enqueue(Packet);
				++FrontBufferSize;
			}
		}

		// Add all the front buffer packets back into the queue
		while (!BackupFrontBuffer.IsEmpty()) {
			InputPacket Packet;
			BackupFrontBuffer.Dequeue(Packet);

			FrontBuffer.Enqueue(Packet);
			++FrontBufferSize;
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

	uint32 BufferSize() const {
		return FrontBufferSize;
	}

	void QueueInput(const InputPacket& Packet) {
		FrontBuffer.Enqueue(Packet);
		++FrontBufferSize;
	}

	bool ConsumeInput(InputPacket& OutPacket) {
		if (FrontBuffer.IsEmpty()) {
			return false;
		}

		FrontBuffer.Dequeue(OutPacket);
		--FrontBufferSize;

		BackBuffer.Enqueue(OutPacket);
		return true;
	}

private:

	/** The queued inputs. When rewinding the back buffer is swapped into the front buffer. */
	TQueue<InputPacket> FrontBuffer;
	uint32 FrontBufferSize = 0;

	/** The inputs that have already been used */
	TQueue<InputPacket> BackBuffer;

};

