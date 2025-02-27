#include "Bone.h"

#include <iostream>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>


Bone::Bone(int id, const aiNodeAnim* channel) : id(id)
{
	numPositions = channel->mNumPositionKeys;
	for (unsigned int i = 0; i < numPositions; i++)
	{
		aiVector3D aiPosition = channel->mPositionKeys[i].mValue;
		float timeStamp = (float)channel->mPositionKeys[i].mTime;
		KeyPosition data;
		data.position = glm::vec3(aiPosition.x, aiPosition.y, aiPosition.z);
		data.timeStamp = timeStamp;
		positions.push_back(data);
	}

	numRotations = channel->mNumRotationKeys;
	for (unsigned int i = 0; i < numRotations; i++)
	{
		aiQuaternion aiOrientation = channel->mRotationKeys[i].mValue;
		float timeStamp = (float)channel->mRotationKeys[i].mTime;
		KeyRotation data;
		data.orientation = glm::quat(aiOrientation.w, aiOrientation.x, aiOrientation.y, aiOrientation.z);
		data.timeStamp = timeStamp;
		rotations.push_back(data);
	}

	numScales = channel->mNumScalingKeys;
	for (unsigned int i = 0; i < numScales; i++)
	{
		aiVector3D scale = channel->mScalingKeys[i].mValue;
		float timeStamp = (float)channel->mScalingKeys[i].mTime;
		KeyScale data;
		data.scale = glm::vec3(scale.x, scale.y, scale.z);
		data.timeStamp = timeStamp;
		scales.push_back(data);
	}
}


void Bone::update(float animationTime, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
{
	translation =	interpolatePosition(animationTime);
	rotation =		interpolateRotation(animationTime);
	scale =			interpolateScaling(animationTime);
}


int Bone::getPositionIndex(float animationTime)
{
	for (unsigned int i = 0; i < numPositions - 1; i++)
	{
		if (animationTime <= positions[i + 1].timeStamp)
			return i;
	}
	assert(0);
	return -1;
}


int Bone::getRotationIndex(float animationTime)
{
	for (unsigned int i = 0; i < numRotations - 1; i++)
	{
		if (animationTime <= rotations[i + 1].timeStamp)
			return i;
	}
	assert(0);
	return -1;
}


int Bone::getScaleIndex(float animationTime)
{
	for (unsigned int i = 0; i < numScales - 1; i++)
	{
		if (animationTime <= scales[i + 1].timeStamp)
			return i;
	}
	assert(0);
	return -1;
}

//
// @NOTE: Well hey, I don't know if this is the best/correct way to do this algorithm.
// So the way I'm about to do root motion is taking the first position keyframe
// and the last one and their animationTime and lerping the inbetween times based
// off these times. Instead of going from 0 to the whole animation's duration.
// I feel like since these two keyframes represent the "start" and "end" of the
// animations, that's what it should look like. PLEASE CORRECT ME IF WRONG.
//     -Timo
// 
// Well, I can't seem to really get it working. When the animation loops and the cycle
// repeats, there seems to be some popping that happens with the root motion. Looking
// at the original blender files, it seems like the root motion is doing the stuff correctly,
// so it's pretty unusual at the very least. I'll have to investigate into why this
// isn't working, bc IT'S PROBABLY AT THE CODING LEVEL EH!
//     -Timo
// 
// Okay, so here's what's up. Root motion requires that the keyframes interpolate
// linearly, so when I figured out that all the root bones for the running and walking animations
// for the player model were bezier interpolation, I switched it to linear and it fixed everything.
// MAKE SURE YOU APPLY THIS TO ALL MODELS THAT WILL USE ROOT MOTION!
//     -Timo
//
glm::vec3 Bone::INTERNALgetRootBoneXZDeltaPosition()
{
	if (numPositions <= 1)
		return glm::vec3(0.0f);		// @NOTE: bc there are no positions/just a single position, there is no root motion here, thus doing nothing.  -Timo

	size_t endIndex = (size_t)glm::max((int)0, (int)numPositions - 1);
	glm::vec3 deltaPosition = positions[endIndex].position - positions[0].position;
	deltaPosition.y = 0.0f;		// Just do XZ axes
	return deltaPosition;
}


void Bone::INTERNALmutateBoneWithXZDeltaPosition(const glm::vec3& xzDeltaPosition)
{
	size_t endIndex = (size_t)glm::max((int)0, (int)numPositions - 1);
	float startTime = positions[0].timeStamp;
	float endTime = positions[endIndex].timeStamp;

	// Affect this bone
	for (size_t i = 0; i < positions.size(); i++)
	{
		float timeStampNormalized = (positions[i].timeStamp - startTime) / (endTime - startTime);
		positions[i].position -= xzDeltaPosition * timeStampNormalized;
	}
}


//
// -------------------- Private functions --------------------
//
float Bone::getScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime)
{
	float scaleFactor = 0.0f;
	float midWayLength = animationTime - lastTimeStamp;
	float framesDiff = nextTimeStamp - lastTimeStamp;
	scaleFactor = midWayLength / framesDiff;
	return scaleFactor;
}


glm::vec3 Bone::interpolatePosition(float animationTime)
{
	if (1 == numPositions)
		//return glm::translate(glm::mat4(1.0f), positions[0].position);
		return positions[0].position;

	int p0Index = getPositionIndex(animationTime);
	int p1Index = p0Index + 1;
	float scaleFactor = getScaleFactor(positions[p0Index].timeStamp, positions[p1Index].timeStamp, animationTime);
	glm::vec3 finalPosition = glm::mix(positions[p0Index].position, positions[p1Index].position, scaleFactor);
	//return glm::translate(glm::mat4(1.0f), finalPosition);
	return finalPosition;
}


glm::quat Bone::interpolateRotation(float animationTime)
{
	if (1 == numRotations)
	{
		auto rotation = glm::normalize(rotations[0].orientation);
		//return glm::toMat4(rotation);
		return rotation;
	}

	int p0Index = getRotationIndex(animationTime);
	int p1Index = p0Index + 1;
	float scaleFactor = getScaleFactor(rotations[p0Index].timeStamp, rotations[p1Index].timeStamp, animationTime);
	glm::quat finalRotation = glm::slerp(rotations[p0Index].orientation, rotations[p1Index].orientation, scaleFactor);
	//return glm::toMat4(glm::normalize(finalRotation));
	return finalRotation;
}


glm::vec3 Bone::interpolateScaling(float animationTime)
{
	if (1 == numScales)
		//return glm::scale(glm::mat4(1.0f), scales[0].scale);
		return scales[0].scale;

	int p0Index = getScaleIndex(animationTime);
	int p1Index = p0Index + 1;
	float scaleFactor = getScaleFactor(scales[p0Index].timeStamp, scales[p1Index].timeStamp, animationTime);
	glm::vec3 finalScale = glm::mix(scales[p0Index].scale, scales[p1Index].scale, scaleFactor);
	//return glm::scale(glm::mat4(1.0f), finalScale);
	return finalScale;
}
