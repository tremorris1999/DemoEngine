#include "VoxelGroup.h"

#include <sstream>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/scalar_multiplication.hpp>
#include <glm/gtx/quaternion.hpp>
#include "components/PhysicsComponents.h"
#include "../mainloop/MainLoop.h"
#include "../render_engine/model/Model.h"
#include "../render_engine/material/Material.h"
#include "../render_engine/resources/Resources.h"
#include "../utils/PhysicsUtils.h"
#include "Spline.h"

#ifdef _DEVELOP
#include "../render_engine/render_manager/RenderManager.h"

#include "../imgui/imgui.h"
#include "../imgui/imgui_stdlib.h"
#endif


VoxelGroup::VoxelGroup()
{
	name = "Voxel Group (unnamed)";
	temp_voxel_render_size = voxel_render_size;
	voxel_group_color = glm::vec3(1);
	temp_voxel_group_color = voxel_group_color;
	velocity = physx::PxVec3(0.0f);
	angularVelocity = physx::PxVec3(0.0f);
	rigidbodyIsDynamic = false;
	assignedSplineGUID = "";
	movingPlatformMoveBackwards = false;
	pingPongHoldTime = 0.0f;

	// @TODO: small rant: I need some way of not having to create the voxels and then right after load in the voxel data from loadPropertiesFromJson() !!! It's dumb to do it twice.
	resizeVoxelArea(glm::i64vec3(1));												// For now default. (Needs loading system to do different branch)
	setVoxelBitAtPosition(glm::i64vec3(0), true);									// Default: center cube is enabled

	renderComponent = new RenderComponent(this);
	refreshResources();
}

VoxelGroup::~VoxelGroup()
{
	delete renderComponent;
	delete physicsComponent;
}


struct VoxelSaveLoadGroup
{
	int numBits;
	char currentBit;
};

