#include "PointLight.h"

#include <glad/glad.h>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../mainloop/MainLoop.h"
#include "../render_engine/material/Texture.h"
#include "../render_engine/material/Shader.h"
#include "../render_engine/resources/Resources.h"
#include "../render_engine/render_manager/RenderManager.h"

#ifdef _DEVELOP
#include "../imgui/imgui.h"
#include "../imgui/imgui_stdlib.h"
#include "../imgui/ImGuizmo.h"
#endif

#include "../utils/PhysicsUtils.h"


PointLight::PointLight(bool castsShadows)
{
	name = "Point Light";

	lightComponent = new PointLightLight(this, castsShadows);

	lightComponent->facingDirection = glm::vec3(0.0f);		// 0'd out facingdirection shows it's a point light in-shader

	refreshResources();
}

PointLight::~PointLight()
{
	delete lightComponent;
}

void PointLight::loadPropertiesFromJson(nlohmann::json& object)
{
	BaseObject::loadPropertiesFromJson(object["baseObject"]);
	lightComponent->loadPropertiesFromJson(object["lightComponent"]);

	//
	// I'll take the leftover tokens then
	//
	lightComponent->color = glm::vec3(object["color"][0], object["color"][1], object["color"][2]);
	lightComponent->colorIntensity = object["color_multiplier"];

	((PointLightLight*)lightComponent)->refreshShadowBuffers();
}

nlohmann::json PointLight::savePropertiesToJson()
{
	nlohmann::json j;
	j["type"] = TYPE_NAME;
	j["baseObject"] = BaseObject::savePropertiesToJson();
	j["lightComponent"] = lightComponent->savePropertiesToJson();

	j["color"] = { lightComponent->color.r, lightComponent->color.g, lightComponent->color.b };
	j["color_multiplier"] = lightComponent->colorIntensity;

	return j;
}

void PointLight::refreshResources()
{
	lightGizmoTextureId = ((Texture*)Resources::getResource("texture;lightIcon"))->getHandle();
}

PointLightLight::PointLightLight(BaseObject* bo, bool castsShadows) : LightComponent(bo, castsShadows)
{
	lightType = LightType::POINT;

	color = glm::vec3(1.0f);
	colorIntensity = 150.0f;

	refreshShadowBuffers();
}

PointLightLight::~PointLightLight()
{
	destroyShadowBuffers();
}

void PointLightLight::refreshShadowBuffers()
{
	constexpr float lightAttenuationThreshold = 0.025f;		// If the attenuation gets below here, then the light is done (also where the shadowmap ends)
	farPlane = glm::sqrt(colorIntensity / lightAttenuationThreshold);

	if (castsShadows)
		createShadowBuffers();
	else
		destroyShadowBuffers();
}

constexpr unsigned int depthMapResolution = 512;		// NOTE: this is x6 since we're doing a whole cube!
void PointLightLight::createShadowBuffers()
{
	if (shadowMapsCreated) return;

	refreshResources();

	glGenFramebuffers(1, &lightFBO);

	glGenTextures(1, &shadowMapTexture);
	glBindTexture(GL_TEXTURE_CUBE_MAP, shadowMapTexture);
	for (unsigned int i = 0; i < 6; i++)
	{
		glTexImage2D(
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
			0,
			GL_DEPTH_COMPONENT,
			depthMapResolution,
			depthMapResolution,
			0,
			GL_DEPTH_COMPONENT,
			GL_FLOAT,
			nullptr
		);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	// Framebuffer biz
	glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowMapTexture, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);

	int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
		throw 0;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Set flag
	shadowMapsCreated = true;
}

void PointLightLight::destroyShadowBuffers()
{
	if (!shadowMapsCreated) return;

	glDeleteFramebuffers(1, &lightFBO);
	glDeleteTextures(1, &shadowMapTexture);

	shadowMapsCreated = false;
}

