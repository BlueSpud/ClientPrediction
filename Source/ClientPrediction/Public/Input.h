#pragma once

#include <mutex>

#include "PhysicsState.h"

#include "Input.generated.h"

USTRUCT()
struct CLIENTPREDICTION_API FInputPacket {
	
	GENERATED_BODY()

	FInputPacket() = default;
	FInputPacket(uint32 PacketNumber) {
		this->PacketNumber = PacketNumber;
	}
	
	/** 
	* Input frames have their own number independent of the frame number because they are not necissarily consumed in 
	* lockstep with the frames they're generated on due to latency. 
	*/
	uint32 PacketNumber = FPhysicsState::kInvalidFrame;

	/** Temporary test input */
	bool bIsApplyingForce = false;

	void NetSerialize(FArchive& Ar) {
		Ar << bIsApplyingForce;
		Ar << PacketNumber;
	}
};

template <typename InputPacket>
class FInputBuffer {

public:
	
	void Rewind(uint32 PacketNumber);
	void Ack(uint32 PacketNumber);

	uint32 RemoteBufferSize();
	uint32 AuthorityBufferSize();

	void QueueInputAuthority(const FInputPacket& Packet);
	void QueueInputRemote(const FInputPacket& Packet);
	
	bool ConsumeInputAuthority(FInputPacket& OutPacket);
	bool ConsumeInputRemote(FInputPacket& OutPacket);
	
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
	