void VoxelGroup::loadPropertiesFromJson(nlohmann::json& object)
{
	// NOTE: "Type" is taken care of not here, but at the very beginning when the object is getting created.
	BaseObject::loadPropertiesFromJson(object["baseObject"]);

	//
	// Prepare bitfield sizing
	//
	{
		voxel_render_size = 2.0f;			// @NOTE: Unfortunately, this is simply the deprecated size. The default size should've been 4.0f (what it's set at now) but that's just how it is I guess.  -Timo
		if (object.contains("voxel_render_size"))
			voxel_render_size = object["voxel_render_size"];

		temp_voxel_render_size = voxel_render_size;

		if (object.contains("voxel_group_color"))
		{
			voxel_group_color.r = object["voxel_group_color"][0];
			voxel_group_color.g = object["voxel_group_color"][1];
			voxel_group_color.b = object["voxel_group_color"][2];
			temp_voxel_group_color = voxel_group_color;
		}

		glm::i64vec3 voxelGroupSize = glm::i64vec3(32);
		if (object.contains("voxel_group_size"))
			voxelGroupSize = glm::i64vec3(object["voxel_group_size"][0], object["voxel_group_size"][1], object["voxel_group_size"][2]);

		glm::i64vec3 voxelGroupOffset = voxelGroupSize / (glm::i64)2;
		if (object.contains("voxel_group_offset"))
			voxelGroupOffset = glm::i64vec3(object["voxel_group_offset"][0], object["voxel_group_offset"][1], object["voxel_group_offset"][2]);

		resizeVoxelArea(voxelGroupSize, voxelGroupOffset);
	}

	//
	// Load actual bitfield
	//
	if (object.contains("voxel_bit_field"))
	{
		std::string bigBitfield = object["voxel_bit_field"];

		// Parse out all the VoxelSaveLoadGroups
		std::vector<VoxelSaveLoadGroup> vslgs;
		{
			std::stringstream bbfStream(bigBitfield);
			std::string seg;
			while (std::getline(bbfStream, seg, ';'))
			{
				size_t colonPos = seg.find(':');
				const std::string str_numBits = seg.substr(0, colonPos);
				const std::string str_currentBit = seg.substr(colonPos + 1);

				VoxelSaveLoadGroup vslg;
				vslg.numBits = std::stoi(str_numBits);
				vslg.currentBit = str_currentBit.c_str()[0];
				vslgs.push_back(vslg);
			}
		}
		
		// Fill in the voxels
		size_t currentVSLGroup = 0;
		VoxelSaveLoadGroup vslg;
		vslg.numBits = vslgs[0].numBits;
		vslg.currentBit = vslgs[0].currentBit;

		for (size_t i = 0; i < (size_t)voxel_group_size.x; i++)
		{
			for (size_t j = 0; j < (size_t)voxel_group_size.y; j++)
			{
				for (size_t k = 0; k < (size_t)voxel_group_size.z; k++)
				{
					setVoxelBitAtPosition(
						{ i, j, k },
						vslg.currentBit != '0'
					);

					// See if should move to next VSL group
					vslg.numBits--;
					if (vslg.numBits == 0 && currentVSLGroup + 1 < vslgs.size())		// NOTE: the second part isn't really needed, however, that's more of a safety precaution in the case that the data is weird or something. @AMEND: ok, apparently it IS needed, bc the very last voxel will run, finishing the whole set, and since the last set is finished, it's gonna wanna run this block to increment the group index, so we should prevent that from happening.
					{
						// Move to next group
						currentVSLGroup++;
						vslg.numBits = vslgs[currentVSLGroup].numBits;
						vslg.currentBit = vslgs[currentVSLGroup].currentBit;
					}
				}
			}
		}
	}

	//
	// Load Moving Platform Properties
	//
	if (object.contains("moving_platform_velocity"))
		velocity = physx::PxVec3(object["moving_platform_velocity"][0], object["moving_platform_velocity"][1], object["moving_platform_velocity"][2]);
	if (object.contains("moving_platform_angular_velocity"))
		angularVelocity = physx::PxVec3(object["moving_platform_angular_velocity"][0], object["moving_platform_angular_velocity"][1], object["moving_platform_angular_velocity"][2]);
	if (object.contains("moving_platform_spline_guid"))
		assignedSplineGUID = object["moving_platform_spline_guid"];
	if (object.contains("moving_platform_spline_speed"))
		splineSpeed = object["moving_platform_spline_speed"];
	if (object.contains("moving_platform_spline_position_in_time"))
		currentSplinePosition = object["moving_platform_spline_position_in_time"];
	if (object.contains("moving_platform_spline_move_backwards"))
		movingPlatformMoveBackwards = object["moving_platform_spline_move_backwards"];
	if (object.contains("moving_platform_spline_mvt_mode"))
		splineMovementMode = (MOVING_PLATFORM_MODE)object["moving_platform_spline_mvt_mode"];
	if (object.contains("moving_platform_spline_pp_hold_time"))
		pingPongHoldTime = object["moving_platform_spline_pp_hold_time"];

	// TODO: make it so that you don't have to retrigger this every time you load
	refreshResources();
}

nlohmann::json VoxelGroup::savePropertiesToJson()
{
	nlohmann::json j;
	j["type"] = TYPE_NAME;
	j["baseObject"] = BaseObject::savePropertiesToJson();

	//
	// Save bitfield sizing
	//
	j["voxel_render_size"] = voxel_render_size;
	j["voxel_group_color"] = { voxel_group_color.r, voxel_group_color.g, voxel_group_color.b };
	j["voxel_group_size"] = { voxel_group_size.x, voxel_group_size.y, voxel_group_size.z };
	j["voxel_group_offset"] = { voxel_group_offset.x, voxel_group_offset.y, voxel_group_offset.z };

	//
	// Save bitfield
	//
	std::string bigBitfield = "";

	VoxelSaveLoadGroup vslg;
	vslg.currentBit = 'S';
	vslg.numBits = 0;
	
	for (size_t i = 0; i < (size_t)voxel_group_size.x; i++)
	{
		for (size_t j = 0; j < (size_t)voxel_group_size.y; j++)
		{
			for (size_t k = 0; k < (size_t)voxel_group_size.z; k++)
			{
				//
				// Change bit if different from currentBit
				//
				char sampledBit = getVoxelBitAtPosition({ i, j, k }) ? '1' : '0';
				const bool isLast = (i == (size_t)(voxel_group_size.x - 1) && j == (size_t)(voxel_group_size.y - 1) && k == (size_t)(voxel_group_size.z - 1));
				if (vslg.currentBit != sampledBit)
				{
					if (vslg.currentBit != 'S')
					{
						// Write the amount of previous bits there were before we switch to new bit!
						bigBitfield += std::to_string(vslg.numBits) + ":" + vslg.currentBit + ";";
					}

					// Reset number of bits!
					vslg.currentBit = sampledBit;
					vslg.numBits = 0;
				}
				vslg.numBits++;

				if (isLast)
				{
					// Final write
					bigBitfield += std::to_string(vslg.numBits) + ":" + vslg.currentBit + ";";
				}
			}
		}
	}
	j["voxel_bit_field"] = bigBitfield;

	//
	// Save Moving Platform Properties
	//
	j["moving_platform_velocity"] = { velocity.x, velocity.y, velocity.z };
	j["moving_platform_angular_velocity"] = { angularVelocity.x, angularVelocity.y, angularVelocity.z };
	j["moving_platform_spline_guid"] = assignedSplineGUID;
	j["moving_platform_spline_speed"] = splineSpeed;
	j["moving_platform_spline_position_in_time"] = currentSplinePosition;
	j["moving_platform_spline_move_backwards"] = movingPlatformMoveBackwards;
	j["moving_platform_spline_mvt_mode"] = (int)splineMovementMode;
	j["moving_platform_spline_pp_hold_time"] = pingPongHoldTime;

	return j;
}