void PointLightLight::configureShaderAndMatrices()
{
	glm::mat4 shadowProjection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

	glm::vec3 position = PhysicsUtils::getPosition(baseObject->getTransform());

	shadowTransforms.clear();
	shadowTransforms.push_back(shadowProjection * glm::lookAt(position, position + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
	shadowTransforms.push_back(shadowProjection * glm::lookAt(position, position + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
	shadowTransforms.push_back(shadowProjection * glm::lookAt(position, position + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)));
	shadowTransforms.push_back(shadowProjection * glm::lookAt(position, position + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)));
	shadowTransforms.push_back(shadowProjection * glm::lookAt(position, position + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)));
	shadowTransforms.push_back(shadowProjection * glm::lookAt(position, position + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0)));

	for (size_t i = 0; i < 6; i++)
		pointLightShadowShader->setMat4("shadowMatrices[" + std::to_string(i) + "]", shadowTransforms[i]);
	pointLightShadowShader->setFloat("farPlane", farPlane);
	pointLightShadowShader->setVec3("lightPosition", position);
}

void PointLightLight::renderPassShadowMap()
{
#ifdef _DEVELOP
	refreshResources();
#endif

	pointLightShadowShader->use();

	// Render depth of scene
	glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);

	glViewport(0, 0, depthMapResolution, depthMapResolution);
	glClear(GL_DEPTH_BUFFER_BIT);
	//glCullFace(GL_FRONT);  // peter panning

	configureShaderAndMatrices();
	MainLoop::getInstance().renderManager->renderSceneShadowPass(pointLightShadowShader);

	//glCullFace(GL_BACK);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PointLightLight::refreshResources()
{
	pointLightShadowShader = (Shader*)Resources::getResource("shader;pointLightShadowPass");
}

#ifdef _DEVELOP
bool PointLight::showLightVolumes = false;

void PointLight::imguiPropertyPanel()
{
	ImGui::ColorEdit3("Light base color", &lightComponent->color[0], ImGuiColorEditFlags_DisplayRGB);

	static float cachedColorIntensity = lightComponent->colorIntensity;
	ImGui::DragFloat("Light color multiplier", &lightComponent->colorIntensity);
	if (cachedColorIntensity != lightComponent->colorIntensity)
	{
		cachedColorIntensity = lightComponent->colorIntensity;
		((PointLightLight*)lightComponent)->refreshShadowBuffers();
	}

	// Show light volumes
	ImGui::Checkbox("Show light volumes (Global)", &showLightVolumes);

	//
	// Toggle shadows
	//
	std::string toggleShadowsLabel = "Turn " + std::string(lightComponent->castsShadows ? "Off" : "On") + " Shadows";
	if (ImGui::Button(toggleShadowsLabel.c_str()))
	{
		lightComponent->castsShadows = !lightComponent->castsShadows;
		((PointLightLight*)lightComponent)->refreshShadowBuffers();
	}
}

void PointLight::imguiRender()
{
	refreshResources();

	//
	// Draw Light position			(TODO: This needs to get extracted into its own function)
	//
	float gizmoSize1to1 = 30.0f;
	glm::vec3 lightPosOnScreen = MainLoop::getInstance().camera.PositionToClipSpace(PhysicsUtils::getPosition(getTransform()));
	float clipZ = lightPosOnScreen.z;


	float gizmoRadius = gizmoSize1to1;
	{
		float a = 0.0f;
		float b = gizmoSize1to1;

		float defaultHeight = tanf(glm::radians(MainLoop::getInstance().camera.fov)) * clipZ;
		float t = gizmoSize1to1 / defaultHeight;
		//std::cout << t << std::endl;
		gizmoRadius = t * (b - a) + a;
	}

	if (clipZ > 0.0f)
	{
		lightPosOnScreen /= clipZ;
		lightPosOnScreen.x = ImGui::GetWindowPos().x + lightPosOnScreen.x * MainLoop::getInstance().camera.width / 2 + MainLoop::getInstance().camera.width / 2;
		lightPosOnScreen.y = ImGui::GetWindowPos().y - lightPosOnScreen.y * MainLoop::getInstance().camera.height / 2 + MainLoop::getInstance().camera.height / 2;

		//std::cout << lightPosOnScreen.x << ", " << lightPosOnScreen.y << ", " << lightPosOnScreen.z << std::endl;
		ImVec2 p_min = ImVec2(lightPosOnScreen.x - gizmoRadius, lightPosOnScreen.y + gizmoRadius);
		ImVec2 p_max = ImVec2(lightPosOnScreen.x + gizmoRadius, lightPosOnScreen.y - gizmoRadius);

		ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)lightGizmoTextureId, p_min, p_max);
	}

	if (showLightVolumes)
		PhysicsUtils::imguiRenderSphereCollider(getTransform(), ((PointLightLight*)getLightComponent())->farPlane);
}
#endif
