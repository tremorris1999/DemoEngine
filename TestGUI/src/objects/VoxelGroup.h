#pragma once

#include <PxPhysicsAPI.h>
#include "BaseObject.h"
#include "../render_engine/model/animation/Animator.h"


typedef unsigned int GLuint;
class Spline;
class Material;

enum class MOVING_PLATFORM_MODE
{
	LOOP, STOP_AT_END, PING_PONG
};

class VoxelGroup : public BaseObject
{
public:
	static const std::string TYPE_NAME;

	VoxelGroup();
	~VoxelGroup();

	void loadPropertiesFromJson(nlohmann::json& object);
	nlohmann::json savePropertiesToJson();

	void preRenderUpdate();
	void physicsUpdate();

	PhysicsComponent* physicsComponent = nullptr;
	RenderComponent* renderComponent = nullptr;

	LightComponent* getLightComponent() { return nullptr; }
	PhysicsComponent* getPhysicsComponent() { return physicsComponent; }
	RenderComponent* getRenderComponent() { return renderComponent; }

	void INTERNALrecreatePhysicsComponent();

#ifdef _DEVELOP
	void imguiPropertyPanel();
	void imguiRender();
#endif


	// TODO: This needs to stop (having a bad loading function) bc this function should be private, but it's not. Kuso.
	void refreshResources();

private:
	//
	// Some voxel props
	//
	glm::vec3 voxel_group_color;
	glm::vec3 temp_voxel_group_color;

	float voxel_render_size = 4.0f;
	float temp_voxel_render_size;
	glm::i64vec3 voxel_group_size;
	glm::i64vec3 voxel_group_offset;
	std::vector<std::vector<std::vector<bool>>> voxel_bit_field;
	bool is_voxel_bit_field_dirty = false;

	void resizeVoxelArea(glm::i64vec3 size, glm::i64vec3 offset = glm::i64vec3(0));
	void setVoxelBitAtPosition(glm::i64vec3 pos, bool flag);
	bool getVoxelBitAtPosition(glm::i64vec3 pos);
	void updateQuadMeshFromBitField();
	Model* voxel_model = nullptr;
	std::map<std::string, Material*> materials;

	struct ImguiRenderVariables
	{
		bool show_render;
		int voxel_edit_mode;
		glm::vec3 cursor_pos_projected;
		glm::vec3 rayhit_normal_raw;
		glm::vec3 rayhit_normal_cooked;
		glm::i64vec3 select_from_pos;
		glm::i64vec3 select_to_pos;
	};
	ImguiRenderVariables irv;

	//
	// Moving platform props
	//
	bool rigidbodyIsDynamic;	// INTERNAL
	physx::PxVec3 velocity = physx::PxVec3(0.0f),
		angularVelocity = physx::PxVec3(0.0f);

	std::string assignedSplineGUID = "";
	float splineSpeed;
	float currentSplinePosition;
	bool movingPlatformMoveBackwards;
	MOVING_PLATFORM_MODE splineMovementMode;
	float pingPongHoldTime;
	float pingPongHoldTimer;	// INTERNAL
};
