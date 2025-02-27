#pragma once

#include <vector>
#include <string>
#include <PxPhysicsAPI.h>


enum class StaminaEvent
{
	JUMP, MOVE
};

enum class Transformation
{
	HUMAN,
	CAT,
	SLIME
};

class GameState
{
public:
	physx::PxRigidActor*		playerActorPointer				= nullptr;
	int							maxPlayerStaminaAmount			= 100;
	float						currentPlayerStaminaAmount		= 100;
	float						playerStaminaOneServingAmount	= 10;
	float						playerStaminaDepleteChaser		= 0;		// NOTE: this shows how much the stamina is depleting, so it will essentially follow the currentPlayerStaminaAmount
	std::vector<std::string>	playerAllCollectedPuddleGUIDs;
	int							roomEnteringId;

	bool						isDayNightTimeMoving			= true;
	float						dayNightTimeSpeed				= (1.0f / (20.0f * 60.0f));	 // 20 minutes
	float						dayNightTime					= 0.0f;				// [0-1], and 1 is dusk

	Transformation				currentTransformation			= Transformation::HUMAN;  // Transformation::CAT;
	int							human_numJumps					= 1;	// NOTE: 1 is single jump, 2 is double jump (if fall off, then double jump only)
	

	static GameState& getInstance();

	void addPuddleGUID(const std::string& guid);		// @@@@@@TODO: The water system is changing, so no need for this guid-collected system!
	void removePuddleGUID(const std::string& guid);
	bool isPuddleCollected(const std::string& guid);

	void requestTriggerHold(physx::PxRigidActor* triggerActor);
	void requestTriggerRelease(physx::PxRigidActor* triggerActor);	// NOTE: this hsould remove the triggeractor from the stack... but it's not implemented yet
	physx::PxRigidActor* getCurrentTriggerHold();

	void inputStaminaEvent(StaminaEvent staminaEvent, float deltaTime = 1.0f);
	void updateStaminaDepletionChaser(float deltaTime = 1.0f);

	void updateDayNightTime(float deltaTime);

private:
	std::vector<physx::PxRigidActor*> currentHeldTriggerActorQueue;
	bool staminaWasDepletedThisFrame = false;
	float staminaDepletionSpeed = 0.0f, staminaDepletionAccel = 0.1f;
};