void VoxelGroup::refreshResources()
{
	if (is_voxel_bit_field_dirty)
	{
		// Create quadmesh
		renderComponent->clearAllModels();
		updateQuadMeshFromBitField();

		if (voxel_model != nullptr)
		{
			//
			// Create cooked collision mesh
			//
			INTERNALrecreatePhysicsComponent();
			renderComponent->addModelToRender({ voxel_model, true, nullptr });
		}


		is_voxel_bit_field_dirty = false;
	}

	if (voxel_model != nullptr)
	{
		materials["Material"] = (Material*)Resources::getResource("material;pbrVoxelGroup");
		voxel_model->setMaterials(materials);
	}
}

void VoxelGroup::resizeVoxelArea(glm::i64vec3 size, glm::i64vec3 offset)
{
	//
	// Create new voxel_bit_field
	//
	std::vector<bool> zVoxels;
	zVoxels.resize(size.z, false);
	std::vector<std::vector<bool>> yzVoxels;
	yzVoxels.resize(size.y, zVoxels);
	std::vector<std::vector<std::vector<bool>>> xyzVoxels;
	xyzVoxels.resize(size.x, yzVoxels);

	// Fill it!
	if (voxel_bit_field.size() == 0)
	{
		// From scratch
		voxel_group_offset = size / (glm::i64)2;		// TODO: figure this out, bc this line breaks the voxel creation system (and probs the deletion system too)
		//voxel_group_offset = glm::i64vec3(0);
	}
	else
	{
		// 
		// (NOTE: this is an unsafe operation and if offset is incorrectly set, there could be some bad joojoo with out-of-bounds stuff)
		// I trust ya tho ;)
		//
		for (size_t i = 0; i < voxel_bit_field.size(); i++)
		{
			size_t offsetX = offset.x - voxel_group_offset.x;

			for (size_t j = 0; j < voxel_bit_field[i].size(); j++)
			{
				size_t offsetY = offset.y - voxel_group_offset.y;

				for (size_t k = 0; k < voxel_bit_field[i][j].size(); k++)
				{
					size_t offsetZ = offset.z - voxel_group_offset.z;

					xyzVoxels[offsetX + i][offsetY + j][offsetZ + k] = voxel_bit_field[i][j][k];
				}
			}
		}

		// With existing
		voxel_group_offset = offset;
	}

	// Apply the new voxels
	voxel_bit_field = xyzVoxels;

	// Update size
	voxel_group_size = size;
	is_voxel_bit_field_dirty = true;
}

void VoxelGroup::setVoxelBitAtPosition(glm::i64vec3 pos, bool flag)
{
	if (pos.x < 0 ||
		pos.y < 0 ||
		pos.z < 0 ||
		pos.x >= voxel_group_size.x ||
		pos.y >= voxel_group_size.y ||
		pos.z >= voxel_group_size.z)
		return;

	voxel_bit_field[pos.x][pos.y][pos.z] = flag;
	is_voxel_bit_field_dirty = true;
}

bool VoxelGroup::getVoxelBitAtPosition(glm::i64vec3 pos)
{
	if (pos.x < 0 ||
		pos.y < 0 ||
		pos.z < 0 ||
		pos.x >= voxel_group_size.x ||
		pos.y >= voxel_group_size.y ||
		pos.z >= voxel_group_size.z)
		return false;

	return voxel_bit_field[pos.x][pos.y][pos.z];
}

