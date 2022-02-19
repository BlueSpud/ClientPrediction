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

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) {
		Ar << bIsApplyingForce;
		Ar << PacketNumber;
		
		bOutSuccess = true;
		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FInputPacket> : public TStructOpsTypeTraitsBase2<FInputPacket>
{
	enum
	{
		WithNetSerializer = true
	};
};

class FInputBuffer {

public:
	
	void Rewind(uint32 PacketNumber);
	void Ack(uint32 PacketNumber);

	size_t ClientBufferSize();
	size_t ServerBufferSize();

	void QueueInputServer(const FInputPacket& Packet);
	void QueueInputClient(const FInputPacket& Packet);
	
	bool ConsumeInputServer(FInputPacket& OutPacket);
	bool ConsumeInputClient(FInputPacket& OutPacket);
	
private:

	/** On the client the queued inputs. When rewinding the back buffer is swapped into the front buffer. */
    TQueue<FInputPacket> FrontBuffer;

	/** The inputs that have already been used on the client */
	TQueue<FInputPacket> BackBuffer;

	/** The inputs from the server */
	TArray<FInputPacket> ServerBuffer;

	uint32 ClientFrontBufferSize = 0;
	
	std::mutex ServerMutex;
	std::mutex ClientMutex;

};