void VoxelGroup::updateQuadMeshFromBitField()
{
	if (voxel_model != nullptr)
	{
		delete voxel_model;
		voxel_model = nullptr;
	}

	std::vector<Vertex> quadMesh;
	Vertex vertBuilder;			// Let's call him "Bob" (https://pm1.narvii.com/6734/14e3582d0aa8180bd12d22772631e949ad4e6837v2_hq.jpg)

	//
	// Cycle thru each bit in the bitfield and see if meshes need to be rendered there
	//
	bool has_filled_bits = false;
	for (size_t i = 0; i < voxel_bit_field.size(); i++)
	{
		for (size_t j = 0; j < voxel_bit_field[i].size(); j++)
		{
			for (size_t k = 0; k < voxel_bit_field[i][j].size(); k++)
			{
				// Skip if empty
				if (!voxel_bit_field[i][j][k])
					continue;

				// Compute the origin position in the case that quads are added
				// NOTE: the origin is going to be the bottom-left-front
				glm::vec3 voxelOriginPosition = { i, j, k };
				voxelOriginPosition -= voxel_group_offset;
				voxelOriginPosition *= voxel_render_size;

				//
				// See if on the edge!
				// 
				// x-1 (normal: left)
				if (i == 0 || !voxel_bit_field[i - 1][j][k])
				{
					vertBuilder.position = glm::vec3(0, 0, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(0, 0, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(0, 1, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(0, 1, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
				}

				// x+1 (normal: right)
				if (i == voxel_bit_field.size() - 1 || !voxel_bit_field[i + 1][j][k])
				{
					vertBuilder.position = glm::vec3(1, 1, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 1, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 0, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 0, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
				}

				// y-1 (normal: down)
				if (j == 0 || !voxel_bit_field[i][j - 1][k])
				{
					vertBuilder.position = glm::vec3(0, 0, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(0, 0, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 0, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 0, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
				}

				// y+1 (normal: up)
				if (j == voxel_bit_field[i].size() - 1 || !voxel_bit_field[i][j + 1][k])
				{
					vertBuilder.position = glm::vec3(0, 1, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(0, 1, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 1, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 1, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
				}

				// z-1 (normal: backwards)
				if (k == 0 || !voxel_bit_field[i][j][k - 1])
				{
					vertBuilder.position = glm::vec3(0, 0, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(0, 1, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 1, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 0, 0) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
				}

				// z+1 (normal: forwards)
				if (k == voxel_bit_field[i][j].size() - 1 || !voxel_bit_field[i][j][k + 1])
				{
					vertBuilder.position = glm::vec3(0, 1, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(0, 0, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 0, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
					vertBuilder.position = glm::vec3(1, 1, 1) * voxel_render_size + voxelOriginPosition;
					quadMesh.push_back(vertBuilder);
				}
			}
		}
	}

	if (quadMesh.size() > 0)
	{
		voxel_model = new Model(quadMesh);

		std::vector<Mesh>& meshes = voxel_model->getRenderMeshes();
		nlohmann::json j;
		j["color"] = { voxel_group_color.r, voxel_group_color.g, voxel_group_color.b };
		for (size_t i = 0; i < meshes.size(); i++)
		{
			meshes[i].setMaterialInjections(j);
		}
	}
	else
		// DELETE IF NO VOXELS
		MainLoop::getInstance().deleteObject(this);
}

void VoxelGroup::physicsUpdate()
{
	const bool shouldBeDynamicRigidbody = !(velocity.isZero() && angularVelocity.isZero() && assignedSplineGUID.empty());
	if (rigidbodyIsDynamic != shouldBeDynamicRigidbody)
	{
		is_voxel_bit_field_dirty = true;
		return;
	}

	if (!shouldBeDynamicRigidbody)
		return;

	physx::PxRigidDynamic* body = (physx::PxRigidDynamic*)getPhysicsComponent()->getActor();
	physx::PxTransform trans = body->getGlobalPose();

	Spline* splineRef = nullptr;
	if (!assignedSplineGUID.empty() && (splineRef = Spline::getSplineFromGUID(assignedSplineGUID)) != nullptr)
	{
		trans.p = PhysicsUtils::toPxVec3(
			splineRef->getPositionFromLengthAlongPath(currentSplinePosition)
		);

		// Move along
		const float totalLengthOfPath = splineRef->getTotalLengthOfPath();
		currentSplinePosition += splineSpeed * MainLoop::getInstance().physicsDeltaTime * (movingPlatformMoveBackwards ? -1.0f : 1.0f);

		// Loop mode
		if (splineMovementMode == MOVING_PLATFORM_MODE::LOOP)
		{
			currentSplinePosition = glm::mod(currentSplinePosition, totalLengthOfPath);
		}
		// Stop at end mode
		else if (splineMovementMode == MOVING_PLATFORM_MODE::STOP_AT_END)
		{
			currentSplinePosition = glm::clamp(currentSplinePosition, 0.0f, totalLengthOfPath - 0.001f);
		}
		// Ping-pong mode
		else if (splineMovementMode == MOVING_PLATFORM_MODE::PING_PONG)
		{
			// Push up against the end and hold
			if (currentSplinePosition > totalLengthOfPath - 0.001f)
			{
				currentSplinePosition = totalLengthOfPath - 0.001f;
				pingPongHoldTimer += MainLoop::getInstance().physicsDeltaTime;
			}
			else if (currentSplinePosition < 0.0f)
			{
				currentSplinePosition = 0;
				pingPongHoldTimer += MainLoop::getInstance().physicsDeltaTime;
			}
			else
			{
				pingPongHoldTimer = 0.0f;
			}

			// End the holding once timer reaches the required time
			if (pingPongHoldTimer > pingPongHoldTime)
			{
				movingPlatformMoveBackwards = !movingPlatformMoveBackwards;
				pingPongHoldTimer = 0.0f;
			}
		}
	}
	else
		trans.p += velocity;		// Unfortunately, I think we need to turn off velocity if there's an assigned spline path

	trans.q *= PhysicsUtils::createQuatFromEulerDegrees(PhysicsUtils::toGLMVec3(angularVelocity));
	
	body->setKinematicTarget(trans);
	INTERNALsubmitPhysicsCalculation(PhysicsUtils::physxTransformToGlmMatrix(body->getGlobalPose()));
}

void VoxelGroup::INTERNALrecreatePhysicsComponent()
{
	if (physicsComponent != nullptr)
		delete physicsComponent;

	const bool shouldBeDynamicRigidbody = !(velocity.isZero() && angularVelocity.isZero() && assignedSplineGUID.empty());
	physicsComponent = new TriangleMeshCollider(this, { { voxel_model } }, shouldBeDynamicRigidbody ? RigidActorTypes::KINEMATIC : RigidActorTypes::STATIC);
	rigidbodyIsDynamic = shouldBeDynamicRigidbody;
	//setTransform(getTransform());		// NOTE: this is to prevent weird interpolation skipping
}


enum VOXEL_EDIT_MODES { NORMAL, APPENDING_VOXELS, REMOVING_VOXELS };

void VoxelGroup::preRenderUpdate()
{
#ifdef _DEVELOP
	// NOTE: turns out even though no physics simulation is happening during !playMode, you can still do raycasts. Nice
	if (MainLoop::getInstance().playMode)
		return;

	// If current selected object is me
	if (!MainLoop::getInstance().renderManager->isObjectSelected(guid))
		return;

	// MODE: 0: normal. 1: appending a bunch of voxels
	static int mode = NORMAL;
	static ViewPlane rawPlane;
	static glm::vec3 cookedNormal;
	static glm::i64vec3 fromPosition;
	static glm::i64vec3 toPosition;
	static int operationKeyBinding;		// Either GLFW_KEY_X for delete or GLFW_KEY_C for create

	// Do the WS position camera vector.
	Camera& camera = MainLoop::getInstance().camera;

	double xpos, ypos;
	glfwGetCursorPos(MainLoop::getInstance().window, &xpos, &ypos);
	xpos /= MainLoop::getInstance().camera.width;
	ypos /= MainLoop::getInstance().camera.height;
	ypos = 1.0 - ypos;
	xpos = xpos * 2.0f - 1.0f;
	ypos = ypos * 2.0f - 1.0f;
	glm::vec3 clipSpacePosition(xpos, ypos, 1.0f);
	const glm::vec3 worldSpacePosition = camera.clipSpacePositionToWordSpace(clipSpacePosition);

	if (mode == NORMAL)
	{
		// If press c (if held shift, then c is group append)
		bool yareAdd = false;
		static bool prevIsButtonHeld2 = GLFW_RELEASE;
		bool isButtonHeld2 = glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_C);
		if (prevIsButtonHeld2 == GLFW_RELEASE && isButtonHeld2 == GLFW_PRESS)
		{
			yareAdd = true;
		}
		prevIsButtonHeld2 = isButtonHeld2;

		// OR if press x (if held shift, then x is group remove)
		bool yareDelete = false;
		static bool prevIsButtonHeld1 = GLFW_RELEASE;
		bool isButtonHeld1 = glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_X);
		if (prevIsButtonHeld1 == GLFW_RELEASE && isButtonHeld1 == GLFW_PRESS)
		{
			yareDelete = true;
		}
		prevIsButtonHeld1 = isButtonHeld1;


		if (!yareDelete && !yareAdd)
			return;

		//
		// Check what position and angle it's clicked on! (@TODO: Figure out why not working)
		//
		physx::PxRaycastBuffer hitInfo;
		bool hit = PhysicsUtils::raycast(
			PhysicsUtils::toPxVec3(camera.position),
			PhysicsUtils::toPxVec3(glm::normalize(worldSpacePosition - camera.position)),
			500,
			hitInfo
		);

		if (!hit || !hitInfo.hasBlock || hitInfo.block.actor != getPhysicsComponent()->getActor())
			return;

		std::cout << "Jojoj" << std::endl;

		glm::vec3 positionRaw = glm::inverse(getTransform()) * glm::vec4(PhysicsUtils::toGLMVec3(hitInfo.block.position), 1.0f) / voxel_render_size;
		glm::vec3 normal = glm::round(glm::mat3(glm::transpose(getTransform())) * PhysicsUtils::toGLMVec3(hitInfo.block.normal));
		positionRaw -= glm::vec3(0.1f) * normal;		// NOTE: a slight offset occurs especially if rotated, so this is to do some padding for that
		glm::i64vec3 position = glm::i64vec3(glm::floor(positionRaw)) + voxel_group_offset;

		//
		// Edit the voxelgroup!
		//
		bool isShiftHeld =
			glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
			glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
		if (isShiftHeld && (yareAdd || yareDelete))
		{
			//
			// Take the normal and project the current mouse position to it
			//
			rawPlane.position = PhysicsUtils::toGLMVec3(hitInfo.block.position);
			rawPlane.normal = PhysicsUtils::toGLMVec3(hitInfo.block.normal);
			cookedNormal = normal;

			fromPosition = position;

			if (yareAdd)
				mode = APPENDING_VOXELS;
			else if (yareDelete)
				mode = REMOVING_VOXELS;
		}
		else if (yareAdd)
		{
			// Append one voxel at a time (If in range!)
			glm::i64vec3 additionVoxel = position + glm::i64vec3(normal);
			setVoxelBitAtPosition(additionVoxel, true);
		}
		else if (yareDelete)
		{
			// Delete voxel one at a time!
			setVoxelBitAtPosition(position, false);
		}
	}

	if (mode == APPENDING_VOXELS || mode == REMOVING_VOXELS)
	{
		bool isButtonHeld = false;
		if (mode == APPENDING_VOXELS)
			isButtonHeld = glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_C);
		else if (mode == REMOVING_VOXELS)
			isButtonHeld = glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_X);

		if (isButtonHeld)
		{
			//
			// Keep track of what the position of (cpu side ray-marching)
			//
			glm::vec3 prevRMPos = worldSpacePosition + glm::vec3(1.0f);		// Just to enter the while loop below
			glm::vec3 currRMPos = worldSpacePosition;
			const glm::vec3 targetRMPos = camera.position;		// NOTE: not the goal, but the direction that RM is gonna go

			size_t iterations = 0;
			const size_t max_iterations = 100;
			while (glm::distance(prevRMPos, currRMPos) > 0.01f && iterations < max_iterations)
			{
				prevRMPos = currRMPos;

				const float distance = glm::abs(rawPlane.getSignedDistance(prevRMPos));
				const glm::vec3 raymarchDelta = glm::normalize(targetRMPos - currRMPos) * distance;
				currRMPos += raymarchDelta;

				iterations++;
			}

			if (iterations < max_iterations)
			{
				// Convert hovering position to voxelposition
				glm::vec3 toPositionRaw = glm::inverse(getTransform()) * glm::vec4(currRMPos, 1.0f) / voxel_render_size;
				toPositionRaw -= glm::vec3(0.1f) * cookedNormal;		// NOTE: a slight offset occurs (especially if rotated (It's like 10^-8)), so this is to do some padding for that
				toPosition = glm::i64vec3(glm::floor(toPositionRaw)) + voxel_group_offset;

				irv.show_render = true;
				irv.voxel_edit_mode = mode;
				irv.cursor_pos_projected = currRMPos;
				irv.rayhit_normal_raw = rawPlane.normal;
				irv.rayhit_normal_cooked = cookedNormal;
				irv.select_to_pos = toPosition;
				irv.select_from_pos = fromPosition;
			}
		}
		else
		{
			glm::i64vec3 offset = mode == APPENDING_VOXELS && fromPosition - toPosition == glm::i64vec3(0) ? glm::i64vec3(cookedNormal) : glm::i64vec3(0);
			glm::i64vec3 startPlacementPosition = fromPosition + offset;
			glm::i64vec3 currentPlacementPosition = fromPosition + offset;
			glm::i64vec3 targetPlacementPosition = toPosition + offset;

			//
			// Check to see if need to resize
			//
			const glm::i64vec3 minIndex = glm::i64vec3(0);
			const glm::i64vec3 maxIndex = voxel_group_size - (glm::i64)1;
			glm::i64vec3 newMin = glm::min(minIndex, targetPlacementPosition);
			glm::i64vec3 newMax = glm::max(maxIndex, targetPlacementPosition);
			if (newMin != minIndex || newMax != maxIndex)
			{
				const glm::i64vec3 addToSize = glm::abs(newMin) + newMax - maxIndex;
				const glm::i64vec3 addToOffset = glm::abs(newMin);
				resizeVoxelArea(voxel_group_size + addToSize, voxel_group_offset + addToOffset);

				// Update the operation about to be done
				startPlacementPosition += addToOffset;
				currentPlacementPosition += addToOffset;
				targetPlacementPosition += addToOffset;
			}

			//
			// Fill in all that area specified while holding SHIFT+(C/X)
			//
			bool breakX, breakY, breakZ;
			for (currentPlacementPosition.x = startPlacementPosition.x, breakX = false; !breakX; currentPlacementPosition.x = PhysicsUtils::moveTowards(currentPlacementPosition.x, targetPlacementPosition.x, 1))
			{
				for (currentPlacementPosition.y = startPlacementPosition.y, breakY = false; !breakY; currentPlacementPosition.y = PhysicsUtils::moveTowards(currentPlacementPosition.y, targetPlacementPosition.y, 1))
				{
					for (currentPlacementPosition.z = startPlacementPosition.z, breakZ = false; !breakZ; currentPlacementPosition.z = PhysicsUtils::moveTowards(currentPlacementPosition.z, targetPlacementPosition.z, 1))
					{
						//
						// Fill in current spot, then move towards the next spot
						//
						setVoxelBitAtPosition(currentPlacementPosition, (mode == APPENDING_VOXELS));

						// Check if finished
						if (currentPlacementPosition.z == targetPlacementPosition.z)
						{
							breakZ = true;
						}
					}

					// Check if finished
					if (currentPlacementPosition.y == targetPlacementPosition.y)
					{
						breakY = true;
					}
				}

				// Check if finished
				if (currentPlacementPosition.x == targetPlacementPosition.x)
				{
					breakX = true;
				}
			}

			mode = NORMAL;
		}
	}
#endif
}

#ifdef _DEVELOP
void VoxelGroup::imguiPropertyPanel()
{
	ImGui::DragFloat("Voxel Render Size", &temp_voxel_render_size);
	if (temp_voxel_render_size != voxel_render_size &&
		ImGui::Button("Apply New Size"))
	{
		voxel_render_size = temp_voxel_render_size;
		is_voxel_bit_field_dirty = true;
	}

	ImGui::ColorEdit3("Voxel Group Color", &temp_voxel_group_color.x);
	if (temp_voxel_group_color != voxel_group_color &&
		ImGui::Button("Apply New Color"))
	{
		voxel_group_color = temp_voxel_group_color;
		is_voxel_bit_field_dirty = true;
	}

	ImGui::Separator();
	ImGui::Text("Moving Platform Props");

	ImGui::DragFloat3("Velocity", &velocity[0], 0.01f);
	ImGui::DragFloat3("Ang Velocity", &angularVelocity[0], 0.01f);

	// @TODO: make an object assigner... And this'd probably be what the interface would look like eh!
	if (ImGui::Button("Assign Spline.."))
		ImGui::OpenPopup("assign_spline_popup");
	if (ImGui::BeginPopup("assign_spline_popup"))
	{
		if (ImGui::Selectable(("<None>##" + guid).c_str(), assignedSplineGUID.empty()))
			assignedSplineGUID = "";

		for (size_t i = 0; i < Spline::m_all_splines.size(); i++)
		{
			if (ImGui::Selectable((Spline::m_all_splines[i]->name + " ::: " + Spline::m_all_splines[i]->guid).c_str(), assignedSplineGUID == Spline::m_all_splines[i]->guid))
				assignedSplineGUID = Spline::m_all_splines[i]->guid;		// NOTE: we may have a problem in the future where splines get deleted.
		}
		ImGui::EndPopup();
	}

	Spline* splineRef = nullptr;
	if (!assignedSplineGUID.empty() && (splineRef = Spline::getSplineFromGUID(assignedSplineGUID)) != nullptr)
	{
		ImGui::Text(("Spline Length: " + std::to_string(splineRef->getTotalLengthOfPath())).c_str());

		ImGui::DragFloat("Spline Speed", &splineSpeed);

		if (ImGui::DragFloat("Current Spline Position", &currentSplinePosition, 1.0f, 0.0f, splineRef->getTotalLengthOfPath() - 0.001f))
		{
			glm::vec3 newPos = splineRef->getPositionFromLengthAlongPath(currentSplinePosition);
			setTransform(glm::translate(glm::mat4(1.0f), newPos) * glm::toMat4(PhysicsUtils::getRotation(getTransform())) * glm::scale(glm::mat4(1.0f), PhysicsUtils::getScale(getTransform())));
		}

		ImGui::Checkbox("Spline Move Thru Backwards", &movingPlatformMoveBackwards);

		int splineMvtModeAsInt = (int)splineMovementMode;
		ImGui::Combo("Moving platform mvt mode", &splineMvtModeAsInt, "Loop\0Stop at End\0Ping pong");
		splineMovementMode = (MOVING_PLATFORM_MODE)splineMvtModeAsInt;

		if (splineMovementMode == MOVING_PLATFORM_MODE::PING_PONG)
			ImGui::DragFloat("Ping Pong Hold Time", &pingPongHoldTime);
	}
}

void VoxelGroup::imguiRender()
{
	if (irv.show_render)
	{
		// Draw the bounds for the creation area of the voxel group
		const float halfSingleBlock = 0.5f * voxel_render_size;
		const glm::vec3 normalMask = glm::abs(irv.rayhit_normal_cooked);

		glm::vec3 deltaPositions(irv.select_to_pos - irv.select_from_pos);
		glm::vec3 halfExtents = (glm::abs(deltaPositions) + 1.0f) * halfSingleBlock;
		halfExtents *= 1.0f - normalMask;
		halfExtents = glm::max(glm::vec3(halfSingleBlock), halfExtents);
		physx::PxBoxGeometry geom(PhysicsUtils::toPxVec3(halfExtents));

		const glm::vec3 offset = (glm::vec3(irv.select_from_pos - voxel_group_offset) + (irv.voxel_edit_mode == APPENDING_VOXELS && deltaPositions == glm::vec3(0) ? irv.rayhit_normal_cooked : glm::vec3(0.0f))) * voxel_render_size + (deltaPositions + 1.0f) * halfSingleBlock;

		glm::vec3 position = PhysicsUtils::getPosition(getTransform());
		glm::quat rotation = PhysicsUtils::getRotation(getTransform());
		glm::vec3 scale = PhysicsUtils::getScale(getTransform());
		PhysicsUtils::imguiRenderBoxCollider(
			glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation) * glm::translate(glm::mat4(1.0f), offset) * glm::scale(glm::mat4(1.0f), scale),
			geom,
			(irv.voxel_edit_mode == APPENDING_VOXELS ? ImColor(0.78f, 0.243f, 0.373f) : ImColor(0.196f, 0.078f, 0.706f))
		);

		// Draw the guiding circle for appending with SHIFT+C
		PhysicsUtils::imguiRenderCircle(glm::translate(glm::mat4(1.0f), irv.cursor_pos_projected), 0.5f, glm::eulerAngles(glm::quat({ 0, 0, 1 }, irv.rayhit_normal_raw)), glm::vec3(), 10);
		irv.show_render = false;
	}
}
#endif
