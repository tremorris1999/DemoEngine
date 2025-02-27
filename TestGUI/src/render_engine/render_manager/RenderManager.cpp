#include "RenderManager.h"

#include "../../mainloop/MainLoop.h"

#include <string>
#include <cmath>
#include <random>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/scalar_multiplication.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <filesystem>
#include <stb/stb_image_write.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef _DEVELOP
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_stdlib.h"
#include "../../imgui/imgui_impl_glfw.h"
#include "../../imgui/imgui_impl_opengl3.h"
#include "../../imgui/ImGuizmo.h"
#include "../../imgui/GraphEditor.h"
#endif

#include "../material/Texture.h"
#include "../material/Shader.h"
#include "../material/shaderext/ShaderExtCloud_effect.h"
#include "../material/shaderext/ShaderExtCSM_shadow.h"
#include "../material/shaderext/ShaderExtPBR_daynight_cycle.h"
#include "../material/shaderext/ShaderExtShadow.h"
#include "../material/shaderext/ShaderExtSSAO.h"
#include "../material/shaderext/ShaderExtZBuffer.h"
#include "../material/Material.h"
#include "../model/Model.h"
#include "../resources/Resources.h"
#include "../../utils/FileLoading.h"
#include "../../utils/PhysicsUtils.h"
#include "../../utils/GameState.h"

#include <assimp/matrix4x4.h>

// Characters yo
#include "../../objects/PlayerCharacter.h"
#include "../../objects/YosemiteTerrain.h"
#include "../../objects/DirectionalLight.h"
#include "../../objects/PointLight.h"
#include "../../objects/WaterPuddle.h"
#include "../../objects/RiverDropoff.h"
#include "../../objects/VoxelGroup.h"
#include "../../objects/Spline.h"
#include "../../objects/GondolaPath.h"

#define CONTAINS_IN_VECTOR(v, key) (std::find(v.begin(), v.end(), key) != v.end())
#define REMOVE_FROM_VECTOR(v, elem) v.erase(std::remove(v.begin(), v.end(), elem), v.end())


void renderCube();
void renderQuad();

static bool showShadowMapView = false;
static bool showCloudNoiseView = false;
bool RenderManager::isWireFrameMode = false;
bool RenderManager::renderPhysicsDebug = false;
bool RenderManager::renderMeshRenderAABB = false;

#ifdef _DEVELOP
ImGuizmo::OPERATION transOperation;

std::string modelMetadataFname;
RenderComponent* modelForTimelineViewer = nullptr;
Animator* animatorForModelForTimelineViewer = nullptr;

template <typename T, std::size_t N>
struct Array
{
   T data[N];
   const size_t size() const { return N; }

   const T operator [] (size_t index) const { return data[index]; }
   operator T* () {
      T* p = new T[N];
      memcpy(p, data, sizeof(data));
      return p;
   }
};

template <typename T, typename ... U> Array(T, U...)->Array<T, 1 + sizeof...(U)>;
struct GraphEditorDelegate : public GraphEditor::Delegate
{
	bool AllowedLink(GraphEditor::NodeIndex from, GraphEditor::NodeIndex to) override
	{
		return true;
	}

	void SelectNode(GraphEditor::NodeIndex nodeIndex, bool selected) override
	{
		mNodes[nodeIndex].mSelected = selected;
	}

	void MoveSelectedNodes(const ImVec2 delta) override
	{
		for (auto& node : mNodes)
		{
			if (!node.mSelected)
			{
			continue;
			}
			node.x += delta.x;
			node.y += delta.y;
		}
	}

	virtual void RightClick(GraphEditor::NodeIndex nodeIndex, GraphEditor::SlotIndex slotIndexInput, GraphEditor::SlotIndex slotIndexOutput) override
	{
	}

	void AddLink(GraphEditor::NodeIndex inputNodeIndex, GraphEditor::SlotIndex inputSlotIndex, GraphEditor::NodeIndex outputNodeIndex, GraphEditor::SlotIndex outputSlotIndex) override
	{
		mLinks.push_back({ inputNodeIndex, inputSlotIndex, outputNodeIndex, outputSlotIndex });
	}

	void DelLink(GraphEditor::LinkIndex linkIndex) override
	{
		mLinks.erase(mLinks.begin() + linkIndex);
	}

	void CustomDraw(ImDrawList* drawList, ImRect rectangle, GraphEditor::NodeIndex nodeIndex) override
	{
		//drawList->AddLine(rectangle.Min, rectangle.Max, IM_COL32(0, 0, 0, 255));
		//drawList->AddText(rectangle.Min, IM_COL32(255, 128, 64, 255), "Draw");
	}

	const size_t GetTemplateCount() override
	{
		return sizeof(mTemplates) / sizeof(GraphEditor::Template);
	}

	const GraphEditor::Template GetTemplate(GraphEditor::TemplateIndex index) override
	{
		return mTemplates[index];
	}

	const size_t GetNodeCount() override
	{
		return mNodes.size();
	}

	const GraphEditor::Node GetNode(GraphEditor::NodeIndex index) override
	{
		const auto& myNode = mNodes[index];
		return GraphEditor::Node
		{
			myNode.name,
			myNode.templateIndex,
			ImRect(ImVec2(myNode.x, myNode.y), ImVec2(myNode.x + 200, myNode.y + 50)),
			myNode.mSelected
		};
	}

	const size_t GetLinkCount() override
	{
		return mLinks.size();
	}

	const GraphEditor::Link GetLink(GraphEditor::LinkIndex index) override
	{
		return mLinks[index];
	}

	// Graph datas
	static const inline GraphEditor::Template mTemplates[] = {
		{
			IM_COL32(160, 160, 180, 255),
			IM_COL32(100, 100, 140, 255),
			IM_COL32(110, 110, 150, 255),
			1,
			Array{"In"},
			nullptr,
			1,
			Array{"Out"},
			nullptr
		},
	};

	struct Node
	{
		const char* name;
		GraphEditor::TemplateIndex templateIndex;
		float x, y;
		bool mSelected;
	};

	std::vector<Node> mNodes;
	std::vector<GraphEditor::Link> mLinks;
	std::map<std::string, size_t> mNodeNameToIndex;
};

struct AnimationNameAndIncluded
{
	std::string name;
	bool included;
};
struct AnimationDetail
{
	bool trackXZRootMotion = false;
	float timestampSpeed = 1.0f;
};

// @NOTE: likely after this is constructed here, we're gonna have to move it to the Animator class so they can be the sole users of it.
struct ASMVariable
{
	std::string varName;
	float value;
	enum ASMVariableType
	{
		BOOL,
		INT,
		FLOAT
	} variableType;
};
struct ASMTransitionCondition
{
	std::string varName;
	float compareToValue;
	std::vector<std::string> specialCaseCurrentASMNodeNames;
	enum ASMComparisonOperator
	{
		EQUAL,
		NEQUAL,
		LESSER,
		GREATER,
		LEQUAL,
		GEQUAL
	} comparisonOperator;

	const static std::string specialCaseKey;
};
const std::string ASMTransitionCondition::specialCaseKey = "JASDKFHASKDGH#@$H@K!%K!H@#KH!@#K!$BFBNSDAFNANSDF  ";		// @NOTE: Man, I really hope nobody thinks to name their ASM variable this... it'd just be trollin' dango bango  -Timo
struct ASMTransitionConditionGroup
{
	std::vector<ASMTransitionCondition> transitionConditions;	// @NOTE: all these conditions must be true to transition. Also, it's to transition TO HERE. (aka from somewhere else) (aka ANY STATE -> HERE)  -Timo
};
struct ASMNode
{
	std::string nodeName;			// @NOTE: this is not used as the index. Use the index of animationStateMachineNodes;
	std::string animationName1;		// @NOTE: This should get saved as an index to the animation, not the name!
	std::string animationName2;		// @NOTE: This should get saved as an index to the animation, not the name!
	std::string varFloatBlend;		// The float var that should keep track of blending between animationName1 and animationName2
	float animationBlend1 = 0.0f;
	float animationBlend2 = 1.0f;
	bool loopAnimation = true;
	bool doNotTransitionUntilAnimationFinished = false;
	float transitionTime = 0.0f;

	std::vector<ASMTransitionConditionGroup> transitionConditionGroups;		// @NOTE: only at least one of these transition groups have to be true to transition. However, for a group to be true, each of the transition conditions must be true. It's like (x && y && z) || (a && b && c), where the groups are like the OR's and the actual transition conditions compare like the AND's  -Timo
};
struct TimelineViewerState
{
	std::map<std::string, std::string> materialPathsMap;
	std::vector<AnimationNameAndIncluded> animationNameAndIncluded;
	std::map<std::string, AnimationDetail> animationDetailMap;

	// Animation State Machine (ASM)
	std::vector<ASMVariable> animationStateMachineVariables;
	std::vector<ASMNode> animationStateMachineNodes;
	size_t animationStateMachineStartNode = 0;		// @NOTE: @TODO: This is currently unused... but you're gonna have to figure this out once the whole ASM machine is built (as in, the functionality is built in and you can use the TEST button)

	// Editor only values
	int editor_selectedAnimation = -1;
	int editor_currentlyPlayingAnimation = -1;
	AnimationDetail* editor_selectedAnimationPtr = nullptr;
	bool editor_isAnimationPlaying = false;
	float editor_animationPlaybackTimestamp = 0.0f;

	int editor_selectedASMNode = -1;
	bool editor_isASMPreviewMode = false;
	std::vector<ASMVariable> editor_asmVarCopyForPreview;
	size_t editor_previewModeCurrentASMNode;
	bool editor_ASMPreviewModeIsPaused = false;
	GraphEditorDelegate editor_ASMNodePreviewerDelegate;
	bool editor_flag_ASMNodePreviewerRunFitAllNodes = false;
} timelineViewerState;
#endif


RenderManager::RenderManager()
{
	createSkeletalAnimationUBO();
	createLightInformationUBO();
	createCameraInfoUBO();
	createShaderPrograms();
	createFonts();
	createHDRBuffer();
	createLumenAdaptationTextures();
	createCloudNoise();
	//loadResources();

#ifdef _DEVELOP
	createPickingBuffer();
#endif

	//
	// Create all the day/night cycle irradiance and prefilter maps
	// @REFACTOR: place this inside the GlobalLight's constructor and something for the destructor
	//
	bool firstSkyMap = true;
	for (size_t i = 0; i < numSkyMaps; i++)
	{
		createHDRSkybox(
			firstSkyMap,
			i,
			glm::vec3(
				cosf(glm::radians(preBakedSkyMapAngles[i])),
				-sinf(glm::radians(preBakedSkyMapAngles[i])),
				0.0f
			)
		);
		firstSkyMap = false;
	}
}

RenderManager::~RenderManager()
{
	destroySkeletalAnimationUBO();
	destroyLightInformationUBO();
	destroyCameraInfoUBO();
	destroyShaderPrograms();
	destroyFonts();
	destroyHDRBuffer();
	destroyLumenAdaptationTextures();
	destroyCloudNoise();
	//unloadResources();

#ifdef _DEVELOP
	destroyPickingBuffer();
#endif
}


const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
const glm::mat4 captureViews[] =
{
	glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
	glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
	glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
	glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
	glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
	glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
};


void RenderManager::createHDRSkybox(bool first, size_t index, const glm::vec3& sunOrientation)
{
	const int renderTextureSize = 512;

	//
	// Create the framebuffer and renderbuffer to capture the hdr skybox
	//
	if (first)
	{
		glGenFramebuffers(1, &hdrPBRGenCaptureFBO);
		glGenRenderbuffers(1, &hdrPBRGenCaptureRBO);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, hdrPBRGenCaptureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, hdrPBRGenCaptureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, renderTextureSize, renderTextureSize);

	if (first)
	{
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdrPBRGenCaptureRBO);
	}

	//
	// Create cubemap for the framebuffer and renderbuffer
	//
	if (first)
	{
		glGenTextures(1, &envCubemap);
		glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
		for (unsigned int i = 0; i < 6; ++i)
		{
			// note that we store each face with 16 bit floating point values
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				renderTextureSize, renderTextureSize, 0, GL_RGB, GL_FLOAT, nullptr);
		}
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	//
	// Render out the hdr skybox to the framebuffer
	//

	// Get the nightsky texture from disk
	Texture::setLoadSync(true);
	nightSkyboxCubemap = (Texture*)Resources::getResource("texture;nightSkybox");
	Texture::setLoadSync(false);

	// Convert HDR dynamic skybox to cubemap equivalent
	skybox_program_id->use();
	skybox_program_id->setVec3("mainCameraPosition", glm::vec3(0.0f));
	skybox_program_id->setVec3("sunOrientation", sunOrientation);
	skybox_program_id->setVec3("sunColor", skyboxParams.sunColor);
	skybox_program_id->setVec3("skyColor1", skyboxParams.skyColor1);
	skybox_program_id->setVec3("groundColor", skyboxParams.groundColor);
	skybox_program_id->setFloat("sunIntensity", skyboxParams.sunIntensity);
	skybox_program_id->setFloat("globalExposure", skyboxParams.globalExposure);
	skybox_program_id->setFloat("cloudHeight", skyboxParams.cloudHeight);
	skybox_program_id->setFloat("perlinDim", skyboxParams.perlinDim);
	skybox_program_id->setFloat("perlinTime", skyboxParams.perlinTime);
	skybox_program_id->setFloat("depthZFar", -1.0f);
	skybox_program_id->setMat4("projection", captureProjection);
	skybox_program_id->setInt("renderNight", true);
	skybox_program_id->setSampler("nightSkybox", nightSkyboxCubemap->getHandle());
	skybox_program_id->setMat3("nightSkyTransform", skyboxParams.nightSkyTransform);

	glViewport(0, 0, renderTextureSize, renderTextureSize); // don't forget to configure the viewport to the capture dimensions.
	glBindFramebuffer(GL_FRAMEBUFFER, hdrPBRGenCaptureFBO);
	for (unsigned int i = 0; i < 6; i++)
	{
		skybox_program_id->setMat4("view", captureViews[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		renderCube(); // renders a 1x1 cube in NDR
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	//
	// Create Irradiance Map
	//
	glGenTextures(1, &irradianceMap[index]);
	glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap[index]);
	for (unsigned int i = 0; i < 6; i++)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, irradianceMapSize, irradianceMapSize, 0, GL_RGB, GL_FLOAT, nullptr);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindFramebuffer(GL_FRAMEBUFFER, hdrPBRGenCaptureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, hdrPBRGenCaptureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, irradianceMapSize, irradianceMapSize);

	irradiance_program_id->use();
	irradiance_program_id->setMat4("projection", captureProjection);
	irradiance_program_id->setSampler("environmentMap", envCubemap);

	// Render out the irradiance map!
	glViewport(0, 0, irradianceMapSize, irradianceMapSize);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrPBRGenCaptureFBO);
	for (unsigned int i = 0; i < 6; i++)
	{
		irradiance_program_id->setMat4("view", captureViews[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap[index], 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		renderCube();
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	//
	// Create prefilter map for specular roughness
	//
	glGenTextures(1, &prefilterMap[index]);
	glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap[index]);
	for (unsigned int i = 0; i < 6; i++)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, prefilterMapSize, prefilterMapSize, 0, GL_RGB, GL_FLOAT, nullptr);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);		// Use mips to capture more "diffused" roughness

	//
	// Run Monte-carlo simulation on the environment lighting
	//
	prefilter_program_id->use();
	prefilter_program_id->setSampler("environmentMap", envCubemap);
	prefilter_program_id->setMat4("projection", captureProjection);

	glBindFramebuffer(GL_FRAMEBUFFER, hdrPBRGenCaptureFBO);
	for (unsigned int mip = 0; mip < maxMipLevels; mip++)
	{
		// Resize to mip level size
		unsigned int mipWidth = (unsigned int)(prefilterMapSize * std::pow(0.5, mip));
		unsigned int mipHeight = (unsigned int)(prefilterMapSize * std::pow(0.5, mip));
		glBindRenderbuffer(GL_RENDERBUFFER, hdrPBRGenCaptureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
		glViewport(0, 0, mipWidth, mipHeight);

		float roughness = (float)mip / (float)(maxMipLevels - 1);
		prefilter_program_id->setFloat("roughness", roughness);
		for (unsigned int i = 0; i < 6; i++)
		{
			prefilter_program_id->setMat4("view", captureViews[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap[index], mip);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			renderCube();
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!first)
		return;

	//
	// Create PBR BRDF LUT
	//
	const int brdfLUTSize = 512;

	//unsigned int brdfLUTTexture;
	glGenTextures(1, &brdfLUTTexture);

	glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, brdfLUTSize, brdfLUTSize, 0, GL_RG, GL_FLOAT, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Redo the render buffer to create the brdf texture
	glBindFramebuffer(GL_FRAMEBUFFER, hdrPBRGenCaptureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, hdrPBRGenCaptureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, brdfLUTSize, brdfLUTSize);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);

	glViewport(0, 0, brdfLUTSize, brdfLUTSize);
	brdf_program_id->use();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	renderQuad();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	ShaderExtPBR_daynight_cycle::brdfLUT = brdfLUTTexture;
}

void RenderManager::recreateRenderBuffers()
{
	destroyHDRBuffer();
	createHDRBuffer();
#ifdef _DEVELOP
	destroyPickingBuffer();
	createPickingBuffer();
#endif
}

#ifdef _DEVELOP
std::vector<BaseObject*> RenderManager::getSelectedObjects()
{
	std::vector<BaseObject*> objs;

	for (size_t i = 0; i < selectedObjectIndices.size(); i++)
	{
		size_t selectedIndex = selectedObjectIndices[i];
		if (selectedIndex >= MainLoop::getInstance().objects.size())
			continue;

		objs.push_back(MainLoop::getInstance().objects[selectedIndex]);
	}

	return objs;
}

bool RenderManager::isObjectSelected(size_t index)
{
	return (std::find(selectedObjectIndices.begin(), selectedObjectIndices.end(), index) != selectedObjectIndices.end());
}

bool RenderManager::isObjectSelected(const std::string& guid)
{
	// @NOTE: this is very inefficient, but it's only used in the voxelgroup class in the level editor, so yeah. It should be okay eh.
	for (size_t i = 0; i < selectedObjectIndices.size(); i++)
	{
		if (MainLoop::getInstance().objects[selectedObjectIndices[i]]->guid == guid)
			return true;
	}

	return false;
}

void RenderManager::addSelectObject(size_t index)
{
	if (!isObjectSelected(index))
		selectedObjectIndices.push_back(index);
}

void RenderManager::deselectObject(size_t index)
{
	auto it = std::find(selectedObjectIndices.begin(), selectedObjectIndices.end(), index);
	if (it != selectedObjectIndices.end())
	{
		selectedObjectIndices.erase(it);
	}
}

void RenderManager::deselectAllSelectedObject()
{
	selectedObjectIndices.clear();
}

void RenderManager::deleteAllSelectedObjects()
{
	std::vector<BaseObject*> objs = getSelectedObjects();
	for (size_t i = 0; i < objs.size(); i++)
	{
		delete objs[i];
	}
	deselectAllSelectedObject();
}
#endif

#ifdef _DEVELOP
void RenderManager::physxVisSetDebugLineList(std::vector<physx::PxDebugLine>* lineList)
{
	delete physxVisDebugLines;			// NOTE: this gets destroyed to prevent any memory leaks
	physxVisDebugLines = lineList;
}
#endif

void RenderManager::addTextRenderer(TextRenderer* textRenderer)
{
	textRQ.textRenderers.push_back(textRenderer);
}

void RenderManager::removeTextRenderer(TextRenderer* textRenderer)
{
	textRQ.textRenderers.erase(
		std::remove(
			textRQ.textRenderers.begin(),
			textRQ.textRenderers.end(),
			textRenderer
		),
		textRQ.textRenderers.end()
	);
}

void RenderManager::pushMessage(const std::string& text)
{
	notifHoldTimers.push_back(0);
	notifMessages.push_back(text);
}

void RenderManager::createHDRBuffer()
{
	//
	// Create HDR framebuffer
	//
	glCreateFramebuffers(1, &hdrFBO);
	// Create floating point color buffer
	glCreateTextures(GL_TEXTURE_2D, 1, &hdrColorBuffer);
	glTextureParameteri(hdrColorBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(hdrColorBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(hdrColorBuffer, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(hdrColorBuffer, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureStorage2D(hdrColorBuffer, 1, GL_RGBA16F, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
	//glTextureSubImage2D(hdrColorBuffer, 0, 0, 0, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height, GL_RGBA, GL_FLOAT, NULL);
	// Create depth buffer (renderbuffer)
	glGenRenderbuffers(1, &hdrDepthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, hdrDepthRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
	// Attach buffers
	glNamedFramebufferTexture(hdrFBO, GL_COLOR_ATTACHMENT0, hdrColorBuffer, 0);
	glNamedFramebufferRenderbuffer(hdrFBO, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdrDepthRBO);
	if (glCheckNamedFramebufferStatus(hdrFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (HDR Render Buffer)" << std::endl;

	//
	// Create HDR FXAA framebuffer
	// @NOTE: there is no depth buffer for this. Since it's just for postprocessing.
	//
	glCreateFramebuffers(1, &hdrFXAAFBO);
	// Create floating point color buffer
	glCreateTextures(GL_TEXTURE_2D, 1, &hdrFXAAColorBuffer);
	glTextureParameteri(hdrFXAAColorBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(hdrFXAAColorBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(hdrFXAAColorBuffer, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(hdrFXAAColorBuffer, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureStorage2D(hdrFXAAColorBuffer, 1, GL_RGBA16F, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
	// Attach buffers
	glNamedFramebufferTexture(hdrFXAAFBO, GL_COLOR_ATTACHMENT0, hdrFXAAColorBuffer, 0);
	if (glCheckNamedFramebufferStatus(hdrFXAAFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (HDR FXAA Render Buffer)" << std::endl;

	//
	// Create bloom pingpong buffers
	//
	glm::vec2 bufferDimensions(MainLoop::getInstance().camera.width, MainLoop::getInstance().camera.height);
	bufferDimensions /= 2.0f;		// NOTE: Bloom should start at 1/4 the size.

	glGenFramebuffers(bloomBufferCount, bloomFBOs);
	glGenTextures(bloomBufferCount, bloomColorBuffers);
	for (size_t i = 0; i < bloomBufferCount / 2; i++)
	{
		bufferDimensions /= 2.0f;
		for (size_t j = 0; j < 2; j++)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, bloomFBOs[i * 2 + j]);
			glBindTexture(GL_TEXTURE_2D, bloomColorBuffers[i * 2 + j]);
			glTexImage2D(		// @TODO: We don't need the alpha channel for this yo.
				GL_TEXTURE_2D, 0, GL_RGBA16F, (GLsizei)bufferDimensions.x, (GLsizei)bufferDimensions.y, 0, GL_RGBA, GL_FLOAT, NULL
			);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glFramebufferTexture2D(
				GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomColorBuffers[i * 2 + j], 0
			);
		}
	}

	//
	// Create Z-Prepass framebuffer
	//
	zPrePassDepthTexture =
		new Texture2D(
			(GLsizei)MainLoop::getInstance().camera.width,
			(GLsizei)MainLoop::getInstance().camera.height,
			1,
			GL_DEPTH_COMPONENT24,
			GL_DEPTH_COMPONENT,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	//ssNormalTexture =			@DEPRECATE
	//	new Texture2D(
	//		(GLsizei)MainLoop::getInstance().camera.width,
	//		(GLsizei)MainLoop::getInstance().camera.height,
	//		1,
	//		GL_RGB16F,
	//		GL_RGB,
	//		GL_FLOAT,
	//		nullptr,
	//		GL_NEAREST,
	//		GL_NEAREST,
	//		GL_CLAMP_TO_EDGE,
	//		GL_CLAMP_TO_EDGE
	//	);

	glCreateFramebuffers(1, &zPrePassFBO);
	//glNamedFramebufferTexture(zPrePassFBO, GL_COLOR_ATTACHMENT0, ssNormalTexture->getHandle(), 0);		@DEPRECATE
	glNamedFramebufferTexture(zPrePassFBO, GL_DEPTH_ATTACHMENT, zPrePassDepthTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(zPrePassFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Z-Prepass Framebuffer)" << std::endl;

	ShaderExtZBuffer::depthTexture = zPrePassDepthTexture->getHandle();

	//
	// Create Skybox framebuffer
	//
	skyboxLowResTexture = new Texture2D(skyboxLowResSize, skyboxLowResSize, 1, GL_RGB16F, GL_RGB, GL_FLOAT, nullptr, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	glCreateFramebuffers(1, &skyboxFBO);
	glNamedFramebufferTexture(skyboxFBO, GL_COLOR_ATTACHMENT0, skyboxLowResTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(skyboxFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Skybox Framebuffer)" << std::endl;

	skyboxLowResBlurTexture = new Texture2D(skyboxLowResSize, skyboxLowResSize, 1, GL_RGB16F, GL_RGB, GL_FLOAT, nullptr, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	glCreateFramebuffers(1, &skyboxBlurFBO);
	glNamedFramebufferTexture(skyboxBlurFBO, GL_COLOR_ATTACHMENT0, skyboxLowResBlurTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(skyboxBlurFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Skybox blur Framebuffer)" << std::endl;

	skyboxDepthSlicedLUT = new Texture3D(skyboxDepthSlicedLUTSize, skyboxDepthSlicedLUTSize, skyboxDepthSlicedLUTSize, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, nullptr, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	glCreateFramebuffers(skyboxDepthSlicedLUTSize, skyboxDepthSlicedLUTFBOs);
	for (size_t i = 0; i < skyboxDepthSlicedLUTSize; i++)
	{
		glNamedFramebufferTextureLayer(skyboxDepthSlicedLUTFBOs[i], GL_COLOR_ATTACHMENT0, skyboxDepthSlicedLUT->getHandle(), 0, i);
		if (glCheckNamedFramebufferStatus(skyboxDepthSlicedLUTFBOs[i], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete! (Skybox depth sliced Framebuffer (#" << i << "))" << std::endl;
	}
	ShaderExtCloud_effect::atmosphericScattering = skyboxDepthSlicedLUT->getHandle();

	skyboxDetailsSS =
		new Texture2D(
			(GLsizei)MainLoop::getInstance().camera.width,//(GLsizei)ssaoFBOSize,
			(GLsizei)MainLoop::getInstance().camera.height,//(GLsizei)ssaoFBOSize,
			1,
			GL_RGB16F,
			GL_RGB,
			GL_FLOAT,
			nullptr,
			GL_LINEAR,
			GL_LINEAR,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	glCreateFramebuffers(1, &skyboxDetailsSSFBO);
	glNamedFramebufferTexture(skyboxDetailsSSFBO, GL_COLOR_ATTACHMENT0, skyboxDetailsSS->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(skyboxDetailsSSFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Nighttime Skybox Screenspace Framebuffer)" << std::endl;

	irradianceMapInterpolated = new TextureCubemap(irradianceMapSize, irradianceMapSize, 1, GL_RGB16F, GL_RGB, GL_FLOAT, nullptr, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	glCreateFramebuffers(1, &irradianceMapInterpolatedFBO);
	prefilterMapInterpolated = new TextureCubemap(prefilterMapSize, prefilterMapSize, maxMipLevels, GL_RGB16F, GL_RGB, GL_FLOAT, nullptr, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	glCreateFramebuffers(1, &prefilterMapInterpolatedFBO);

	ShaderExtPBR_daynight_cycle::irradianceMap = irradianceMapInterpolated->getHandle();
	ShaderExtPBR_daynight_cycle::prefilterMap = prefilterMapInterpolated->getHandle();


	//
	// Create SSAO framebuffer
	//
	ssaoRotationTexture = (Texture*)Resources::getResource("texture;ssaoRotation");
	ssaoTexture =
		new Texture2D(
			(GLsizei)MainLoop::getInstance().camera.width,//(GLsizei)ssaoFBOSize,
			(GLsizei)MainLoop::getInstance().camera.height,//(GLsizei)ssaoFBOSize,
			1,
			GL_R8,
			GL_RED,
			GL_UNSIGNED_BYTE,
			nullptr,
			GL_LINEAR,
			GL_LINEAR,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	ssaoBlurTexture =
		new Texture2D(
			(GLsizei)MainLoop::getInstance().camera.width,//(GLsizei)ssaoFBOSize,
			(GLsizei)MainLoop::getInstance().camera.height,//(GLsizei)ssaoFBOSize,
			1,
			GL_R8,
			GL_RED,
			GL_UNSIGNED_BYTE,
			nullptr,
			GL_LINEAR,
			GL_LINEAR,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);

	ShaderExtSSAO::ssaoTexture = ssaoTexture->getHandle();
	
	glCreateFramebuffers(1, &ssaoFBO);
	glNamedFramebufferTexture(ssaoFBO, GL_COLOR_ATTACHMENT0, ssaoTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(ssaoFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (SSAO Framebuffer)" << std::endl;
	
	glCreateFramebuffers(1, &ssaoBlurFBO);
	glNamedFramebufferTexture(ssaoBlurFBO, GL_COLOR_ATTACHMENT0, ssaoBlurTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(ssaoBlurFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (SSAO Blur Framebuffer)" << std::endl;

	//
	// Create Volumetric Lighting framebuffer
	//
	//volumetricLightingStrength = 0.01f;		// @NOTE: I hate how subtle it is, but it just needs to be like this lol (According to Tiffoneus Bamboozler)  -Timo 01-20-2022
	volumetricLightingStrength = 0.005f;		// @FIXME: @TODO: @NOTE: This is really bad looking and can cause gpu color issues if the brightness is too high (which 0.01f was too high apparently.. on some angles) SO YOU NEED TO REDO THE VOLUMETRIC LIGHT SYSTEM AND MAKE IT BETTER!!!!!

	constexpr float volumetricTextureScale = 0.25f;  // 0.125f;
	volumetricTextureWidth = MainLoop::getInstance().camera.width * volumetricTextureScale;
	volumetricTextureHeight = MainLoop::getInstance().camera.height * volumetricTextureScale;

	volumetricTexture =
		new Texture2D(
			(GLsizei)volumetricTextureWidth,
			(GLsizei)volumetricTextureHeight,
			1,
			GL_R32F,
			GL_RED,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_LINEAR,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	volumetricBlurTexture =
		new Texture2D(
			(GLsizei)volumetricTextureWidth,
			(GLsizei)volumetricTextureHeight,
			1,
			GL_R32F,
			GL_RED,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_LINEAR,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);

	glCreateFramebuffers(1, &volumetricFBO);
	glNamedFramebufferTexture(volumetricFBO, GL_COLOR_ATTACHMENT0, volumetricTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(volumetricFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Volumetric Framebuffer)" << std::endl;

	glCreateFramebuffers(1, &volumetricBlurFBO);
	glNamedFramebufferTexture(volumetricBlurFBO, GL_COLOR_ATTACHMENT0, volumetricBlurTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(volumetricBlurFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Volumetric Blur Framebuffer)" << std::endl;

	//
	// Create cloud raymarching buffer
	//
	constexpr float cloudEffectTextureScale = 1.0f;			// @NOTE: @CLOUDS: This can be used for settings/quality settings. Doing full-size rendering will be default, but half and quarter size should be an option too.
	cloudEffectTextureWidth = MainLoop::getInstance().camera.width * cloudEffectTextureScale;
	cloudEffectTextureHeight = MainLoop::getInstance().camera.height * cloudEffectTextureScale;

	cloudEffectTexture =
		new Texture2D(
			(GLsizei)cloudEffectTextureWidth,
			(GLsizei)cloudEffectTextureHeight,
			1,
			GL_RGBA32F,
			GL_RGBA,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	cloudEffectBlurTexture =
		new Texture2D(
			(GLsizei)cloudEffectTextureWidth,
			(GLsizei)cloudEffectTextureHeight,
			1,
			GL_RGBA32F,
			GL_RGBA,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	cloudEffectDepthTexture =
		new Texture2D(
			(GLsizei)cloudEffectTextureWidth,
			(GLsizei)cloudEffectTextureHeight,
			1,
			GL_R32F,  //GL_DEPTH_COMPONENT24,
			GL_RED,  //GL_DEPTH_COMPONENT,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	cloudEffectDepthTextureFloodFill =
		new Texture2D(
			(GLsizei)cloudEffectTextureWidth,
			(GLsizei)cloudEffectTextureHeight,
			1,
			GL_R32F,
			GL_RED,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	cloudEffectTAAHistoryTexture =
		new Texture2D(
			(GLsizei)cloudEffectTextureWidth,
			(GLsizei)cloudEffectTextureHeight,
			1,
			GL_RGBA32F,
			GL_RGBA,
			GL_FLOAT,
			nullptr,
			GL_LINEAR,
			GL_LINEAR,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);

	ShaderExtCloud_effect::cloudEffect = cloudEffectTexture->getHandle();
	ShaderExtCloud_effect::cloudDepthTexture = cloudEffectDepthTexture->getHandle();

	glCreateFramebuffers(1, &cloudEffectFBO);
	glNamedFramebufferTexture(cloudEffectFBO, GL_COLOR_ATTACHMENT0, cloudEffectTexture->getHandle(), 0);
	glNamedFramebufferTexture(cloudEffectFBO, GL_COLOR_ATTACHMENT1, cloudEffectDepthTexture->getHandle(), 0);		// @NOTE: this is needing to be a color attachment and not a depth attachment bc we need to write the depth value manually instead of letting the fullscreen quad be the "Depth"  -Timo
	if (glCheckNamedFramebufferStatus(cloudEffectFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Cloud Effect Screenspace Framebuffer)" << std::endl;

	glCreateFramebuffers(1, &cloudEffectBlurFBO);
	glNamedFramebufferTexture(cloudEffectBlurFBO, GL_COLOR_ATTACHMENT0, cloudEffectBlurTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(cloudEffectBlurFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Cloud Effect Blur Screenspace Framebuffer)" << std::endl;

	glCreateFramebuffers(1, &cloudEffectDepthFloodFillXFBO);
	glNamedFramebufferTexture(cloudEffectDepthFloodFillXFBO, GL_COLOR_ATTACHMENT0, cloudEffectDepthTextureFloodFill->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(cloudEffectDepthFloodFillXFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Cloud Effect Depth Floodfill X Screenspace Framebuffer)" << std::endl;

	glCreateFramebuffers(1, &cloudEffectDepthFloodFillYFBO);
	glNamedFramebufferTexture(cloudEffectDepthFloodFillYFBO, GL_COLOR_ATTACHMENT0, cloudEffectDepthTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(cloudEffectDepthFloodFillYFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Cloud Effect Depth Floodfill Y Screenspace Framebuffer)" << std::endl;

	glCreateFramebuffers(1, &cloudEffectTAAHistoryFBO);
	glNamedFramebufferTexture(cloudEffectTAAHistoryFBO, GL_COLOR_ATTACHMENT0, cloudEffectTAAHistoryTexture->getHandle(), 0);
	if (glCheckNamedFramebufferStatus(cloudEffectTAAHistoryFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (Cloud Effect TAA History Framebuffer)" << std::endl;
}

void RenderManager::destroyHDRBuffer()
{
	delete cloudEffectTexture;
	delete cloudEffectDepthTexture;
	glDeleteFramebuffers(1, &cloudEffectFBO);
	delete cloudEffectBlurTexture;
	glDeleteFramebuffers(1, &cloudEffectBlurFBO);
	delete cloudEffectDepthTextureFloodFill;
	glDeleteFramebuffers(1, &cloudEffectDepthFloodFillXFBO);
	glDeleteFramebuffers(1, &cloudEffectDepthFloodFillYFBO);
	delete cloudEffectTAAHistoryTexture;
	glDeleteFramebuffers(1, &cloudEffectTAAHistoryFBO);

	delete volumetricBlurTexture;
	glDeleteFramebuffers(1, &volumetricBlurFBO);
	delete volumetricTexture;
	glDeleteFramebuffers(1, &volumetricFBO);

	delete skyboxLowResTexture;
	glDeleteFramebuffers(1, &skyboxFBO);
	delete skyboxLowResBlurTexture;
	glDeleteFramebuffers(1, &skyboxBlurFBO);
	delete skyboxDepthSlicedLUT;
	glDeleteFramebuffers(skyboxDepthSlicedLUTSize, skyboxDepthSlicedLUTFBOs);
	delete skyboxDetailsSS;
	glDeleteFramebuffers(1, &skyboxDetailsSSFBO);
	delete irradianceMapInterpolated;
	glDeleteFramebuffers(1, &irradianceMapInterpolatedFBO);
	delete prefilterMapInterpolated;
	glDeleteFramebuffers(1, &prefilterMapInterpolatedFBO);

	delete ssaoBlurTexture;
	glDeleteFramebuffers(1, &ssaoBlurFBO);
	delete ssaoTexture;
	glDeleteFramebuffers(1, &ssaoFBO);

	delete zPrePassDepthTexture;
	glDeleteFramebuffers(1, &zPrePassFBO);

	glDeleteTextures(1, &hdrColorBuffer);
	glDeleteRenderbuffers(1, &hdrDepthRBO);
	glDeleteFramebuffers(1, &hdrFBO);

	glDeleteTextures(1, &hdrFXAAColorBuffer);
	glDeleteFramebuffers(1, &hdrFXAAFBO);

	glDeleteTextures(bloomBufferCount, bloomColorBuffers);
	glDeleteFramebuffers(bloomBufferCount, bloomFBOs);
}

constexpr GLsizei luminanceTextureSize = 64;
void RenderManager::createLumenAdaptationTextures()
{
	// NOTE: these are always gonna be a constant 64x64 mipmapped so yeah, no need to recreate
	hdrLumDownsampling =
		new Texture2D(
			luminanceTextureSize,
			luminanceTextureSize,
			glm::floor(glm::log2((float_t)luminanceTextureSize)) + 1,		// log_2(64) == 6, and add 1 (i.e.: 64, 32, 16, 8, 4, 2, 1 :: 7 levels)
			GL_R16F,
			GL_RED,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	glCreateFramebuffers(1, &hdrLumFBO);
	glNamedFramebufferTexture(hdrLumFBO, GL_COLOR_ATTACHMENT0, hdrLumDownsampling->getHandle(), 0);		// First level will get populated, and when we need to read the luminance, we'll generate mipmaps and get the lowest luminance value
	if (glCheckNamedFramebufferStatus(hdrLumFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (HDR Luminance Sampling Framebuffer)" << std::endl;

	// Create Sampling buffer
	glGenTextures(1, &hdrLumAdaptation1x1);		// NOTE: this has to be non-DSA version of creating textures. For some reason it doesn't work with glCreateTextures();
	glTextureView(hdrLumAdaptation1x1, GL_TEXTURE_2D, hdrLumDownsampling->getHandle(), GL_R16F, 6, 1, 0, 1);

	// Create prev/processed buffers (will be used for ping pong)
	hdrLumAdaptationPrevious =
		new Texture2D(
			1,
			1,
			1,
			GL_R16F,
			GL_RED,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	hdrLumAdaptationProcessed =
		new Texture2D(
			1,
			1,
			1,
			GL_R16F,
			GL_RED,
			GL_FLOAT,
			nullptr,
			GL_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE
		);
	const glm::vec4 startingPixel(glm::vec3(0.00005f), 1.0f);		// NOTE: I presume this'll be mega bright
	glTextureSubImage2D(hdrLumAdaptationPrevious->getHandle(), 0, 0, 0, 1, 1, GL_RED, GL_FLOAT, glm::value_ptr(startingPixel));
}

void RenderManager::destroyLumenAdaptationTextures()
{
	glDeleteTextures(1, &hdrLumAdaptation1x1);
	delete hdrLumDownsampling;
	glDeleteFramebuffers(1, &hdrLumFBO);
	delete hdrLumAdaptationPrevious;
	delete hdrLumAdaptationProcessed;
}

// @NOTE: https://www.guerrilla-games.com/media/News/Files/The-Real-time-Volumetric-Cloudscapes-of-Horizon-Zero-Dawn.pdf
constexpr GLsizei cloudNoiseTex1Size = 128;
constexpr GLsizei cloudNoiseTex2Size = 32;
Texture* cloudNoise1Channels[4];
Texture* cloudNoise2Channels[3];
void RenderManager::createCloudNoise()
{
	constexpr const char* baseNoiseDirectory = "res/_generated/cloud_base_noise";
	constexpr const char* detailNoiseDirectory = "res/_generated/cloud_detail_noise";

	//
	// PRECHECK: see if should load clouds from textures instead
	//
	if (std::filesystem::is_directory(baseNoiseDirectory) &&
		std::filesystem::is_directory(detailNoiseDirectory))
	{
		std::cout << "::CLOUD NOISE GENERATOR:: Cache detected. Loading noise texture from cache..." << std::endl;
		try
		{
			// Compile all base noise files together
			std::vector<ImageFile> baseNoiseFiles;
			std::vector<ImageFile> detailNoiseFiles;

			for (size_t i = 0; i < cloudNoiseTex1Size; i++)
			{
				std::string filename = "/layer" + std::to_string(i) + ".png";
				if (!std::filesystem::exists(baseNoiseDirectory + filename))
				{
					std::cout << "File " << std::string(baseNoiseDirectory) << filename << " Does not exist." << std::endl;
					throw new std::exception();
				}

				baseNoiseFiles.push_back({ baseNoiseDirectory + filename, true, false, false });
			}

			for (size_t i = 0; i < cloudNoiseTex2Size; i++)
			{
				std::string filename = "/layer" + std::to_string(i) + ".png";
				if (!std::filesystem::exists(detailNoiseDirectory + filename))
				{
					std::cout << "File " << std::string(detailNoiseDirectory) << filename << " Does not exist." << std::endl;
					throw new std::exception();
				}

				detailNoiseFiles.push_back({ detailNoiseDirectory + filename, true, false, false });
			}

			// Load up the textures (@CLOUDS: noise 1 has 4 channels RGBA, and noise2 has 3 channels RGB)
			Texture::setLoadSync(true);
			cloudNoise1 = new Texture3DFromFile(baseNoiseFiles, GL_RGBA, GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT, GL_REPEAT);
			cloudNoise2 = new Texture3DFromFile(detailNoiseFiles, GL_RGB, GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT, GL_REPEAT);
			Texture::setLoadSync(false);

			std::cout << "::CLOUD NOISE GENERATOR:: Successfully loaded noise cache" << std::endl;
			return;
		}
		catch (const std::exception& e)
		{
			std::cout << "::ERROR:: " << e.what() << std::endl;
			std::cout << "::CLOUD NOISE GENERATOR:: Cache load failed. Falling back to auto-generate." << std::endl;
		}
	}

	//
	// GENERATE CLOUD NOISE (Fallback/first time... @NOTE: ship the game with the cloud cache fyi, or at least a file that contains the points)
	//
	std::cout << "::CLOUD NOISE GENERATOR:: Start" << std::endl;

	//
	// @Cloud noise 1:
	//		R: perlin-worley
	//		G: worley	(Same freq. as R)
	//		B: worley	(Medium frequency)
	//		A: worley	(Highest frequency)
	//
	cloudNoise1 =
		new Texture3D(
			cloudNoiseTex1Size,
			cloudNoiseTex1Size,
			cloudNoiseTex1Size,
			1,
			GL_RGBA8,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			nullptr,
			GL_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			GL_REPEAT
		);

	// Create just a temporary UBO to store the worley noise points
	GLuint cloudNoiseUBO;
	glCreateBuffers(1, &cloudNoiseUBO);
	glNamedBufferData(cloudNoiseUBO, sizeof(glm::vec4) * 1024, nullptr, GL_STATIC_DRAW);	 // @NOTE: the int:numPoints variable gets rounded up to 16 bytes in the buffer btw. No packing.
	glBindBufferBase(GL_UNIFORM_BUFFER, 4, cloudNoiseUBO);

	Texture* cloudNoise1FractalOctaves[] = {
		new Texture3D(cloudNoiseTex1Size, cloudNoiseTex1Size, cloudNoiseTex1Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT),
		new Texture3D(cloudNoiseTex1Size, cloudNoiseTex1Size, cloudNoiseTex1Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT),
		new Texture3D(cloudNoiseTex1Size, cloudNoiseTex1Size, cloudNoiseTex1Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT),
	};

	//Texture* cloudNoise1Channels[] = {
	cloudNoise1Channels[0] = new Texture3D(cloudNoiseTex1Size, cloudNoiseTex1Size, cloudNoiseTex1Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT);   //,
	cloudNoise1Channels[1] = new Texture3D(cloudNoiseTex1Size, cloudNoiseTex1Size, cloudNoiseTex1Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT);   //,
	cloudNoise1Channels[2] = new Texture3D(cloudNoiseTex1Size, cloudNoiseTex1Size, cloudNoiseTex1Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT);   //,
	cloudNoise1Channels[3] = new Texture3D(cloudNoiseTex1Size, cloudNoiseTex1Size, cloudNoiseTex1Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT);   //,
//};

	const size_t channelGridSizes[] = {
		3, 7, 11,
		9, 20, 33,
		12, 24, 40,
		20, 35, 49
	};		// @NOTE: amount^3 must not exceed 1024... bc I can't figure out how to get more than 1024 in a ubo even though the spec says I should have at least 64kb of vram????
	std::vector<glm::vec3> worleyPoints[] = {
		std::vector<glm::vec3>(), std::vector<glm::vec3>(), std::vector<glm::vec3>(),
		std::vector<glm::vec3>(), std::vector<glm::vec3>(), std::vector<glm::vec3>(),
		std::vector<glm::vec3>(), std::vector<glm::vec3>(), std::vector<glm::vec3>(),
		std::vector<glm::vec3>(), std::vector<glm::vec3>(), std::vector<glm::vec3>()
	};

	std::cout << "::CLOUD NOISE GENERATOR:: Generating grid offsets" << std::endl;

	std::random_device randomDevice;
	std::mt19937 randomEngine(randomDevice());
	std::uniform_real_distribution<> distribution(0.0, 1.0);

	for (size_t i = 0; i < 12; i++)
	{
		size_t worleyPointCount = channelGridSizes[i] * channelGridSizes[i] * channelGridSizes[i];
		for (size_t j = 0; j < glm::min((size_t)1024, worleyPointCount); j++)
			worleyPoints[i].push_back(
				glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine))
			);
	}

	// Render out all the layers with the points!
	// @TODO: I think what needs to happen is to generate a texture that contains all the points for a worley map and then use that texture to generate the actual worley map. This look up image can just be a GL_NEAREST filtered 8192x8192 texture I think. That should be big enough, and then delete it after it's not needed anymore.  -Timo
	//		@RESPONSE: So I think that the current sitation where only 1024 points are inserted and then repeated works just fine actually. This allows for a smaller set of information needed to look thru.  -Timo
	// @TODO: Also I am noting that there is noise stacking on page 33 of the pdf (https://www.guerrilla-games.com/media/News/Files/The-Real-time-Volumetric-Cloudscapes-of-Horizon-Zero-Dawn.pdf) FOR SURE!!!!!
	std::cout << "::CLOUD NOISE GENERATOR:: Rendering base worley noise" << std::endl;
	
	GLuint noiseFBO;
	glCreateFramebuffers(1, &noiseFBO);
	glViewport(0, 0, cloudNoiseTex1Size, cloudNoiseTex1Size);

	// Render noise textures onto 4 different 8 bit textures
	for (size_t j = 0; j < 4; j++)
	{
		cloudNoiseGenerateShader->use();
		cloudNoiseGenerateShader->setInt("includePerlin", (int)(j == 0));

		// Render each octave (3 total) for one 8 bit noise texture
		size_t numNoiseOctaves = 3;
		for (size_t k = 0; k < numNoiseOctaves; k++)
		{
			cloudNoiseGenerateShader->setInt("gridSize", (int)channelGridSizes[j * numNoiseOctaves + k]);

			assert(worleyPoints[j * numNoiseOctaves + k].size() <= 1024);
			cloudNoiseInfo.worleyPoints.clear();
			for (size_t worleypointcopy = 0; worleypointcopy < worleyPoints[j * numNoiseOctaves + k].size(); worleypointcopy++)
				cloudNoiseInfo.worleyPoints.push_back(glm::vec4(worleyPoints[j * numNoiseOctaves + k][worleypointcopy], 0.0f));
			glNamedBufferSubData(cloudNoiseUBO, 0, sizeof(glm::vec4) * cloudNoiseInfo.worleyPoints.size(), glm::value_ptr(cloudNoiseInfo.worleyPoints[0]));	 // @NOTE: the offset is sizeof(glm::vec4) bc the memory layout std140 rounds up the 4 bytes of the sizeof(int) up to 16 bytes (size of a vec4)

			//
			// Render each layer with the noise profile
			//
			for (size_t i = 0; i < cloudNoiseTex1Size; i++)
			{
				cloudNoiseGenerateShader->setFloat("currentRenderDepth", (float)i / (float)cloudNoiseTex1Size);

				glNamedFramebufferTextureLayer(noiseFBO, GL_COLOR_ATTACHMENT0, cloudNoise1FractalOctaves[k]->getHandle(), 0, i);
				auto status = glCheckNamedFramebufferStatus(noiseFBO, GL_FRAMEBUFFER);
				if (status != GL_FRAMEBUFFER_COMPLETE)
					std::cout << "Framebuffer not complete! (Worley Noise FBO) (Layer: " << i << ") (Channel: " << j << ") (Octave: " << k << ")" << std::endl;

				glBindFramebuffer(GL_FRAMEBUFFER, noiseFBO);
				glClear(GL_COLOR_BUFFER_BIT);
				renderQuad();
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
		}

		//
		// Combine octaves into a single noise channel
		//
		for (size_t i = 0; i < cloudNoiseTex1Size; i++)
		{
			cloudNoiseFractalShader->use();
			cloudNoiseFractalShader->setFloat("currentRenderDepth", (float)i / (float)cloudNoiseTex1Size);
			cloudNoiseFractalShader->setSampler("sample1", cloudNoise1FractalOctaves[0]->getHandle());
			cloudNoiseFractalShader->setSampler("sample2", cloudNoise1FractalOctaves[1]->getHandle());
			cloudNoiseFractalShader->setSampler("sample3", cloudNoise1FractalOctaves[2]->getHandle());
			glNamedFramebufferTextureLayer(noiseFBO, GL_COLOR_ATTACHMENT0, cloudNoise1Channels[j]->getHandle(), 0, i);
			auto status = glCheckNamedFramebufferStatus(noiseFBO, GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE)
				std::cout << "Framebuffer not complete! (Worley Noise FBO) (Layer: " << i << ") (Channel: " << j << ")" << std::endl;

			glBindFramebuffer(GL_FRAMEBUFFER, noiseFBO);
			glClear(GL_COLOR_BUFFER_BIT);
			renderQuad();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	//
	// Render all 4 of the noise textures onto a single RGBA texture
	//
	for (size_t i = 0; i < cloudNoiseTex1Size; i++)
	{
		cloudNoiseCombineShader->use();
		cloudNoiseCombineShader->setFloat("currentRenderDepth", (float)i / (float)cloudNoiseTex1Size);
		cloudNoiseCombineShader->setSampler("R", cloudNoise1Channels[0]->getHandle());
		cloudNoiseCombineShader->setSampler("G", cloudNoise1Channels[1]->getHandle());
		cloudNoiseCombineShader->setSampler("B", cloudNoise1Channels[2]->getHandle());
		cloudNoiseCombineShader->setSampler("A", cloudNoise1Channels[3]->getHandle());

		glNamedFramebufferTextureLayer(noiseFBO, GL_COLOR_ATTACHMENT0, cloudNoise1->getHandle(), 0, i);
		auto status = glCheckNamedFramebufferStatus(noiseFBO, GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete! (Worley Noise FBO) (Layer: " << i << ") (Channel: COMBINER)" << std::endl;

		glBindFramebuffer(GL_FRAMEBUFFER, noiseFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		renderQuad();

		// Save the render result for later!		@COPYPASTA
		static bool first = true;
		if (first)
		{
			first = false;
			std::filesystem::create_directories(baseNoiseDirectory);
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, noiseFBO);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		GLubyte* pixels = new GLubyte[4 * cloudNoiseTex1Size * cloudNoiseTex1Size];
		glReadPixels(0, 0, cloudNoiseTex1Size, cloudNoiseTex1Size, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		stbi_flip_vertically_on_write(true);
		stbi_write_png((baseNoiseDirectory + std::string("/layer") + std::to_string(i) + ".png").c_str(), cloudNoiseTex1Size, cloudNoiseTex1Size, 4, pixels, 4 * cloudNoiseTex1Size);
		std::cout << "::CLOUD NOISE GENERATOR:: \tSaved render for base_noise layer " << i << std::endl;
		delete[] pixels;

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	//
	// Cloud noise 2:
	//		R: worley	(Lowest frequency)
	//		G: worley
	//		B: worley	(Highest frequency)
	//
	cloudNoise2 =
		new Texture3D(
			cloudNoiseTex2Size,
			cloudNoiseTex2Size,
			cloudNoiseTex2Size,
			1,
			GL_RGB8,
			GL_RGB,
			GL_UNSIGNED_BYTE,
			nullptr,
			GL_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			GL_REPEAT
		);

	Texture* cloudNoise2FractalOctaves[] = {
		new Texture3D(cloudNoiseTex2Size, cloudNoiseTex2Size, cloudNoiseTex2Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT),
		new Texture3D(cloudNoiseTex2Size, cloudNoiseTex2Size, cloudNoiseTex2Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT),
		new Texture3D(cloudNoiseTex2Size, cloudNoiseTex2Size, cloudNoiseTex2Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT),
	};

	//Texture* cloudNoise2Channels[] = {
		cloudNoise2Channels[0] = new Texture3D(cloudNoiseTex2Size, cloudNoiseTex2Size, cloudNoiseTex2Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT);   //,
		cloudNoise2Channels[1] = new Texture3D(cloudNoiseTex2Size, cloudNoiseTex2Size, cloudNoiseTex2Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT);   //,
		cloudNoise2Channels[2] = new Texture3D(cloudNoiseTex2Size, cloudNoiseTex2Size, cloudNoiseTex2Size, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, nullptr, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT);   //,
	//};

	// @COPYPASTA: Reinsert/recalculate the randomized points for worley noise
	std::cout << "::CLOUD NOISE GENERATOR:: Generating detail noise grid offsets" << std::endl;

	for (size_t i = 0; i < 9; i++)
	{
		size_t worleyPointCount = channelGridSizes[i] * channelGridSizes[i] * channelGridSizes[i];
		worleyPoints[i].clear();
		for (size_t j = 0; j < glm::min((size_t)1024, worleyPointCount); j++)
			worleyPoints[i].push_back(
				glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine))
			);
	}

	// Render it out!
	glViewport(0, 0, cloudNoiseTex2Size, cloudNoiseTex2Size);
	std::cout << "::CLOUD NOISE GENERATOR:: Rendering detail worley noise" << std::endl;

	// Render noise textures onto 3 different 8 bit textures
	for (size_t j = 0; j < 3; j++)
	{
		cloudNoiseGenerateShader->use();
		cloudNoiseGenerateShader->setInt("includePerlin", false);

		// Render each octave (3 total) for one 8 bit noise texture
		size_t numNoiseOctaves = 3;
		for (size_t k = 0; k < numNoiseOctaves; k++)
		{
			cloudNoiseGenerateShader->setInt("gridSize", (int)channelGridSizes[j * numNoiseOctaves + k]);

			assert(worleyPoints[j * numNoiseOctaves + k].size() <= 1024);
			cloudNoiseInfo.worleyPoints.clear();
			for (size_t worleypointcopy = 0; worleypointcopy < worleyPoints[j * numNoiseOctaves + k].size(); worleypointcopy++)
				cloudNoiseInfo.worleyPoints.push_back(glm::vec4(worleyPoints[j * numNoiseOctaves + k][worleypointcopy], 0.0f));
			glNamedBufferSubData(cloudNoiseUBO, 0, sizeof(glm::vec4) * cloudNoiseInfo.worleyPoints.size(), glm::value_ptr(cloudNoiseInfo.worleyPoints[0]));	 // @NOTE: the offset is sizeof(glm::vec4) bc the memory layout std140 rounds up the 4 bytes of the sizeof(int) up to 16 bytes (size of a vec4)

			//
			// Render each layer with the noise profile
			//
			for (size_t i = 0; i < cloudNoiseTex2Size; i++)
			{
				cloudNoiseGenerateShader->setFloat("currentRenderDepth", (float)i / (float)cloudNoiseTex2Size);

				glNamedFramebufferTextureLayer(noiseFBO, GL_COLOR_ATTACHMENT0, cloudNoise2FractalOctaves[k]->getHandle(), 0, i);
				auto status = glCheckNamedFramebufferStatus(noiseFBO, GL_FRAMEBUFFER);
				if (status != GL_FRAMEBUFFER_COMPLETE)
					std::cout << "Framebuffer not complete! (Worley Noise FBO (pt2)) (Layer: " << i << ") (Channel: " << j << ") (Octave: " << k << ")" << std::endl;

				glBindFramebuffer(GL_FRAMEBUFFER, noiseFBO);
				glClear(GL_COLOR_BUFFER_BIT);
				renderQuad();
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
		}

		//
		// Combine octaves into a single noise channel
		//
		for (size_t i = 0; i < cloudNoiseTex2Size; i++)
		{
			cloudNoiseFractalShader->use();
			cloudNoiseFractalShader->setFloat("currentRenderDepth", (float)i / (float)cloudNoiseTex2Size);
			cloudNoiseFractalShader->setSampler("sample1", cloudNoise2FractalOctaves[0]->getHandle());
			cloudNoiseFractalShader->setSampler("sample2", cloudNoise2FractalOctaves[1]->getHandle());
			cloudNoiseFractalShader->setSampler("sample3", cloudNoise2FractalOctaves[2]->getHandle());
			glNamedFramebufferTextureLayer(noiseFBO, GL_COLOR_ATTACHMENT0, cloudNoise2Channels[j]->getHandle(), 0, i);
			auto status = glCheckNamedFramebufferStatus(noiseFBO, GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE)
				std::cout << "Framebuffer not complete! (Worley Noise FBO (pt2)) (Layer: " << i << ") (Channel: " << j << ")" << std::endl;

			glBindFramebuffer(GL_FRAMEBUFFER, noiseFBO);
			glClear(GL_COLOR_BUFFER_BIT);
			renderQuad();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	//
	// Render all 3 of the noise textures onto a single RGB texture
	//
	for (size_t i = 0; i < cloudNoiseTex2Size; i++)
	{
		cloudNoiseCombineShader->use();
		cloudNoiseCombineShader->setFloat("currentRenderDepth", (float)i / (float)cloudNoiseTex2Size);
		cloudNoiseCombineShader->setSampler("R", cloudNoise2Channels[0]->getHandle());
		cloudNoiseCombineShader->setSampler("G", cloudNoise2Channels[1]->getHandle());
		cloudNoiseCombineShader->setSampler("B", cloudNoise2Channels[2]->getHandle());

		glNamedFramebufferTextureLayer(noiseFBO, GL_COLOR_ATTACHMENT0, cloudNoise2->getHandle(), 0, i);
		auto status = glCheckNamedFramebufferStatus(noiseFBO, GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete! (Worley Noise FBO (pt2)) (Layer: " << i << ") (Channel: COMBINER)" << std::endl;

		glBindFramebuffer(GL_FRAMEBUFFER, noiseFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		renderQuad();

		// Save the render result for later!		@COPYPASTA
		static bool first = true;
		if (first)
		{
			first = false;
			std::filesystem::create_directories(detailNoiseDirectory);
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, noiseFBO);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		GLubyte* pixels = new GLubyte[3 * cloudNoiseTex2Size * cloudNoiseTex2Size];
		glReadPixels(0, 0, cloudNoiseTex2Size, cloudNoiseTex2Size, GL_RGB, GL_UNSIGNED_BYTE, pixels);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		stbi_flip_vertically_on_write(true);
		stbi_write_png((detailNoiseDirectory + std::string("/layer") + std::to_string(i) + ".png").c_str(), cloudNoiseTex2Size, cloudNoiseTex2Size, 3, pixels, 3 * cloudNoiseTex2Size);
		std::cout << "::CLOUD NOISE GENERATOR:: \tSaved render for detail_noise layer " << i << std::endl;
		delete[] pixels;

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	// Cleanup
	glDeleteFramebuffers(1, &noiseFBO);
	glDeleteBuffers(1, &cloudNoiseUBO);


	std::cout << "::CLOUD NOISE GENERATOR:: Finished" << std::endl;
}

void RenderManager::destroyCloudNoise()
{
	delete cloudNoise1;
	delete cloudNoise2;
}

void RenderManager::createShaderPrograms()
{
	skybox_program_id = (Shader*)Resources::getResource("shader;skybox");
	skyboxDetailsShader = (Shader*)Resources::getResource("shader;skyboxDetails");
	debug_csm_program_id = (Shader*)Resources::getResource("shader;debugCSM");
	debug_cloud_noise_program_id = (Shader*)Resources::getResource("shader;debugCloudNoise");
	text_program_id = (Shader*)Resources::getResource("shader;text");
	irradiance_program_id = (Shader*)Resources::getResource("shader;irradianceGeneration");
	prefilter_program_id = (Shader*)Resources::getResource("shader;pbrPrefilterGeneration");
	brdf_program_id = (Shader*)Resources::getResource("shader;brdfGeneration");
	environmentMapMixerShader = (Shader*)Resources::getResource("shader;environmentMapMixer");
	hdrLuminanceProgramId = (Shader*)Resources::getResource("shader;luminance_postprocessing");
	hdrLumAdaptationComputeProgramId = (Shader*)Resources::getResource("shader;computeLuminanceAdaptation");
	bloom_postprocessing_program_id = (Shader*)Resources::getResource("shader;bloom_postprocessing");
	postprocessing_program_id = (Shader*)Resources::getResource("shader;postprocessing");
	fxaaPostProcessingShader = (Shader*)Resources::getResource("shader;fxaa_postprocessing");
	pbrShaderProgramId = (Shader*)Resources::getResource("shader;pbr");
	notificationUIProgramId = (Shader*)Resources::getResource("shader;notificationUI");
	INTERNALzPassShader = (Shader*)Resources::getResource("shader;zPassShader");
	ssaoProgramId = (Shader*)Resources::getResource("shader;ssao");
	volumetricProgramId = (Shader*)Resources::getResource("shader;volumetricLighting");
	blurXProgramId = (Shader*)Resources::getResource("shader;blurX");
	blurYProgramId = (Shader*)Resources::getResource("shader;blurY");
	blurX3ProgramId = (Shader*)Resources::getResource("shader;blurX3");
	blurY3ProgramId = (Shader*)Resources::getResource("shader;blurY3");
	cloudNoiseGenerateShader = (Shader*)Resources::getResource("shader;cloudNoiseGenerate");
	cloudNoiseFractalShader = (Shader*)Resources::getResource("shader;cloudNoiseFractal");
	cloudNoiseCombineShader = (Shader*)Resources::getResource("shader;cloudNoiseCombine");
	cloudEffectShader = (Shader*)Resources::getResource("shader;cloudEffectSS");
	cloudEffectFloodFillShaderX = (Shader*)Resources::getResource("shader;cloudEffectDepthFloodfillX");
	cloudEffectFloodFillShaderY = (Shader*)Resources::getResource("shader;cloudEffectDepthFloodfillY");
	cloudEffectColorFloodFillShaderX = (Shader*)Resources::getResource("shader;cloudEffectColorFloodfillX");
	cloudEffectColorFloodFillShaderY = (Shader*)Resources::getResource("shader;cloudEffectColorFloodfillY");
	simpleDenoiseShader = (Shader*)Resources::getResource("shader;sirBirdDenoise");
	cloudEffectApplyShader = (Shader*)Resources::getResource("shader;cloudEffectApply");
	cloudEffectTAAHistoryShader = (Shader*)Resources::getResource("shader;cloudHistoryTAA");
}

void RenderManager::destroyShaderPrograms()
{
	Resources::unloadResource("shader;skybox");
	Resources::unloadResource("shader;skyboxDetails");
	Resources::unloadResource("shader;debugCSM");
	Resources::unloadResource("shader;debugCloudNoise");
	Resources::unloadResource("shader;text");
	Resources::unloadResource("shader;irradianceGeneration");
	Resources::unloadResource("shader;pbrPrefilterGeneration");
	Resources::unloadResource("shader;brdfGeneration");
	Resources::unloadResource("shader;environmentMapMixer");
	Resources::unloadResource("shader;luminance_postprocessing");
	Resources::unloadResource("shader;computeLuminanceAdaptation");
	Resources::unloadResource("shader;bloom_postprocessing");
	Resources::unloadResource("shader;postprocessing");
	Resources::unloadResource("shader;fxaa_postprocessing");
	Resources::unloadResource("shader;pbr");
	Resources::unloadResource("shader;notificationUI");
	Resources::unloadResource("shader;zPassShader");
	Resources::unloadResource("shader;ssao");
	Resources::unloadResource("shader;volumetricLighting");
	Resources::unloadResource("shader;blurX");
	Resources::unloadResource("shader;blurY");
	Resources::unloadResource("shader;blurX3");
	Resources::unloadResource("shader;blurY3");
	Resources::unloadResource("shader;cloudNoiseGenerate");
	Resources::unloadResource("shader;cloudNoiseFractal");
	Resources::unloadResource("shader;cloudNoiseCombine");
	Resources::unloadResource("shader;cloudEffectSS");
	Resources::unloadResource("shader;cloudEffectDepthFloodfillX");
	Resources::unloadResource("shader;cloudEffectDepthFloodfillY");
	Resources::unloadResource("shader;cloudEffectColorFloodfillX");
	Resources::unloadResource("shader;cloudEffectColorFloodfillY");
	Resources::unloadResource("shader;sirBirdDenoise");
	Resources::unloadResource("shader;cloudEffectApply");
	Resources::unloadResource("shader;cloudHistoryTAA");
}

void RenderManager::createFonts()
{
	FT_Library ft;
	if (FT_Init_FreeType(&ft))
	{
		std::cout << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
		return;
	}

	FT_Face face;
	if (FT_New_Face(ft, "res/font/arial.ttf", 0, &face))
	{
		std::cout << "ERROR::FREETYPE: Failed to load font" << std::endl;
		return;
	}

	FT_Set_Pixel_Sizes(face, 0, 32);

	//
	// Load ASCII first 128 characters (test for now)
	//
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (unsigned char c = 0; c < 128; c++)
	{
		if (FT_Load_Char(face, c, FT_LOAD_RENDER))
		{
			std::cout << "ERROR::FREETYTPE: Failed to load Glyph" << std::endl;
			continue;
		}

		// Generate texture
		unsigned int texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RED,
			face->glyph->bitmap.width,
			face->glyph->bitmap.rows,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			face->glyph->bitmap.buffer
		);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		// Store character
		TextCharacter newChar =
		{
			texture,
			glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
			glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
			(unsigned int)face->glyph->advance.x,
			(unsigned int)face->glyph->advance.y
		};
		characters.insert(std::pair<char, TextCharacter>(c, newChar));
	}

	FT_Done_Face(face);
	FT_Done_FreeType(ft);

	//
	// Initialize vao and vbo for drawing text
	//
	glGenVertexArrays(1, &textVAO);
	glGenBuffers(1, &textVBO);
	glBindVertexArray(textVAO);
	glBindBuffer(GL_ARRAY_BUFFER, textVBO);

	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void RenderManager::destroyFonts()
{
	// Destroy all the font textures
	for (auto it = characters.begin(); it != characters.end(); it++)
	{
		glDeleteTextures(1, &it->second.textureId);
	}

	glDeleteBuffers(1, &textVBO);
	glDeleteVertexArrays(1, &textVAO);
}

void RenderManager::render()
{
#ifdef _DEVELOP
	// @Debug: reload shaders
	createShaderPrograms();
#endif

	//
	// Keyboard shortcuts for wireframe and physics debug
	// 
	// NOTE: the physics debug and mesh AABB debug views don't work in release mode bc imgui is deactivated completely in release mode
	//
	static bool prevF1Keypressed = GLFW_RELEASE;
	bool f1Keypressed = glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_F1);
	if (prevF1Keypressed == GLFW_RELEASE && f1Keypressed == GLFW_PRESS)
	{
		isWireFrameMode = !isWireFrameMode;
	}
	prevF1Keypressed = f1Keypressed;

	static bool prevF2Keypressed = GLFW_RELEASE;
	bool f2Keypressed = glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_F2);
	if (prevF2Keypressed == GLFW_RELEASE && f2Keypressed == GLFW_PRESS)
	{
		renderPhysicsDebug = !renderPhysicsDebug;
	}
	prevF2Keypressed = f2Keypressed;

	static bool prevF3Keypressed = GLFW_RELEASE;
	bool f3Keypressed = glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_F3);
	if (prevF3Keypressed == GLFW_RELEASE && f3Keypressed == GLFW_PRESS)
	{
		renderMeshRenderAABB = !renderMeshRenderAABB;
	}
	prevF3Keypressed = f3Keypressed;

	//
	// Setup projection matrix for rendering
	//
	{
		glm::mat4 cameraProjection = MainLoop::getInstance().camera.calculateProjectionMatrix();
		glm::mat4 cameraView = MainLoop::getInstance().camera.calculateViewMatrix();
		updateCameraInfoUBO(cameraProjection, cameraView);
	}

#ifdef _DEVELOP
	renderImGuiPass();
#endif

	//
	// Render shadow map(s) to depth framebuffer(s)
	//
	for (size_t i = 0; i < MainLoop::getInstance().lightObjects.size(); i++)
	{
		if (!MainLoop::getInstance().lightObjects[i]->castsShadows)
			continue;

		MainLoop::getInstance().lightObjects[i]->renderPassShadowMap();
	}

	setupSceneShadows();

	
#ifdef _DEVELOP
	//
	// Render Picking texture
	//
	if (!MainLoop::getInstance().timelineViewerMode && DEBUGdoPicking)
	{
		if (!MainLoop::getInstance().playMode)		// NOTE: no reason in particular for making this !playmode only
		{
			static int mostRecentPickedIndex = -1;

			// Render out picking data
			glViewport(0, 0, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
			glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			pickingRenderFormatShader = (Shader*)Resources::getResource("shader;pickingRenderFormat");
			pickingRenderFormatShader->use();
			for (uint32_t i = 0; i < (uint32_t)MainLoop::getInstance().objects.size(); i++)
			{
				if (i == (uint32_t)mostRecentPickedIndex)
					continue;

				RenderComponent* rc = MainLoop::getInstance().objects[i]->getRenderComponent();
				if (rc == nullptr)
					continue;

				pickingRenderFormatShader->setUint("objectID", i + 1);
				rc->renderShadow(pickingRenderFormatShader);
			}

			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			// Read pixel
			double mouseX, mouseY;
			glfwGetCursorPos(MainLoop::getInstance().window, &mouseX, &mouseY);
			PixelInfo pixInfo = readPixelFromPickingBuffer((uint32_t)mouseX, (uint32_t)(MainLoop::getInstance().camera.height - mouseY - 1));
			size_t id = (size_t)pixInfo.objectID;

			mostRecentPickedIndex = (int)id - 1;

			if (!glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_LEFT_CONTROL) &&
				!glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_RIGHT_CONTROL))
				deselectAllSelectedObject();
			if (mostRecentPickedIndex >= 0)
			{
				if (isObjectSelected((size_t)mostRecentPickedIndex))
					deselectObject((size_t)mostRecentPickedIndex);
				else
					addSelectObject((size_t)mostRecentPickedIndex);
			}
		}

		// Unset flag
		DEBUGdoPicking = false;
	}
#endif

	updateLightInformationUBO();
	renderScene();

	//
	// Compute volumetric lighting for just main light (directional light)
	//
	LightComponent* mainlight = nullptr;
	for (size_t i = 0; i < MainLoop::getInstance().lightObjects.size(); i++)
	{
		if (MainLoop::getInstance().lightObjects[i]->lightType == LightType::DIRECTIONAL)
		{
			mainlight = MainLoop::getInstance().lightObjects[i];
			assert(mainlight->castsShadows);		// NOTE: turn off volumetric lighting if shadows are turned off
			break;
		}
	}
	assert(mainlight != nullptr);		// NOTE: turn off volumetric lighting if shadows are turned off

	if (mainlight->colorIntensity > 0.0f)
	{

		glViewport(0, 0, volumetricTextureWidth, volumetricTextureHeight);
		glBindFramebuffer(GL_FRAMEBUFFER, volumetricFBO);
		volumetricProgramId->use();
		volumetricProgramId->setVec3("mainCameraPosition", MainLoop::getInstance().camera.position);
		volumetricProgramId->setVec3("mainlightDirection", mainlight->facingDirection);
		volumetricProgramId->setMat4("inverseProjectionMatrix", glm::inverse(cameraInfo.projection));
		volumetricProgramId->setMat4("inverseViewMatrix", glm::inverse(cameraInfo.view));
		renderQuad();

		//
		// Blur volumetric lighting pass
		//
		glBindFramebuffer(GL_FRAMEBUFFER, volumetricBlurFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		blurXProgramId->use();
		blurXProgramId->setSampler("textureMap", volumetricTexture->getHandle());
		renderQuad();

		glBindFramebuffer(GL_FRAMEBUFFER, volumetricFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		blurYProgramId->use();
		blurYProgramId->setSampler("textureMap", volumetricBlurTexture->getHandle());
		renderQuad();
	}

	//
	// Do luminance
	//
	glViewport(0, 0, luminanceTextureSize, luminanceTextureSize);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrLumFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	hdrLuminanceProgramId->use();
	hdrLuminanceProgramId->setSampler("hdrColorBuffer", hdrColorBuffer);
	hdrLuminanceProgramId->setSampler("volumetricLighting", volumetricTexture->getHandle());
	hdrLuminanceProgramId->setVec3("sunLightColor", mainlight->color * mainlight->colorIntensity * volumetricLightingStrength * volumetricLightingStrengthExternal);
	renderQuad();
	glGenerateTextureMipmap(hdrLumDownsampling->getHandle());		// This gets the FBO's luminance down to 1x1

	//
	// Kick off light adaptation compute shader
	//
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	hdrLumAdaptationComputeProgramId->use();
	glBindImageTexture(0, hdrLumAdaptationPrevious->getHandle(), 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
	glBindImageTexture(1, hdrLumAdaptation1x1, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R16F);
	glBindImageTexture(2, hdrLumAdaptationProcessed->getHandle(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
	//constexpr glm::vec2 adaptationSpeeds(0.5f, 2.5f);
	constexpr glm::vec2 adaptationSpeeds(1.5f, 2.5f);
	hdrLumAdaptationComputeProgramId->setVec2("adaptationSpeed", adaptationSpeeds* MainLoop::getInstance().deltaTime);
	glDispatchCompute(1, 1, 1);
	//glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);		// See this later

#ifdef _DEVELOP
	//
	// @Debug: Render wireframe of selected object
	//
	static float selectedColorIntensityTime = -1.15f;
	static size_t prevNumSelectedObjs = 0;
	if (glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_LEFT_SHIFT) == GLFW_RELEASE &&		// @NOTE: when pressing shift, this is the start to doing group append for voxel groups, so in the even that this happens, now the creator can see better by not rendering the wireframe (I believe)
		!MainLoop::getInstance().playMode)														// @NOTE: don't render the wireframe if in playmode. It's annoying.
	{
		if (prevNumSelectedObjs != selectedObjectIndices.size())
		{
			// Reset @Copypasta
			prevNumSelectedObjs = selectedObjectIndices.size();
			selectedColorIntensityTime = -1.15f;
		}

		auto objs = getSelectedObjects();
		if (!objs.empty())
		{
			// Render that selected objects!!!!
			Shader* selectionWireframeShader =
				(Shader*)Resources::getResource("shader;selectionSkinnedWireframe");
			selectionWireframeShader->use();

			glDepthMask(GL_FALSE);
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			{
				glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
				glViewport(0, 0, MainLoop::getInstance().camera.width, MainLoop::getInstance().camera.height);

				for (size_t i = 0; i < objs.size(); i++)
				{
					RenderComponent* rc = objs[i]->getRenderComponent();
					if (rc != nullptr)
					{
						float evaluatedIntensityValue = (sinf(selectedColorIntensityTime) + 1);
						//std::cout << evaluatedIntensityValue << std::endl;		@DEBUG
						selectionWireframeShader->setVec4("color", { 0.973f, 0.29f, 1.0f, std::clamp(evaluatedIntensityValue, 0.0f, 1.0f) });
						selectionWireframeShader->setFloat("colorIntensity", evaluatedIntensityValue);
						rc->renderShadow(selectionWireframeShader);
					}
				}
			}
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			glDepthMask(GL_TRUE);

			selectedColorIntensityTime += MainLoop::getInstance().deltaTime * 4.0f;
		}
	}
	else
	{
		// Reset @Copypasta
		prevNumSelectedObjs = selectedObjectIndices.size();
		selectedColorIntensityTime = -1.15f;
	}
#endif

	// Check to make sure the luminance adaptation compute shader is fimished
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

	//
	// Apply @FXAA
	//
	glViewport(0, 0, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFXAAFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	fxaaPostProcessingShader->use();
	fxaaPostProcessingShader->setSampler("hdrColorBuffer", hdrColorBuffer);
	fxaaPostProcessingShader->setSampler("luminanceProcessed", hdrLumAdaptationProcessed->getHandle());
	fxaaPostProcessingShader->setFloat("exposure", exposure);
	fxaaPostProcessingShader->setVec2("invFullResolution", { 1.0f / MainLoop::getInstance().camera.width, 1.0f / MainLoop::getInstance().camera.height });
	renderQuad();

	//
	// Render ui
	// @NOTE: the cameraUBO changes from the normal world space camera to the UI space camera mat4's.
	// @NOTE: this change does not effect the post processing. Simply just the renderUI contents.
	//
	renderUI();

	//
	// Do bloom: breakdown-preprocessing
	//
	bool firstcopy = true;
	float downscaledFactor = 4.0f;						// NOTE: bloom starts at 1/4 the size
	for (size_t i = 0; i < bloomBufferCount / 2; i++)
	{
		for (size_t j = 0; j < 3; j++)																		// There are three stages in each pass: 1: copy, 2: horiz gauss, 3: vert gauss
		{
			size_t bloomFBOIndex = i * 2 + j % 2;					// Needs to have a sequence of i(0):  0,1,0; i(1): 2,3,2; i(2): 4,5,4; i(3): 6,7,6; i(4): 8,9,8
			size_t colorBufferIndex = i * 2 - 1 + j;				// Needs to have a sequence of i(0): -1,0,1; i(1): 1,2,3; i(2): 3,4,5; i(3): 5,6,7; i(4): 7,8,9
			GLint stageNumber = (GLint)j + 1;						// Needs to have a sequence of i(n):  1,2,3

			glBindFramebuffer(GL_FRAMEBUFFER, bloomFBOs[bloomFBOIndex]);
			glClear(GL_COLOR_BUFFER_BIT);
			bloom_postprocessing_program_id->use();
			bloom_postprocessing_program_id->setSampler("hdrColorBuffer", firstcopy ? hdrFXAAColorBuffer : bloomColorBuffers[colorBufferIndex]);
			bloom_postprocessing_program_id->setInt("stage", stageNumber);
			bloom_postprocessing_program_id->setInt("firstcopy", firstcopy);
			bloom_postprocessing_program_id->setFloat("downscaledFactor", downscaledFactor);
			renderQuad();

			firstcopy = false;
		}

		downscaledFactor *= 2.0f;
	}

	//
	// Do bloom: additive color buffer reconstruction		NOTE: the final reconstructed bloom buffer is on [1]
	//
	assert(bloomBufferCount >= 4);											// NOTE: for this algorithm to work, there must be at least 2 passes(aka 4 fbo's) so that there will be a copy into the needed texture for use in the tonemapping pass after this.
	bool firstReconstruction = true;
	downscaledFactor /= 2.0f;
	for (int i = (int)(bloomBufferCount / 2 - 2) * 2; i >= 0; i -= 2)		// NOTE: must use signed int so that it goes negative
	{
		downscaledFactor /= 2.0f;

		size_t bloomFBOIndex = i + 1;
		size_t colorBufferIndex = i;
		size_t smallerColorBufferIndex = i + 3 - (firstReconstruction ? 1 : 0);

		glBindFramebuffer(GL_FRAMEBUFFER, bloomFBOs[bloomFBOIndex]);
		glClear(GL_COLOR_BUFFER_BIT);
		bloom_postprocessing_program_id->use();
		bloom_postprocessing_program_id->setSampler("hdrColorBuffer", bloomColorBuffers[colorBufferIndex]);
		bloom_postprocessing_program_id->setSampler("smallerReconstructHDRColorBuffer", bloomColorBuffers[smallerColorBufferIndex]);
		bloom_postprocessing_program_id->setInt("stage", 4);
		bloom_postprocessing_program_id->setInt("firstcopy", firstcopy);
		bloom_postprocessing_program_id->setFloat("downscaledFactor", downscaledFactor);
		renderQuad();

		firstReconstruction = false;
	}

	//
	// Do tonemapping and post-processing
	// with the fbo and render to a quad
	//
	glViewport(0, 0, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
	glBindFramebuffer(GL_FRAMEBUFFER, doCloudDenoiseNontemporal ? hdrFBO : 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	postprocessing_program_id->use();
	postprocessing_program_id->setSampler("hdrColorBuffer", hdrFXAAColorBuffer);
	postprocessing_program_id->setSampler("bloomColorBuffer", bloomColorBuffers[1]);	// 1 is the final color buffer of the reconstructed bloom
	postprocessing_program_id->setSampler("luminanceProcessed", hdrLumAdaptationProcessed->getHandle());
	postprocessing_program_id->setSampler("volumetricLighting", volumetricTexture->getHandle());
	postprocessing_program_id->setVec3("sunLightColor", mainlight->color* mainlight->colorIntensity* volumetricLightingStrength* volumetricLightingStrengthExternal);
	postprocessing_program_id->setFloat("exposure", exposure);
	postprocessing_program_id->setFloat("bloomIntensity", bloomIntensity);
	renderQuad();

	// @HACK: Oh so hack lol hahahahaha  -Timo
	if (doCloudDenoiseNontemporal)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);		// Sorry blur FBO! Gotta use ya for this D:
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		simpleDenoiseShader->use();
		simpleDenoiseShader->setSampler("textureMap", hdrColorBuffer);
		renderQuad();

		// Arrrgh, since we can't do ping pong, we just have to blit the result over to here too :(  -Timo
		//glBlitNamedFramebuffer(
		//	cloudEffectBlurFBO,
		//	cloudEffectFBO,
		//	0, 0, cloudEffectTextureWidth, cloudEffectTextureHeight,
		//	0, 0, cloudEffectTextureWidth, cloudEffectTextureHeight,
		//	GL_COLOR_BUFFER_BIT,
		//	GL_NEAREST
		//);
	}

	// Swap the hdrLumAdaptation ping-pong textures
	std::swap(hdrLumAdaptationPrevious, hdrLumAdaptationProcessed);

#ifdef _DEVELOP
	// ImGui buffer swap
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}


void RenderManager::createCameraInfoUBO()
{
	glCreateBuffers(1, &cameraInfoUBO);
	glNamedBufferData(cameraInfoUBO, sizeof(CameraInformation), nullptr, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 3, cameraInfoUBO);
}


void RenderManager::updateCameraInfoUBO(glm::mat4 cameraProjection, glm::mat4 cameraView)
{
	cameraInfo.projection = cameraProjection;
	cameraInfo.view = cameraView;
	cameraInfo.projectionView = cameraProjection * cameraView;
	glNamedBufferSubData(cameraInfoUBO, 0, sizeof(CameraInformation), &cameraInfo);
}


void RenderManager::destroyCameraInfoUBO()
{
	glDeleteBuffers(1, &cameraInfoUBO);
}


void RenderManager::setupSceneShadows()
{
	const size_t numLights = std::min((size_t)RenderLightInformation::MAX_LIGHTS, MainLoop::getInstance().lightObjects.size());
	size_t numShadows = 0;		// NOTE: exclude the csm since it's its own EXT
	bool setupCSM = false;		// NOTE: unfortunately, I can't figure out a way to have multiple directional light csm's, so here's to just one!

	for (size_t i = 0; i < numLights; i++)
	{
		if (!MainLoop::getInstance().lightObjects[i]->castsShadows)
			continue;

		if (MainLoop::getInstance().lightObjects[i]->lightType == LightType::DIRECTIONAL)
		{
			if (!setupCSM)
			{
				ShaderExtCSM_shadow::csmShadowMap = MainLoop::getInstance().lightObjects[i]->shadowMapTexture;
				ShaderExtCSM_shadow::cascadePlaneDistances = ((DirectionalLightLight*)MainLoop::getInstance().lightObjects[i])->shadowCascadeLevels;
				ShaderExtCSM_shadow::cascadeShadowMapTexelSize = ((DirectionalLightLight*)MainLoop::getInstance().lightObjects[i])->shadowCascadeTexelSize;
				ShaderExtCSM_shadow::cascadeCount = (GLint)((DirectionalLightLight*)MainLoop::getInstance().lightObjects[i])->shadowCascadeLevels.size();
				ShaderExtCSM_shadow::nearPlane = MainLoop::getInstance().camera.zNear;
				ShaderExtCSM_shadow::farPlane = MainLoop::getInstance().lightObjects[i]->shadowFarPlane;
			}

			setupCSM = true;
		}
		else if (MainLoop::getInstance().lightObjects[i]->lightType == LightType::SPOT)
		{
			ShaderExtShadow::spotLightShadows[numShadows] = MainLoop::getInstance().lightObjects[i]->shadowMapTexture;

			numShadows++;
		}
		else if (MainLoop::getInstance().lightObjects[i]->lightType == LightType::POINT)
		{
			ShaderExtShadow::pointLightShadows[numShadows] = MainLoop::getInstance().lightObjects[i]->shadowMapTexture;
			ShaderExtShadow::pointLightShadowFarPlanes[numShadows] = ((PointLightLight*)MainLoop::getInstance().lightObjects[i])->farPlane;

			numShadows++;
		}

		// Break out early
		if (numShadows >= ShaderExtShadow::MAX_SHADOWS && setupCSM)
			break;
	}
}


void RenderManager::renderScene()
{
	if (isWireFrameMode)	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//
	// Z-PASS and RENDER QUEUE SORTING
	//
	glViewport(0, 0, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
	glBindFramebuffer(GL_FRAMEBUFFER, zPrePassFBO);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	INTERNALzPassShader->use();
	ViewFrustum cookedViewFrustum = ViewFrustum::createFrustumFromCamera(MainLoop::getInstance().camera);		// @Optimize: this can be optimized via a mat4 that just changes the initial view frustum
	
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

#ifdef _DEVELOP
	if (MainLoop::getInstance().timelineViewerMode && modelForTimelineViewer != nullptr)
	{
		// Render just the single renderObject for the animation viewer
		modelForTimelineViewer->render(&cookedViewFrustum, INTERNALzPassShader);
	}
	else
#endif
	for (unsigned int i = 0; i < MainLoop::getInstance().renderObjects.size(); i++)
	{
		// NOTE: viewfrustum culling is handled at the mesh level with some magic. Peek in if ya wanna. -Timo
		MainLoop::getInstance().renderObjects[i]->render(&cookedViewFrustum, INTERNALzPassShader);
	}

	glDisable(GL_CULL_FACE);

	//
	// Capture z-passed screen for SSAO
	//
	glViewport(0, 0, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);  // ssaoFBOSize, ssaoFBOSize);
	glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	if (!isWireFrameMode)
	{
		ssaoProgramId->use();
		ssaoProgramId->setSampler("rotationTexture", ssaoRotationTexture->getHandle());
		ssaoProgramId->setVec2("fullResolution", { MainLoop::getInstance().camera.width, MainLoop::getInstance().camera.height });
		ssaoProgramId->setVec2("invFullResolution", { 1.0f / MainLoop::getInstance().camera.width, 1.0f / MainLoop::getInstance().camera.height });
		ssaoProgramId->setFloat("cameraFOV", MainLoop::getInstance().camera.fov);
		ssaoProgramId->setFloat("zNear", MainLoop::getInstance().camera.zNear);
		ssaoProgramId->setFloat("zNear", MainLoop::getInstance().camera.zNear);
		ssaoProgramId->setFloat("zFar", MainLoop::getInstance().camera.zFar);
		ssaoProgramId->setFloat("powExponent", ssaoScale);
		ssaoProgramId->setFloat("radius", ssaoRadius);
		ssaoProgramId->setFloat("bias", ssaoBias);
		glm::vec4 projInfoPerspective = {
			2.0f / (cameraInfo.projection[0][0]),							// (x) * (R - L)/N
			2.0f / (cameraInfo.projection[1][1]),							// (y) * (T - B)/N
			-(1.0f - cameraInfo.projection[2][0]) / cameraInfo.projection[0][0],  // L/N
			-(1.0f + cameraInfo.projection[2][1]) / cameraInfo.projection[1][1],  // B/N
		};
		ssaoProgramId->setVec4("projInfo", projInfoPerspective);
		renderQuad();

		//
		// Blur SSAO pass
		//
		glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		blurXProgramId->use();
		blurXProgramId->setSampler("textureMap", ssaoTexture->getHandle());
		renderQuad();

		glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		blurYProgramId->use();
		blurYProgramId->setSampler("textureMap", ssaoBlurTexture->getHandle());
		renderQuad();
	}

	glBlitNamedFramebuffer(
		zPrePassFBO,
		hdrFBO,
		0, 0, MainLoop::getInstance().camera.width, MainLoop::getInstance().camera.height,
		0, 0, MainLoop::getInstance().camera.width, MainLoop::getInstance().camera.height,
		GL_DEPTH_BUFFER_BIT,
		GL_NEAREST
	);

	if (isWireFrameMode)	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	//
	// Generate the irradiance and prefilter maps from interpolating and spinning the prebaked ones
	//
	for (int i = (int)numSkyMaps - 1; i >= 0; i--)
	{
		if (-skyboxParams.sunOrientation.y < sinf(glm::radians(preBakedSkyMapAngles[i])))
		{
			whichMap = i;

			if (i + 1 >= numSkyMaps)
				mapInterpolationAmt = 0;
			else
			{
				mapInterpolationAmt =
					1 -
					(-skyboxParams.sunOrientation.y - sinf(glm::radians(preBakedSkyMapAngles[i + 1]))) /
					(sinf(glm::radians(preBakedSkyMapAngles[i])) - sinf(glm::radians(preBakedSkyMapAngles[i + 1])));
			}

			break;
		}
	}

	glm::vec3 flatSunOrientation = skyboxParams.sunOrientation;
	flatSunOrientation.y = 0;
	flatSunOrientation = glm::normalize(flatSunOrientation);
	sunSpinAmount = glm::toMat3(glm::quat(flatSunOrientation, glm::vec3(1, 0, 0)));

	// Render it out!
	// Irradiance map
	environmentMapMixerShader->use();		// Cubemap.vert & EnvMapMixer.frag
	environmentMapMixerShader->setMat4("projection", captureProjection);
	environmentMapMixerShader->setFloat("mapInterpolationAmt", mapInterpolationAmt);
	environmentMapMixerShader->setMat3("sunSpinAmount", sunSpinAmount);
	environmentMapMixerShader->setSampler("texture1", irradianceMap[whichMap]);
	environmentMapMixerShader->setSampler("texture2", irradianceMap[std::clamp(whichMap + 1, (size_t)0, numSkyMaps - 1)]);
	environmentMapMixerShader->setFloat("lod", 0.0f);
	glViewport(0, 0, irradianceMapSize, irradianceMapSize);
	glBindFramebuffer(GL_FRAMEBUFFER, irradianceMapInterpolatedFBO);
	for (unsigned int i = 0; i < 6; i++)
	{
		environmentMapMixerShader->setMat4("view", captureViews[i]);
		glNamedFramebufferTextureLayer(irradianceMapInterpolatedFBO, GL_COLOR_ATTACHMENT0, irradianceMapInterpolated->getHandle(), 0, i);
		glClear(GL_COLOR_BUFFER_BIT);
		renderCube();
	}

	// Prefilter map
	environmentMapMixerShader->setSampler("texture1", prefilterMap[whichMap]);
	environmentMapMixerShader->setSampler("texture2", prefilterMap[std::clamp(whichMap + 1, (size_t)0, numSkyMaps - 1)]);
	glBindFramebuffer(GL_FRAMEBUFFER, prefilterMapInterpolatedFBO);
	for (unsigned int mip = 0; mip < maxMipLevels; mip++)
	{
		environmentMapMixerShader->setFloat("lod", (float)mip);

		unsigned int mipWidth = (unsigned int)(prefilterMapSize * std::pow(0.5, mip));
		unsigned int mipHeight = (unsigned int)(prefilterMapSize * std::pow(0.5, mip));
		glViewport(0, 0, mipWidth, mipHeight);

		for (unsigned int i = 0; i < 6; i++)
		{
			environmentMapMixerShader->setMat4("view", captureViews[i]);
			glNamedFramebufferTextureLayer(prefilterMapInterpolatedFBO, GL_COLOR_ATTACHMENT0, prefilterMapInterpolated->getHandle(), mip, i);
			glClear(GL_COLOR_BUFFER_BIT);
			renderCube();
		}
	}

	//
	// Draw atmospheric scattering skybox
	//
	glViewport(0, 0, (GLsizei)skyboxLowResSize, (GLsizei)skyboxLowResSize);
	glBindFramebuffer(GL_FRAMEBUFFER, skyboxFBO);
	glClear(GL_COLOR_BUFFER_BIT);

	glDepthMask(GL_FALSE);

	skybox_program_id->use();
	skybox_program_id->setMat4("projection", cameraInfo.projection);
	skybox_program_id->setMat4("view", cameraInfo.view);
	skybox_program_id->setVec3("mainCameraPosition", MainLoop::getInstance().camera.position);
	skybox_program_id->setVec3("sunOrientation", skyboxParams.sunOrientation);
	skybox_program_id->setVec3("sunColor", skyboxParams.sunColor);
	skybox_program_id->setVec3("skyColor1", skyboxParams.skyColor1);
	skybox_program_id->setVec3("groundColor", skyboxParams.groundColor);
	skybox_program_id->setFloat("sunIntensity", skyboxParams.sunIntensity);
	skybox_program_id->setFloat("globalExposure", skyboxParams.globalExposure);
	skybox_program_id->setFloat("cloudHeight", skyboxParams.cloudHeight);
	skybox_program_id->setFloat("perlinDim", skyboxParams.perlinDim);
	skybox_program_id->setFloat("perlinTime", skyboxParams.perlinTime);
	skybox_program_id->setFloat("depthZFar", -1.0f);
	skybox_program_id->setInt("renderNight", false);
	skybox_program_id->setSampler("nightSkybox", nightSkyboxCubemap->getHandle());
	skybox_program_id->setMat3("nightSkyTransform", skyboxParams.nightSkyTransform);
	renderCube();

	//
	// Render the depth-sliced atmospheric scattering LUT
	//
	const float zSliceDistance = 32000.0f;		// Supposed to be 32km (see https://sebh.github.io/publications/egsr2020.pdf)		@NOTE: since this could get applied to clouds as well which don't follow the ZFar limit of geometry, it has a possibility of spanning past that 32km range. That's why this value isn't set to the camera's zfar.
	// @NOTE: I know that the raymarching goes out 32km, but I ended up doing the camera zFar for the geometry application. Unusual? Maybe. We'll see how it looks when applying this to the clouds as well. I think the clouds should be 32km threshold.  -Timo
	const float zSliceSize = zSliceDistance / (float)skyboxDepthSlicedLUTSize * 3.2f;  // @NOTE: IT'S SUPPOSED TO BE 32km... but I like the feel of the area getting foggier quicker. And this'll do it for ya
	glViewport(0, 0, skyboxDepthSlicedLUTSize, skyboxDepthSlicedLUTSize);
	for (size_t i = 0; i < skyboxDepthSlicedLUTSize; i++)
	{
		// @NOTE: skybox_program_id should already be in use right here.
		// Hence doing this before the blurring pass on the main background atmosphere  -Timo
		glBindFramebuffer(GL_FRAMEBUFFER, skyboxDepthSlicedLUTFBOs[i]);
		glClear(GL_COLOR_BUFFER_BIT);
		skybox_program_id->setFloat("depthZFar", zSliceSize * (float)(i + 1));
		renderCube();
	}

	//
	// Blur the atmospheric scattering skybox (first one)
	//
	glViewport(0, 0, (GLsizei)skyboxLowResSize, (GLsizei)skyboxLowResSize);
	glBindFramebuffer(GL_FRAMEBUFFER, skyboxBlurFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	blurX3ProgramId->use();
	blurX3ProgramId->setSampler("textureMap", skyboxLowResTexture->getHandle());
	renderQuad();

	glBindFramebuffer(GL_FRAMEBUFFER, skyboxFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	blurY3ProgramId->use();
	blurY3ProgramId->setSampler("textureMap", skyboxLowResBlurTexture->getHandle());
	renderQuad();

	//
	// Render the detailed skybox separately (sun and nighttime mainly)
	//
	glViewport(0, 0, MainLoop::getInstance().camera.width, MainLoop::getInstance().camera.height);
	glBindFramebuffer(GL_FRAMEBUFFER, skyboxDetailsSSFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	skyboxDetailsShader->use();
	skyboxDetailsShader->setMat4("projection", cameraInfo.projection);
	skyboxDetailsShader->setMat4("view", cameraInfo.view);
	skyboxDetailsShader->setVec3("mainCameraPosition", MainLoop::getInstance().camera.position);
	skyboxDetailsShader->setVec3("sunOrientation", skyboxParams.sunOrientation);
	skyboxDetailsShader->setFloat("sunRadius", skyboxParams.sunRadius);
	skyboxDetailsShader->setFloat("sunAlpha", skyboxParams.sunAlpha);
	skyboxDetailsShader->setSampler("nightSkybox", nightSkyboxCubemap->getHandle());
	skyboxDetailsShader->setMat3("nightSkyTransform", skyboxParams.nightSkyTransform);
	renderCube();


	//
	// Capture z-passed screen for @Clouds
	// @NOTE: I don't know if this render step should be inside of renderscene() or around where the volumetric lighting occurs...  -Timo
	// 
	// 
	// @TODO: SO ABOUT THESE @CLOUDS
	//			I think that the best solution is (1) seeing if there is a way to make the bottle opaque
	// (this is the only transparent thing that will probs hinder the experience if it's not faked.
	//
	// @TODO: ACTUALLY, I HAVE A REAL IDEA!!!! ABOUT @CLOUDS
	// 
	// 			So hear me out: basically it's an extra channel for the clouds where it shows the depth
	// of the cloud, and then the cloud will be drawn before any opaque objects are drawn (right after
	// the skybox, so clouds are not post-processing, laid over everything anymore).
	//			
	//			This extra channel's depth indication will just have a simple function that things behind
	// it depth-wise will disappear depending on e^-d and maybe d:=that depth function? NOOOOOO, actually it
	// should be some kind of other value, such as the area's alpha channel or something like that.
	// 
	//			So essentially the same stuff, however, how the stuff blends into the cloud will change.
	// We'll do a render step where we take the depth map of the clouds and then compare it with the depth
	// value generated after all opaque and transparent objects are rendered. Then, in a postprocessing step
	// we compare the cloud's depth with the finished depth map and fade out stuff with that. For even more
	// detail, maybe I could put the depth map of the transparent objects separate from the depth map of the
	// opaque stuff and do a calculation from there. This could be bad, however. Or, we could just do the
	// fading postprocessing step right after the opaque render queue and then right after do transparents.
	// When we're working with transparent materials, however, the fade out parameter is done right there in
	// the shader. Well, after thinking about that, we probs can just do the fadeout right then and there.
	// 
	// Or maybe not. Maybe not mess with alpha channel stuff, but rather just do a mix() between the cloud
	// color behind the object and the fadeout value. If that is the case, I guess we'll just have to have
	// the clouds not pay attention to the depth buffer from the z prepass???? Idk man.
	// 
	// 
	// 
	// @TODO: So here are my thoughts about the "REAL IDEA"
	// 
	//			It's gonna be hard to assign a depth value with this effect bc of the dithering.
	// I'm thinking that a simple flood-fill algorithm (probs 2x2) will be good enough for the effect.
	// We'll just do a test where if the 2x2 grid contains more than 50% close depth values, then we just sample
	// the min (closest) depth in there and call it a day. See RenderDoc if you wanna make this more accurate yo.  -Timo
	//
	//
	static const GLuint attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glNamedFramebufferDrawBuffers(cloudEffectFBO, 2, attachments);

	// One time random offsets for @TAA (@POC)
	static std::random_device randomDevice;
	static std::mt19937 randomEngine(randomDevice());
	static std::uniform_real_distribution<> distribution(0.0, 1.0);
	static std::vector<glm::vec3> cameraPosOffsets = {
		glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine)),
		glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine)),
		glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine)),
		glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine)),
		glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine)),
		glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine)),
		glm::vec3(distribution(randomEngine), distribution(randomEngine), distribution(randomEngine)),
	};
	static size_t cameraPosOffsetIndex = 0;
	cameraPosOffsetIndex = (cameraPosOffsetIndex + 1) % cameraPosOffsets.size();
	
	// Offset the offset index in the shader!! @POC
	static int raymarchOffsetDitherIndexOffset = 0;
	raymarchOffsetDitherIndexOffset = doCloudHistoryTAA ? (raymarchOffsetDitherIndexOffset + 1) % 16 : 0;	// @NOTE: there are 16 values in the dither algorithm

	// Draw cloud screen space effect! @CLOUDS
	glViewport(0, 0, (GLsizei)cloudEffectTextureWidth, (GLsizei)cloudEffectTextureHeight);
	glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	cloudEffectShader->use();
	cloudEffectShader->setMat4("inverseProjectionMatrix", glm::inverse(cameraInfo.projection));
	cloudEffectShader->setMat4("inverseViewMatrix", glm::inverse(cameraInfo.view));
	cloudEffectShader->setVec3("mainCameraPosition", MainLoop::getInstance().camera.position + (doCloudHistoryTAA ? cameraPosOffsets[cameraPosOffsetIndex] * cloudEffectInfo.cameraPosJitterScale : glm::vec3(0.0f)));  // @TAA @POC
	cloudEffectShader->setFloat("mainCameraZNear", MainLoop::getInstance().camera.zNear);
	cloudEffectShader->setFloat("mainCameraZFar", MainLoop::getInstance().camera.zFar);
	cloudEffectShader->setFloat("cloudLayerY", cloudEffectInfo.cloudLayerY);
	cloudEffectShader->setFloat("cloudLayerThickness", cloudEffectInfo.cloudLayerThickness);
	cloudEffectShader->setFloat("cloudNoiseMainSize", cloudEffectInfo.cloudNoiseMainSize);
	cloudEffectShader->setFloat("cloudNoiseDetailSize", cloudEffectInfo.cloudNoiseDetailSize);
	cloudEffectShader->setVec3("cloudNoiseDetailOffset", cloudEffectInfo.cloudNoiseDetailOffset);
	cloudEffectShader->setFloat("densityOffsetInner", cloudEffectInfo.densityOffsetInner);
	cloudEffectShader->setFloat("densityOffsetOuter", cloudEffectInfo.densityOffsetOuter);
	cloudEffectShader->setFloat("densityOffsetChangeRadius", cloudEffectInfo.densityOffsetChangeRadius);
	cloudEffectShader->setFloat("densityMultiplier", cloudEffectInfo.densityMultiplier);
	cloudEffectShader->setFloat("densityRequirement", cloudEffectInfo.densityRequirement);
	cloudEffectShader->setFloat("ambientDensity", cloudEffectInfo.darknessThreshold);		// @NOTE: play around with this value. (I think 0.4 works well at daytime  -Timo) (Ambient Density in cloud_effect_screenspace.frag)
	cloudEffectShader->setFloat("irradianceStrength", cloudEffectInfo.irradianceStrength);
	cloudEffectShader->setFloat("lightAbsorptionTowardsSun", cloudEffectInfo.lightAbsorptionTowardsSun);
	cloudEffectShader->setFloat("lightAbsorptionThroughCloud", cloudEffectInfo.lightAbsorptionThroughCloud);
	cloudEffectShader->setSampler("cloudNoiseTexture", cloudNoise1->getHandle());
	cloudEffectShader->setSampler("cloudNoiseDetailTexture", cloudNoise2->getHandle());
	cloudEffectShader->setFloat("raymarchOffset", cloudEffectInfo.raymarchOffset);
	cloudEffectShader->setInt("raymarchOffsetDitherIndexOffset", raymarchOffsetDitherIndexOffset);		// @TAA @POC
	cloudEffectShader->setSampler("atmosphericScattering", skyboxDepthSlicedLUT->getHandle());
	cloudEffectShader->setFloat("cloudMaxDepth", zSliceDistance);
	cloudEffectShader->setVec2("raymarchCascadeLevels", cloudEffectInfo.raymarchCascadeLevels);
	cloudEffectShader->setFloat("farRaymarchStepsizeMultiplier", cloudEffectInfo.farRaymarchStepsizeMultiplier);
	//cloudEffectShader->setFloat("maxRaymarchLength", cloudEffectInfo.maxRaymarchLength);
	cloudEffectShader->setVec3("lightColor", sunColorForClouds);
	cloudEffectShader->setVec3("lightDirection", skyboxParams.sunOrientation);
	cloudEffectShader->setVec4("phaseParameters", cloudEffectInfo.phaseParameters);
	renderQuad();

	glNamedFramebufferDrawBuffers(cloudEffectFBO, 1, attachments);

	cloudEffectInfo.cloudNoiseDetailOffset += cloudEffectInfo.cloudNoiseDetailVelocity * MainLoop::getInstance().deltaTime;

	//
	// @Clouds color floodfill pass
	//
	if (!doCloudHistoryTAA && doCloudColorFloodFill)		// @NOTE: cloud @TAA must be disabled
	{
		glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectBlurFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		cloudEffectColorFloodFillShaderX->use();
		cloudEffectColorFloodFillShaderX->setSampler("textureMap", cloudEffectTexture->getHandle());
		renderQuad();

		glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		cloudEffectColorFloodFillShaderY->use();
		cloudEffectColorFloodFillShaderY->setSampler("textureMap", cloudEffectBlurTexture->getHandle());
		renderQuad();
	}

	//
	// Blur @Clouds pass
	//
	if (cloudEffectInfo.doBlurPass)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectBlurFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		blurX3ProgramId->use();
		blurX3ProgramId->setSampler("textureMap", cloudEffectTexture->getHandle());
		renderQuad();

		glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectFBO);
		glClear(GL_COLOR_BUFFER_BIT);
		blurY3ProgramId->use();
		blurY3ProgramId->setSampler("textureMap", cloudEffectBlurTexture->getHandle());
		renderQuad();
	}

	//
	// Floodfill @Clouds depth pass
	//
	glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectDepthFloodFillXFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	cloudEffectFloodFillShaderX->use();
	cloudEffectFloodFillShaderX->setSampler("cloudEffectDepthBuffer", cloudEffectDepthTexture->getHandle());
	renderQuad();

	glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectDepthFloodFillYFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	cloudEffectFloodFillShaderY->use();
	cloudEffectFloodFillShaderY->setSampler("cloudEffectDepthBuffer", cloudEffectDepthTextureFloodFill->getHandle());
	renderQuad();

	if (doCloudHistoryTAA)
	{
		//
		// @TAA @POC Resolve and Copy history of TAA for @Clouds
		//
		// @NOTE: so about this @TAA system, seems like it adds about 0.45ms so far to the gpu render time... that's kinda slow eh? Idk really.
		// @NOTE: Based off: https://sugulee.wordpress.com/2021/06/21/temporal-anti-aliasingtaa-tutorial/
		//
		static glm::mat4 prevCameraProjectionView = cameraInfo.projectionView;
		static glm::vec3 prevCameraPosition = MainLoop::getInstance().camera.position;
		const glm::vec3 deltaCameraPosition = MainLoop::getInstance().camera.position - prevCameraPosition;
		glBindFramebuffer(GL_FRAMEBUFFER, cloudEffectBlurFBO);		// Sorry blur FBO! Gotta use ya for this D:
		glClear(GL_COLOR_BUFFER_BIT);
		cloudEffectTAAHistoryShader->use();
		cloudEffectTAAHistoryShader->setSampler("cloudEffectBuffer", cloudEffectTexture->getHandle());
		cloudEffectTAAHistoryShader->setSampler("cloudEffectHistoryBuffer", cloudEffectTAAHistoryTexture->getHandle());
		cloudEffectTAAHistoryShader->setSampler("cloudEffectDepthBuffer", cloudEffectDepthTexture->getHandle());
		cloudEffectTAAHistoryShader->setVec2("invFullResolution", { 1.0f / (float)cloudEffectTextureWidth, 1.0f / (float)cloudEffectTextureHeight });
		cloudEffectTAAHistoryShader->setFloat("cameraZNear", MainLoop::getInstance().camera.zNear);
		cloudEffectTAAHistoryShader->setFloat("cameraZFar", MainLoop::getInstance().camera.zFar);
		cloudEffectTAAHistoryShader->setVec3("cameraDeltaPosition", deltaCameraPosition);
		cloudEffectTAAHistoryShader->setMat4("currentInverseCameraProjection", glm::inverse(cameraInfo.projection));
		cloudEffectTAAHistoryShader->setMat4("currentInverseCameraView", glm::inverse(cameraInfo.view));
		cloudEffectTAAHistoryShader->setMat4("prevCameraProjectionView", prevCameraProjectionView);
		renderQuad();

		//std::cout << deltaCameraPosition.x << ",\t" << deltaCameraPosition.y << ",\t" << deltaCameraPosition.z << std::endl;

		prevCameraProjectionView = cameraInfo.projectionView;
		prevCameraPosition = MainLoop::getInstance().camera.position;

		// Copy the resolved buffer to the history buffer
		glBlitNamedFramebuffer(
			cloudEffectBlurFBO,
			cloudEffectTAAHistoryFBO,
			0, 0, cloudEffectTextureWidth, cloudEffectTextureHeight,
			0, 0, cloudEffectTextureWidth, cloudEffectTextureHeight,
			GL_COLOR_BUFFER_BIT,
			GL_NEAREST
		);

		// Arrrgh, since we can't do ping pong, we just have to blit the result over to here too :(  -Timo
		glBlitNamedFramebuffer(
			cloudEffectBlurFBO,
			cloudEffectFBO,
			0, 0, cloudEffectTextureWidth, cloudEffectTextureHeight,
			0, 0, cloudEffectTextureWidth, cloudEffectTextureHeight,
			GL_COLOR_BUFFER_BIT,
			GL_NEAREST
		);
	}

	ShaderExtCloud_effect::mainCameraPosition = MainLoop::getInstance().camera.position;

	//
	// Apply the @Clouds layer
	//
	glViewport(0, 0, MainLoop::getInstance().camera.width, MainLoop::getInstance().camera.height);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	cloudEffectApplyShader->use();
	cloudEffectApplyShader->setSampler("skyboxTexture", skyboxLowResTexture->getHandle());
	cloudEffectApplyShader->setSampler("skyboxDetailTexture", skyboxDetailsSS->getHandle());
	cloudEffectApplyShader->setSampler("cloudEffect", cloudEffectTexture->getHandle());
	renderQuad();

	glDepthMask(GL_TRUE);

	if (isWireFrameMode)	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//
	// OPAQUE RENDER QUEUE
	//
	this->repopulateAnimationUBO = true;	// Reset materials at the start of every frame

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	glDepthFunc(GL_EQUAL);		// NOTE: this is so that the Z prepass gets used and only fragments that are actually visible will get rendered
	for (size_t i = 0; i < opaqueRQ.meshesToRender.size(); i++)
	{
		opaqueRQ.meshesToRender[i]->render(opaqueRQ.modelMatrices[i], 0, opaqueRQ.boneMatrixMemAddrs[i], RenderStage::OPAQUE_RENDER_QUEUE);
	}
	opaqueRQ.meshesToRender.clear();
	opaqueRQ.modelMatrices.clear();
	opaqueRQ.boneMatrixMemAddrs.clear();
	glDepthFunc(GL_LEQUAL);

	//
	// TEXT RENDER QUEUE
	// @NOTE: there is no frustum culling for text right now... Probs need its own AABB's like models
	//
	glEnable(GL_BLEND);
	for (size_t i = 0; i < textRQ.textRenderers.size(); i++)
	{
		TextRenderer& tr = *textRQ.textRenderers[i];
		renderText(tr);
	}

	//////////
	////////// @TEMP: @REFACTOR: try and render a CLOUD!!@!
	//////////
	////////static Shader* cloudShader = (Shader*)Resources::getResource("shader;cloud_billboard");
	////////static Texture* posVolumeTexture = (Texture*)Resources::getResource("texture;cloudTestPos");
	////////static Texture* negVolumeTexture = (Texture*)Resources::getResource("texture;cloudTestNeg");
	////////cloudShader->use();
	////////cloudShader->setMat4("modelMatrix", glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0) - MainLoop::getInstance().camera.position) * glm::inverse(cameraInfo.view));
	////////cloudShader->setVec3("mainLightDirectionVS", glm::mat3(cameraInfo.view) * skyboxParams.sunOrientation);
	////////cloudShader->setVec3("mainLightColor", sunColorForClouds);
	////////cloudShader->setSampler("posVolumeTexture", posVolumeTexture->getHandle());
	////////cloudShader->setSampler("negVolumeTexture", negVolumeTexture->getHandle());
	////////renderQuad();

	//
	// TRANSPARENT RENDER QUEUE
	//
	this->repopulateAnimationUBO = true;	// @NOTE: For @Intel GPUs (and likely AMD too), the UBO needs to be repopulated at the beginning of the transparent render queue or else one thing lacks the skeletal animation weirdly  -Timo

	std::sort(
		transparentRQ.commandingIndices.begin(),
		transparentRQ.commandingIndices.end(),
		[this](const size_t& index1, const size_t& index2)
		{
			const float depthPriority1 = transparentRQ.meshesToRender[index1]->getDepthPriority();
			const float depthPriority2 = transparentRQ.meshesToRender[index2]->getDepthPriority();

			if (depthPriority1 != depthPriority2)
				return depthPriority1 > depthPriority2;
			return transparentRQ.distancesToCamera[index1] > transparentRQ.distancesToCamera[index2];
		}
	);

	for (size_t& index : transparentRQ.commandingIndices)
	{
		transparentRQ.meshesToRender[index]->render(transparentRQ.modelMatrices[index], 0, transparentRQ.boneMatrixMemAddrs[index], RenderStage::TRANSPARENT_RENDER_QUEUE);
	}
	transparentRQ.commandingIndices.clear();
	transparentRQ.meshesToRender.clear();
	transparentRQ.modelMatrices.clear();
	transparentRQ.boneMatrixMemAddrs.clear();
	transparentRQ.distancesToCamera.clear();

	glDisable(GL_BLEND);

	// End of main render queues: turn off face culling		@TEMP: for transparent render queue
	glDisable(GL_CULL_FACE);

	if (isWireFrameMode)	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

#ifdef _DEVELOP
	//
	// @DEBUG: show grid for imguizmo
	//
	static bool imguizmoIsUsingPrevious = false;

	if (ImGuizmo::IsUsing() && !selectedObjectIndices.empty())
	{
		static bool showXGrid = false;
		static bool showYGrid = false;
		static bool showZGrid = false;

		//
		// See which grid to show
		// NOTE: you do NOT wanna see the jank I had to create inside of ImGuizmo.h and ImGuizmo.cpp to make this work. NG NG
		//
		if (!imguizmoIsUsingPrevious)
		{
			// Reset
			showXGrid = false;
			showYGrid = false;
			showZGrid = false;

			// Test what movetype
			ImGuizmo::vec_t gizmoHitProportion;
			int type = ImGuizmo::GetMoveType(transOperation, &gizmoHitProportion);
			if (type == ImGuizmo::MOVETYPE::MT_NONE)
			{
			}
			else if (type == ImGuizmo::MOVETYPE::MT_MOVE_X)
			{
				showYGrid = true;
				showZGrid = true;
			}
			else if (type == ImGuizmo::MOVETYPE::MT_MOVE_Y)
			{
				showXGrid = true;
				showZGrid = true;
			}
			else if (type == ImGuizmo::MOVETYPE::MT_MOVE_Z)
			{
				showXGrid = true;
				showYGrid = true;
			}
			else if (type == ImGuizmo::MOVETYPE::MT_MOVE_YZ)
			{
				showXGrid = true;
			}
			else if (type == ImGuizmo::MOVETYPE::MT_MOVE_ZX)
			{
				showYGrid = true;
			}
			else if (type == ImGuizmo::MOVETYPE::MT_MOVE_XY)
			{
				showZGrid = true;
			}
			else if (type == ImGuizmo::MOVETYPE::MT_MOVE_SCREEN)
			{
				showXGrid = true;
				showYGrid = true;
				showZGrid = true;
			}
		}

		//
		// Draw the grids!!!
		//
		glm::mat4 position = glm::translate(glm::mat4(1.0f), INTERNALselectionSystemAveragePosition);
		glm::mat4 rotation = glm::toMat4(INTERNALselectionSystemLatestOrientation);
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), { 100, 100, 100 });
		LvlGridMaterial* gridMaterial = (LvlGridMaterial*)Resources::getResource("material;lvlGridMaterial");

		if (showZGrid)
		{
			gridMaterial->setColor(glm::vec3(0.1, 0.1, 1) * 5);
			gridMaterial->applyTextureUniforms();
			gridMaterial->getShader()->setMat4("modelMatrix", position * rotation * scale);
			renderQuad();
		}

		if (showYGrid)
		{
			gridMaterial->setColor(glm::vec3(0.1, 1, 0.1) * 5);
			gridMaterial->applyTextureUniforms();
			glm::mat4 xRotate = glm::toMat4(glm::quat(glm::radians(glm::vec3(90, 0, 0))));
			gridMaterial->getShader()->setMat4("modelMatrix", position * rotation * xRotate * scale);
			renderQuad();
		}

		if (showXGrid)
		{
			gridMaterial->setColor(glm::vec3(1, 0.1, 0.1) * 5);
			gridMaterial->applyTextureUniforms();
			glm::mat4 zRotate = glm::toMat4(glm::quat(glm::radians(glm::vec3(0, 90, 0))));
			gridMaterial->getShader()->setMat4("modelMatrix", position * rotation * zRotate * scale);
			renderQuad();
		}
	}

	imguizmoIsUsingPrevious = ImGuizmo::IsUsing();

	//
	// @DEBUG: Draw grid for timelineviewermode
	//
	if (MainLoop::getInstance().timelineViewerMode)
	{
		glm::mat4 xRotate = glm::toMat4(glm::quat(glm::radians(glm::vec3(90, 0, 0))));
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), { 100, 100, 100 });

		LvlGridMaterial* gridMaterial = (LvlGridMaterial*)Resources::getResource("material;lvlGridMaterial");
		gridMaterial->setColor(glm::vec3(0.1, 0.1, 0.1) * 5);
		gridMaterial->applyTextureUniforms();
		gridMaterial->getShader()->setMat4("modelMatrix", xRotate * scale);
		renderQuad();
	}
#endif

	//
	// @DEBUG: Draw the shadowmaps on a quad on top of everything else
	//
	if (showShadowMapView)
	{
		debug_csm_program_id->use();
		debug_csm_program_id->setInt("layer", debugCSMLayerNum);
		debug_csm_program_id->setSampler("depthMap", MainLoop::getInstance().lightObjects[0]->shadowMapTexture);
		renderQuad();
	}

	if (showCloudNoiseView)
	{
		debug_cloud_noise_program_id->use();
		debug_cloud_noise_program_id->setFloat("layer", debugCloudNoiseLayerNum);
		debug_cloud_noise_program_id->setSampler("noiseMap", cloudNoise1->getHandle());  // cloudNoise1Channels[debugCloudNoiseChannel]->getHandle());
		renderQuad();
	}
}


void RenderManager::INTERNALaddMeshToOpaqueRenderQueue(Mesh* mesh, const glm::mat4& modelMatrix, const std::vector<glm::mat4>* boneTransforms)
{
	opaqueRQ.meshesToRender.push_back(mesh);
	opaqueRQ.modelMatrices.push_back(modelMatrix);
	opaqueRQ.boneMatrixMemAddrs.push_back(boneTransforms);
}

void RenderManager::INTERNALaddMeshToTransparentRenderQueue(Mesh* mesh, const glm::mat4& modelMatrix, const std::vector<glm::mat4>* boneTransforms)
{
	transparentRQ.commandingIndices.push_back(transparentRQ.meshesToRender.size());
	transparentRQ.meshesToRender.push_back(mesh);
	transparentRQ.modelMatrices.push_back(modelMatrix);
	transparentRQ.boneMatrixMemAddrs.push_back(boneTransforms);

	float distanceToCamera = (cameraInfo.projectionView * modelMatrix * glm::vec4(mesh->getCenterOfGravity(), 1.0f)).z;
	transparentRQ.distancesToCamera.push_back(distanceToCamera);
}

void RenderManager::renderSceneShadowPass(Shader* shader)
{
#ifdef _DEVELOP
	if (MainLoop::getInstance().timelineViewerMode && modelForTimelineViewer != nullptr)
	{
		// Render just the single selected object when in timelineviewermode
		modelForTimelineViewer->renderShadow(shader);
	}
	else
#endif
	//
	// Render everything
	//
	for (unsigned int i = 0; i < MainLoop::getInstance().renderObjects.size(); i++)
	{
		MainLoop::getInstance().renderObjects[i]->renderShadow(shader);
	}
}


void RenderManager::renderUI()
{
	const float camWidth = MainLoop::getInstance().camera.width;
	const float camHeight = MainLoop::getInstance().camera.height;

	if (camWidth == 0.0f || camHeight == 0.0f)
		return;

	const float referenceScreenHeight = 500.0f;
	const float cameraAspectRatio = camWidth / camHeight;
	const glm::mat4 viewMatrix =
		glm::ortho(
			-referenceScreenHeight * cameraAspectRatio / 2.0f,
			referenceScreenHeight * cameraAspectRatio / 2.0f,
			-referenceScreenHeight / 2.0f,
			referenceScreenHeight / 2.0f,
			-1.0f,
			1.0f
		);
	updateCameraInfoUBO(viewMatrix, glm::mat4(1.0f));

	glDepthMask(GL_FALSE);

	//
	// Update UI Stamina Bar
	//
	{
		GameState::getInstance().updateStaminaDepletionChaser(MainLoop::getInstance().deltaTime);
	}

	//
	// Render Message boxes UI
	//
	{
		glBindFramebuffer(GL_FRAMEBUFFER, hdrFXAAFBO);		// @NOTE: Switched from hdrFBO bc the fxaa postprocessing step happens right before this, so kinda need to do this at this time.
		glViewport(0, 0, camWidth, camHeight);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_BLEND);

		notificationUIProgramId->setFloat("padding", notifPadding);
		notificationUIProgramId->setVec2("extents", notifExtents);
		notificationUIProgramId->setVec3("color1", notifColor1);
		notificationUIProgramId->setVec3("color2", notifColor2);

		glm::vec2 currentPosition = notifPosition;
		for (int i = (int)notifHoldTimers.size() - 1; i >= 0; i--)
		{
			float& timer = notifHoldTimers[i];
			std::string& message = notifMessages[i];

			timer += MainLoop::getInstance().deltaTime;

			float scale = 0.0f;
			if (timer < notifAnimTime)
			{
				// Lead in
				scale = timer / notifAnimTime;
				scale = glm::pow(1 - scale, 4.0f);
			}
			else if (timer < notifAnimTime + notifHoldTime)
			{
				// Hold
				scale = 0.0f;
			}
			else
			{
				// Ease out
				scale = (timer - notifAnimTime - notifHoldTime) / notifAnimTime;
				scale = glm::pow(scale, 4.0f);
			}

			notificationUIProgramId->setFloat("fadeAlpha", (1.0f - scale) * 0.5f);

			const glm::vec3 translation(currentPosition + notifHidingOffset * scale, 0.0f);
			const glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), translation) * glm::scale(glm::mat4(1.0f), glm::vec3(notifExtents, 1.0f));
			notificationUIProgramId->setMat4("modelMatrix", modelMatrix);
			notificationUIProgramId->use();		// @NOTE: the reason why this is here instead of somewhere else is bc when renderText() gets called, the shader changes, so I need to make sure to change it back to this shader for every box that needs to get rendered  -Timo
			renderQuad();

			TextRenderer tr =
			{
				message,
				glm::translate(glm::mat4(1.0f), translation) * glm::scale(glm::mat4(1.0f), glm::vec3(notifMessageSize)),
				notifColor2,
				TextAlignment::CENTER,
				TextAlignment::CENTER
			};
			renderText(tr);

			currentPosition += notifAdvance * (1.0f - scale);

			// Delete stale messages
			if (notifHoldTimers[i] > notifAnimTime * 2.0f + notifHoldTime)
			{
				// DELETE!
				notifHoldTimers.erase(notifHoldTimers.begin() + i);
				notifMessages.erase(notifMessages.begin() + i);
			}
		}

		glDisable(GL_BLEND);
	}

	glDepthMask(GL_TRUE);
}


#ifdef _DEVELOP
void RenderManager::renderImGuiPass()
{
	renderImGuiContents();

	// Render Imgui
	ImGui::Render();

	// Update and Render additional Platform Windows for Imgui
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		GLFWwindow* backup_current_context = glfwGetCurrentContext();

		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		glfwMakeContextCurrent(backup_current_context);
	}
}


void routinePurgeTimelineViewerModel()
{
	delete modelForTimelineViewer->baseObject;
	delete modelForTimelineViewer;
	modelForTimelineViewer = nullptr;
	delete animatorForModelForTimelineViewer;
	animatorForModelForTimelineViewer = nullptr;
}


void routineCreateAndInsertInTheModel(const char* modelMetadataPath, nlohmann::json& modelMetadataData, bool setTimestampSpeedTo1)
{
	// Reset state (@WARNING: don't do anything stupid and leave hanging pointers bc of this yo)	@NOTE: with this, editor values don't have to be explicitly updated here... they will simply just be reset. Also, lists that contain references will just get cleared automagically.
	timelineViewerState = TimelineViewerState();

	//
	// Create and insert in the model
	//
	modelMetadataFname = modelMetadataPath;

	modelForTimelineViewer = new RenderComponent(new DummyBaseObject());

	// Setup importing the animations @COPYPASTA (Resources.cpp)
	std::vector<AnimationMetadata> animationsToInclude;
	if (modelMetadataData.contains("included_anims"))
	{
		std::vector<std::string> animationNames = modelMetadataData["included_anims"];
		for (size_t i = 0; i < animationNames.size(); i++)
		{
			std::string animationName = animationNames[i];
			bool trackXZRootMotion = false;
			float timestampSpeed = 1.0f;
			if (modelMetadataData.contains("animation_details") && modelMetadataData["animation_details"].contains(animationName))
			{
				if (modelMetadataData["animation_details"][animationName].contains("track_xz_root_motion"))
					trackXZRootMotion = modelMetadataData["animation_details"][animationName]["track_xz_root_motion"];

				// @NOTE: In the case where setTimestampSpeedTo1==true, the animationSpeed is 1.0f bc the anim previewer player
				// will use its own value. The animationSpeed value should get imported when the animations would get imported normally.  -Timo
				if (!setTimestampSpeedTo1)
				{
					if (modelMetadataData["animation_details"][animationName].contains("timestamp_speed"))
						timestampSpeed = modelMetadataData["animation_details"][animationName]["timestamp_speed"];
				}
			}

			animationsToInclude.push_back({ animationNames[i], trackXZRootMotion, timestampSpeed });
		}
	}

	Model* model = new Model(std::string(modelMetadataData["model_path"]).c_str(), animationsToInclude);		// @NOTE: direct loading of the model is important, since we don't want the resources system to cache it.
	animatorForModelForTimelineViewer = nullptr;
	if (animationsToInclude.size() > 0)
		animatorForModelForTimelineViewer = new Animator(&model->getAnimations());
	ModelWithMetadata mwm = {
		model,
		true,
		animatorForModelForTimelineViewer
	};
	modelForTimelineViewer->addModelToRender(mwm);

	// Setup importing the material paths (and load those materials at the same time)
	// @NOTE: need to import the animations and the model before touching the materials
	// @COPYPASTA (Resources.cpp)
	if (modelMetadataData.contains("material_paths"))
	{
		nlohmann::json& materialPathsJ = modelMetadataData["material_paths"];
		for (auto& [key, val] : materialPathsJ.items())
		{
			std::string materialPath = std::string(val);
			timelineViewerState.materialPathsMap[key] = materialPath;

			// Load in the material too @COPYPASTA
			try
			{
				//
				// Load in the new material
				//
				Material* materialToAssign = (Material*)Resources::getResource("material;" + materialPath);	// @NOTE: this line should fail if the material isn't inputted correctly.
				if (materialToAssign == nullptr)
					throw nullptr;		// Just get it to the catch statement

				std::map<std::string, Material*> materialAssignmentMap;
				materialAssignmentMap[key] = materialToAssign;
				modelForTimelineViewer->getModelFromIndex(0).model->setMaterials(materialAssignmentMap);	// You'll get a butt ton of errors since there's only one material name in here, but who cares eh  -Timo

				// Assign to the timelineViewerState
				timelineViewerState.materialPathsMap[key] = materialPath;
			}
			catch (...)
			{
				// Abort applying the material name and show an error message.  @HACK: Bad patterns
			}
		}
	}

	//
	// Load in the state

	// Animation Names and Inclusions in model
	std::vector<Animation> modelAnimations = modelForTimelineViewer->getModelFromIndex(0).model->getAnimations();
	std::vector<std::string> animationNameList = modelForTimelineViewer->getModelFromIndex(0).model->getAnimationNameList();
	for (size_t i = 0; i < animationNameList.size(); i++)
	{
		std::string name = animationNameList[i];
		bool included = false;
		for (size_t j = 0; j < modelAnimations.size(); j++)
			if (name == modelAnimations[j].getName())
			{
				included = true;
				break;
			}
		timelineViewerState.animationNameAndIncluded.push_back({ name, included });
	}

	// Get animation details
	if (modelMetadataData.contains("animation_details"))
	{
		for (size_t i = 0; i < timelineViewerState.animationNameAndIncluded.size(); i++)
		{
			AnimationNameAndIncluded& anai = timelineViewerState.animationNameAndIncluded[i];
			if (!anai.included)
				continue;

			AnimationDetail newAnimationDetail;
			if (modelMetadataData["animation_details"].contains(anai.name))
			{
				nlohmann::json animationDetails = modelMetadataData["animation_details"][anai.name];

				if (animationDetails.contains("track_xz_root_motion"))
					newAnimationDetail.trackXZRootMotion = animationDetails["track_xz_root_motion"];
				if (animationDetails.contains("timestamp_speed"))
					newAnimationDetail.timestampSpeed = animationDetails["timestamp_speed"];
			}
			timelineViewerState.animationDetailMap[anai.name] = newAnimationDetail;		// @NOTE: this adds an empty newAnimationDetail if it wasn't found inside of the json file
		}
	}

	// Get Animation State Machine
	if (modelMetadataData.contains("animation_state_machine"))
	{
		nlohmann::json& animStateMachine = modelMetadataData["animation_state_machine"];
		if (animStateMachine.contains("asm_variables"))
		{
			for (auto asmVar_j : animStateMachine["asm_variables"])
			{
				ASMVariable asmVar;
				asmVar.varName = asmVar_j["var_name"];
				asmVar.variableType = (ASMVariable::ASMVariableType)(int)asmVar_j["variable_type"];
				asmVar.value = asmVar_j["value"];
				timelineViewerState.animationStateMachineVariables.push_back(asmVar);
			}
		}

		if (animStateMachine.contains("asm_nodes"))
		{
			std::vector<std::string> allNodeNames;

			for (auto asmNode_j : animStateMachine["asm_nodes"])
			{
				ASMNode asmNode;
				asmNode.nodeName = asmNode_j["node_name"];
				asmNode.animationName1 = asmNode_j["animation_name_1"];
				asmNode.animationName2 = asmNode_j["animation_name_2"];
				asmNode.varFloatBlend = asmNode_j["animation_blend_variable"];		// Yes, this is supposed to be a string
				asmNode.animationBlend1 = asmNode_j["animation_blend_boundary_1"];
				asmNode.animationBlend2 = asmNode_j["animation_blend_boundary_2"];
				asmNode.loopAnimation = asmNode_j["loop_animation"];
				asmNode.doNotTransitionUntilAnimationFinished = asmNode_j["dont_transition_until_animation_finished"];
				asmNode.transitionTime = asmNode_j["transition_time"];
				
				if (asmNode_j.contains("node_transition_condition_groups"))
				{
					for (auto asmTranConditionGroup_j : asmNode_j["node_transition_condition_groups"])
					{
						asmNode.transitionConditionGroups.push_back({});
						size_t currentGroupIndex = asmNode.transitionConditionGroups.size() - 1;
						for (auto asmTranCondition_j : asmTranConditionGroup_j)
						{
							ASMTransitionCondition asmTranCondition;
							asmTranCondition.varName = asmTranCondition_j["var_name"];
							asmTranCondition.comparisonOperator = (ASMTransitionCondition::ASMComparisonOperator)(int)asmTranCondition_j["comparison_operator"];

							if (asmTranCondition.varName == ASMTransitionCondition::specialCaseKey)
							{
								for (auto asmTranConditionNodeNameRef_j : asmTranCondition_j["current_asm_node_name_ref"])
									asmTranCondition.specialCaseCurrentASMNodeNames.push_back(asmTranConditionNodeNameRef_j);
							}
							else
								asmTranCondition.compareToValue = asmTranCondition_j["compare_to_value"];

							asmNode.transitionConditionGroups[currentGroupIndex].transitionConditions.push_back(asmTranCondition);
						}
					}
				}
				timelineViewerState.animationStateMachineNodes.push_back(asmNode);
				allNodeNames.push_back(asmNode.nodeName);

				//
				// Add to GraphEditor Node preview
				//
				GraphEditorDelegate::Node gedNode;
				gedNode.mSelected = false;
				gedNode.templateIndex = 0;
				gedNode.name = (new std::string(asmNode.nodeName))->c_str();		// @MEMLEAK
				if (asmNode_j.contains("ged_pos"))
				{
					gedNode.x = asmNode_j["ged_pos"][0];
					gedNode.y = asmNode_j["ged_pos"][1];
				}
				else
				{
					static float startupPosX = 0;
					gedNode.x = startupPosX;
					gedNode.y = 0.0f;
					startupPosX += 400.0f;
				}
				timelineViewerState.editor_ASMNodePreviewerDelegate.mNodeNameToIndex[asmNode.nodeName] =
					timelineViewerState.editor_ASMNodePreviewerDelegate.mNodes.size();
				timelineViewerState.editor_ASMNodePreviewerDelegate.mNodes.push_back(gedNode);
			}

			//
			// Add in the links for the grapheditor node preview
			//
			for (auto asmNode : timelineViewerState.animationStateMachineNodes)
			{
				size_t tranCondGroupIndex = 0;
				for (auto tranCondGroup : asmNode.transitionConditionGroups)
				{
					// @COPYPASTA: This whole section right here
					std::vector<std::string> relevantNodes = allNodeNames;

					// @Possibly_not_needed: remove the reference to self in the relevant transition nodes.
					auto itr = relevantNodes.cbegin();
					while (itr != relevantNodes.cend())
					{
						if (*itr == asmNode.nodeName)
						{
							itr = relevantNodes.erase(itr);
						}
						else
							itr++;
					}

					for (auto tranCond : tranCondGroup.transitionConditions)
					{
						if (tranCond.varName != ASMTransitionCondition::specialCaseKey ||
							tranCond.comparisonOperator != ASMTransitionCondition::ASMComparisonOperator::EQUAL)
							continue;

						// Shave off or only include certain nodes
						auto itr = relevantNodes.cbegin();
						while (itr != relevantNodes.cend())
						{
							if (!CONTAINS_IN_VECTOR(tranCond.specialCaseCurrentASMNodeNames, *itr))		// @NOTE: only *leave* those that are equal, hence the !=
							{
								itr = relevantNodes.erase(itr);
							}
							else
								itr++;
						}
					}

					// Add the remaining relevantnodes into the links for the node previewer
					if (relevantNodes.empty())
					{
						std::cout << "WARNING: 0 nodes are able to transition into state {" << asmNode.nodeName << "}with Transition Condition Group " << tranCondGroupIndex << std::endl;
					}
					else
					{
						size_t toNodeIndex = timelineViewerState.editor_ASMNodePreviewerDelegate.mNodeNameToIndex[asmNode.nodeName];
						for (auto fromNodeName : relevantNodes)
						{
							size_t fromNodeIndex = timelineViewerState.editor_ASMNodePreviewerDelegate.mNodeNameToIndex[fromNodeName];
							timelineViewerState.editor_ASMNodePreviewerDelegate.AddLink(fromNodeIndex, 0, toNodeIndex, 0);
						}
					}

					tranCondGroupIndex++;
				}
			}
		}

		if (animStateMachine.contains("asm_start_node_index"))
			timelineViewerState.animationStateMachineStartNode = animStateMachine["asm_start_node_index"];
	}
}


void routinePlayCurrentASMAnimation()
{
	const ASMNode& asmNode = timelineViewerState.animationStateMachineNodes[timelineViewerState.editor_previewModeCurrentASMNode];
	if (asmNode.animationName2.empty())
	{
		size_t i = 0;
		for (auto anai : timelineViewerState.animationNameAndIncluded)
		{
			if (!anai.included)
				continue;
			if (anai.name == asmNode.animationName1)
				break;
			i++;
		}
		animatorForModelForTimelineViewer->playAnimation(i, asmNode.transitionTime, asmNode.loopAnimation);
	}
	else
	{
		size_t i = 0, j = 0;
		for (auto anai : timelineViewerState.animationNameAndIncluded)
		{
			if (!anai.included)
				continue;
			if (anai.name == asmNode.animationName1)
				break;
			i++;
		}
		for (auto anai : timelineViewerState.animationNameAndIncluded)
		{
			if (!anai.included)
				continue;
			if (anai.name == asmNode.animationName2)
				break;
			j++;
		}
		animatorForModelForTimelineViewer->playBlendTree({
				{ i, asmNode.animationBlend1, "blendVar" },
				{ j, asmNode.animationBlend2 }
			},
			asmNode.transitionTime,
			asmNode.loopAnimation
		);
	}
}


void RenderManager::renderImGuiContents()
{
	static bool showAnalyticsOverlay = true;
	static bool showDemoWindow = false;
	static bool showSceneProperties = true;
	static bool showObjectSelectionWindow = true;
	static bool showLoadedResourcesWindow = true;
	static bool showTimelineEditorWindow = true;

	//
	// Menu Bar
	//
	{
		if (ImGui::BeginMainMenuBar())
		{
			// Mainmenubar keyboard shortcuts
			GLFWwindow* windowRef = MainLoop::getInstance().window;
			const bool ctrl =
				glfwGetKey(windowRef, GLFW_KEY_LEFT_CONTROL) ||
				glfwGetKey(windowRef, GLFW_KEY_RIGHT_CONTROL);
			const bool shift =
				glfwGetKey(windowRef, GLFW_KEY_LEFT_SHIFT) ||
				glfwGetKey(windowRef, GLFW_KEY_RIGHT_SHIFT);

			static bool didKeyboardShortcutPrev = false;
			bool didKeyboardShortcut = false;
			if (ctrl && glfwGetKey(windowRef, GLFW_KEY_O))
			{
				if (!didKeyboardShortcutPrev)
					FileLoading::getInstance().loadFileWithPrompt(true);
				didKeyboardShortcut = true;
			}
			if (ctrl && !shift && glfwGetKey(windowRef, GLFW_KEY_S))
			{
				if (!didKeyboardShortcutPrev)
					FileLoading::getInstance().saveFile(false);
				didKeyboardShortcut = true;
			}
			if (ctrl && shift && glfwGetKey(windowRef, GLFW_KEY_S))
			{
				if (!didKeyboardShortcutPrev)
					FileLoading::getInstance().saveFile(true);
				didKeyboardShortcut = true;
			}
			didKeyboardShortcutPrev = didKeyboardShortcut;

			//ImGui::begin
			if (ImGui::BeginMenu("File"))
			{
				//ShowExampleMenuFile();
				if (ImGui::MenuItem("Open...", "CTRL+O"))
				{
					FileLoading::getInstance().loadFileWithPrompt(true);
				}

				bool enableSave = FileLoading::getInstance().isCurrentPathValid();
				if (ImGui::MenuItem("Save", "CTRL+S", false, enableSave))
				{
					FileLoading::getInstance().saveFile(false);
				}

				if (ImGui::MenuItem("Save As...", "CTRL+SHIFT+S"))
				{
					FileLoading::getInstance().saveFile(true);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
				if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled item

				ImGui::Separator();
				if (ImGui::MenuItem("Cut", "CTRL+X")) {}
				if (ImGui::MenuItem("Copy", "CTRL+C")) {}
				if (ImGui::MenuItem("Paste", "CTRL+V")) {}

				ImGui::Separator();
				auto objs = getSelectedObjects();
				if (ImGui::MenuItem("Duplicate", "CTRL+D", false, !objs.empty()))
				{
					// NOTE: copypasta
					for (size_t i = 0; i < objs.size(); i++)
					{
						nlohmann::json j = objs[i]->savePropertiesToJson();
						j["baseObject"].erase("guid");
						FileLoading::getInstance().createObjectWithJson(j);
					}

					if (objs.size() == 1)
					{
						// Select the last object if there's only one obj selected.
						deselectAllSelectedObject();
						addSelectObject(MainLoop::getInstance().objects.size() - 1);
					}
					else
						// If there's a group selection, then just deselect everything. It's kinda a hassle to try to select everything i think.
						deselectAllSelectedObject();
				}
				if (ImGui::MenuItem("Delete", "SHIFT+Del", false, !objs.empty()))
				{
					// NOTE: This is copypasta
					deleteAllSelectedObjects();
				}

				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Windows"))
			{
				if (ImGui::MenuItem("Analytics Overlay", NULL, &showAnalyticsOverlay)) {}
				if (ImGui::MenuItem("Scene Properties", NULL, &showSceneProperties)) {}
				if (ImGui::MenuItem("Object Selection", NULL, &showObjectSelectionWindow)) {}
				if (ImGui::MenuItem("Loaded Resources", NULL, &showLoadedResourcesWindow)) {}
				if (ImGui::MenuItem("Timeline Editor", NULL, &showTimelineEditorWindow)) {}
				if (ImGui::MenuItem("ImGui Demo Window", NULL, &showDemoWindow)) {}

				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Rendering"))
			{
				ImGui::MenuItem("Wireframe Mode", "F1", &isWireFrameMode);
				ImGui::MenuItem("Physics Debug During Playmode", "F2", &renderPhysicsDebug);

				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}

	//
	// Analytics Overlay
	//
	if (showAnalyticsOverlay)
	{
		// FPS counter
		static double prevTime = 0.0, crntTime = 0.0;
		static unsigned int counter = 0;
		static std::string fpsReportString;

		crntTime = glfwGetTime();
		double timeDiff = crntTime - prevTime;
		counter++;
		if (timeDiff >= 1.0 / 7.5)
		{
			std::string fps = std::to_string(1.0 / timeDiff * counter).substr(0, 5);
			std::string ms = std::to_string(timeDiff / counter * 1000).substr(0, 5);
			fpsReportString = "FPS: " + fps + " (" + ms + "ms)";
			prevTime = crntTime;
			counter = 0;
		}

		const float PAD = 10.0f;
		static int corner = 3;
		ImGuiIO& io = ImGui::GetIO();
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (corner != -1)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
			ImVec2 work_size = viewport->WorkSize;
			ImVec2 window_pos, window_pos_pivot;
			window_pos.x = (corner & 1) ? (work_pos.x + work_size.x - PAD) : (work_pos.x + PAD);
			window_pos.y = (corner & 2) ? (work_pos.y + work_size.y - PAD) : (work_pos.y + PAD);
			window_pos_pivot.x = (corner & 1) ? 1.0f : 0.0f;
			window_pos_pivot.y = (corner & 2) ? 1.0f : 0.0f;
			ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
			window_flags |= ImGuiWindowFlags_NoMove;
		}
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
		if (ImGui::Begin("Example: Simple overlay", &showAnalyticsOverlay, window_flags))
		{
			ImGui::Text(fpsReportString.c_str());
			/*if (ImGui::BeginPopupContextWindow())
			{
				if (ImGui::MenuItem("Custom", NULL, corner == -1)) corner = -1;
				if (ImGui::MenuItem("Top-left", NULL, corner == 0)) corner = 0;
				if (ImGui::MenuItem("Top-right", NULL, corner == 1)) corner = 1;
				if (ImGui::MenuItem("Bottom-left", NULL, corner == 2)) corner = 2;
				if (ImGui::MenuItem("Bottom-right", NULL, corner == 3)) corner = 3;
				if (showAnalyticsOverlay && ImGui::MenuItem("Close")) showOverlay = false;
				ImGui::EndPopup();
			}*/
		}
		ImGui::End();
	}

	//
	// Demo window
	// @TODO: once everything you need is made, just comment this out (for future reference in the future eh)
	//
	if (showDemoWindow)
		ImGui::ShowDemoWindow(&showDemoWindow);

#ifdef _DEVELOP
	//
	// @PHYSX_VISUALIZATION
	//
	if (renderPhysicsDebug &&
		MainLoop::getInstance().playMode &&
		physxVisDebugLines != nullptr)
	{
		std::vector<physx::PxDebugLine> debugLinesCopy = *physxVisDebugLines;		// @NOTE: this is to prevent the debugLines object from getting deleted while this render function is running
		for (size_t i = 0; i < debugLinesCopy.size(); i++)
		{
			//
			// Convert to screen space
			//
			physx::PxDebugLine& debugLine = debugLinesCopy[i];
			physx::PxU32 lineColor = debugLine.color0;		// @Checkup: would there be any situation where color0 and color1 would differ????

			// Change ugly purple color to the collision green!
			if (lineColor == 0xFFFF00FF)
				lineColor = ImColor::HSV(0.39f, 0.88f, 0.92f);

			bool willBeOnScreen = true;
			glm::vec3 pointsOnScreen[] = {
				MainLoop::getInstance().camera.PositionToClipSpace(PhysicsUtils::toGLMVec3(debugLine.pos0)),
				MainLoop::getInstance().camera.PositionToClipSpace(PhysicsUtils::toGLMVec3(debugLine.pos1))
			};
			for (size_t ii = 0; ii < 2; ii++)
			{
				if (pointsOnScreen[ii].z < 0.0f)
				{
					// Short circuit bc it won't be on screen anymore
					willBeOnScreen = false;
					break;
				}

				pointsOnScreen[ii] /= pointsOnScreen[ii].z;
				pointsOnScreen[ii].x = ImGui::GetWindowPos().x + pointsOnScreen[ii].x * MainLoop::getInstance().camera.width / 2 + MainLoop::getInstance().camera.width / 2;
				pointsOnScreen[ii].y = ImGui::GetWindowPos().y - pointsOnScreen[ii].y * MainLoop::getInstance().camera.height / 2 + MainLoop::getInstance().camera.height / 2;
			}

			if (!willBeOnScreen)
				continue;

			ImVec2 point1(pointsOnScreen[0].x, pointsOnScreen[0].y);
			ImVec2 point2(pointsOnScreen[1].x, pointsOnScreen[1].y);
			ImGui::GetBackgroundDrawList()->AddLine(point1, point2, lineColor, 1.0f);
		}
	}
#endif

	//
	// Everything else
	//
	for (unsigned int i = 0; i < MainLoop::getInstance().objects.size(); i++)
	{
		MainLoop::getInstance().objects[i]->imguiRender();
	}

#ifdef _DEVELOP
	//
	// Render bounds of all renderobjects
	//
	if (renderMeshRenderAABB)
	{
		if (MainLoop::getInstance().timelineViewerMode && modelForTimelineViewer != nullptr)
		{
			modelForTimelineViewer->TEMPrenderImguiModelBounds();
		}
		else
			for (size_t i = 0; i < MainLoop::getInstance().renderObjects.size(); i++)
			{
				MainLoop::getInstance().renderObjects[i]->TEMPrenderImguiModelBounds();
			}
	}
#endif

	if (!MainLoop::getInstance().timelineViewerMode)
	{
		//
		// Object Selection Window
		//
		static int imGuizmoTransformOperation = 0;
		static int imGuizmoTransformMode = 0;
		if (showObjectSelectionWindow)
		{
			if (ImGui::Begin("Scene Object List", &showObjectSelectionWindow))
			{
				ImGui::Combo("##Transform operation combo", &imGuizmoTransformOperation, "Translate\0Rotate\0Scale\0Bounds");
				ImGui::Combo("##Transform mode combo", &imGuizmoTransformMode, "Local\0World");

				GLFWwindow* windowRef = MainLoop::getInstance().window;
				auto objs = getSelectedObjects();
				if (!objs.empty() &&
					!ImGuizmo::IsUsing() &&
					glfwGetMouseButton(windowRef, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE)
				{
					static bool heldQDownPrev = false;
					if (!heldQDownPrev && glfwGetKey(windowRef, GLFW_KEY_Q))
						imGuizmoTransformMode = (int)!(bool)imGuizmoTransformMode;		// Switch between local and world transformation
					heldQDownPrev = glfwGetKey(windowRef, GLFW_KEY_Q) == GLFW_PRESS;

					if (glfwGetKey(windowRef, GLFW_KEY_W))
						imGuizmoTransformOperation = 0;
					if (glfwGetKey(windowRef, GLFW_KEY_E))
						imGuizmoTransformOperation = 1;
					if (glfwGetKey(windowRef, GLFW_KEY_R))
						imGuizmoTransformOperation = 2;
					if ((glfwGetKey(windowRef, GLFW_KEY_LEFT_SHIFT) || glfwGetKey(windowRef, GLFW_KEY_RIGHT_SHIFT)) &&
						glfwGetKey(windowRef, GLFW_KEY_DELETE))
					{
						deleteAllSelectedObjects();
					}

					//
					// Press ctrl+d to copy a selected object
					//
					{
						static bool objectDupeKeyboardShortcutLock = false;
						if ((glfwGetKey(windowRef, GLFW_KEY_LEFT_CONTROL) || glfwGetKey(windowRef, GLFW_KEY_RIGHT_CONTROL)) && glfwGetKey(windowRef, GLFW_KEY_D))
						{
							if (!objectDupeKeyboardShortcutLock)
							{
								objectDupeKeyboardShortcutLock = true;

								// NOTE: copypasta
								for (size_t i = 0; i < objs.size(); i++)
								{
									nlohmann::json j = objs[i]->savePropertiesToJson();
									j["baseObject"].erase("guid");
									FileLoading::getInstance().createObjectWithJson(j);
								}

								if (objs.size() == 1)
								{
									// Select the last object if there's only one obj selected.
									deselectAllSelectedObject();
									addSelectObject(MainLoop::getInstance().objects.size() - 1);
								}
								else
									// If there's a group selection, then just deselect everything. It's kinda a hassle to try to select everything i think.
									deselectAllSelectedObject();
							}
						}
						else
							objectDupeKeyboardShortcutLock = false;
					}
				}

				if (ImGui::BeginListBox("##listbox Scene Objects", ImVec2(300, 25 * ImGui::GetTextLineHeightWithSpacing())))
				{
					//
					// Display all of the objects in the scene
					//
					int shiftSelectRequest[] = { -1, -1 };
					for (size_t n = 0; n < MainLoop::getInstance().objects.size(); n++)
					{
						if (dynamic_cast<DummyBaseObject*>(MainLoop::getInstance().objects[n]))		// Prevent ability to select dummy objects
							continue;

						const bool isSelected = isObjectSelected(n);
						if (ImGui::Selectable(
							(MainLoop::getInstance().objects[n]->name + "##" + MainLoop::getInstance().objects[n]->guid).c_str(),
							isSelected
						))
						{
							bool isShiftHeld =
								glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_LEFT_SHIFT) ||
								glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_RIGHT_SHIFT);

							if (isShiftHeld)
							{
								if (!isSelected && !selectedObjectIndices.empty())
								{
									shiftSelectRequest[0] = glm::min((size_t)selectedObjectIndices[selectedObjectIndices.size() - 1], n);
									shiftSelectRequest[1] = glm::max((size_t)selectedObjectIndices[selectedObjectIndices.size() - 1], n);
								}
							}
							else
							{
								if (!glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_LEFT_CONTROL) &&
									!glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_RIGHT_CONTROL))
									deselectAllSelectedObject();

								if (isObjectSelected(n))
									deselectObject(n);
								else
									addSelectObject(n);
							}
						}

						// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndListBox();

					//
					// Finally Execute SHIFT+click group selection
					//
					if (shiftSelectRequest[0] >= 0 && shiftSelectRequest[1] >= 0)
						for (size_t n = (size_t)shiftSelectRequest[0]; n <= (size_t)shiftSelectRequest[1]; n++)
							addSelectObject(n);
				}

				//
				// Popup for creating objects
				//
				if (ImGui::Button("Add Object.."))
					ImGui::OpenPopup("add_object_popup");
				if (ImGui::BeginPopup("add_object_popup"))
				{
					//
					// @Palette: where to add objects to add in imgui
					//
					BaseObject* newObject = nullptr;
					if (ImGui::Selectable("Player Character"))			newObject = new PlayerCharacter();
					if (ImGui::Selectable("Directional Light"))			newObject = new DirectionalLight(true);
					if (ImGui::Selectable("Point Light"))				newObject = new PointLight(true);
					if (ImGui::Selectable("Yosemite Terrain"))			newObject = new YosemiteTerrain();
					if (ImGui::Selectable("Collectable Water Puddle"))	newObject = new WaterPuddle();
					if (ImGui::Selectable("River Dropoff Area"))		newObject = new RiverDropoff();
					if (ImGui::Selectable("Voxel Group"))				newObject = new VoxelGroup();
					if (ImGui::Selectable("Spline Tool"))				newObject = new Spline();
					if (ImGui::Selectable("GondolaPath"))				newObject = new GondolaPath();

					if (newObject != nullptr)
					{
						if (!glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_LEFT_CONTROL) &&
							!glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_RIGHT_CONTROL))
							deselectAllSelectedObject();
						addSelectObject(MainLoop::getInstance().objects.size() - 1);
					}

					ImGui::EndPopup();
				}
			}
			ImGui::End();
		}

		//
		// Scene Properties window @PROPS
		//
		if (showSceneProperties)
		{
			if (ImGui::Begin("Scene Properties", &showSceneProperties))
			{
				ImGui::DragFloat("Camera Movement Speed", &MainLoop::getInstance().camera.speed, 0.05f);
				ImGui::DragFloat("Global Timescale", &MainLoop::getInstance().timeScale);
				ImGui::Checkbox("Show shadowmap view", &showShadowMapView);

				ImGui::DragFloat("Cloud layer Y start", &cloudEffectInfo.cloudLayerY);
				ImGui::DragFloat("Cloud layer thickness", &cloudEffectInfo.cloudLayerThickness);
				ImGui::DragFloat("Cloud layer tile size", &cloudEffectInfo.cloudNoiseMainSize);
				ImGui::DragFloat("Cloud layer detail tile size", &cloudEffectInfo.cloudNoiseDetailSize);
				ImGui::DragFloat3("Cloud layer detail tile offset", &cloudEffectInfo.cloudNoiseDetailOffset.x);
				ImGui::DragFloat3("Cloud layer detail tile velocity", &cloudEffectInfo.cloudNoiseDetailVelocity.x);
				ImGui::DragFloat("Cloud inner density offset", &cloudEffectInfo.densityOffsetInner, 0.01f);
				ImGui::DragFloat("Cloud outer density offset", &cloudEffectInfo.densityOffsetOuter, 0.01f);
				ImGui::DragFloat("Cloud inner/outer change radius", &cloudEffectInfo.densityOffsetChangeRadius, 1.0f);
				ImGui::DragFloat("Cloud density multiplier", &cloudEffectInfo.densityMultiplier, 0.01f);
				ImGui::DragFloat("Cloud density requirement", &cloudEffectInfo.densityRequirement, 0.01f);
				ImGui::DragFloat("Cloud Ambient Density", &cloudEffectInfo.darknessThreshold, 0.01f);
				ImGui::DragFloat("Cloud irradianceStrength", &cloudEffectInfo.irradianceStrength, 0.01f);
				ImGui::DragFloat("Cloud absorption (sun)", &cloudEffectInfo.lightAbsorptionTowardsSun, 0.01f);
				ImGui::DragFloat("Cloud absorption (cloud)", &cloudEffectInfo.lightAbsorptionThroughCloud, 0.01f);
				ImGui::DragFloat("Cloud Raymarch offset", &cloudEffectInfo.raymarchOffset, 0.01f);
				ImGui::DragFloat2("Cloud near raymarch method distance", &cloudEffectInfo.raymarchCascadeLevels.x);
				ImGui::DragFloat("Cloud farRaymarchStepsizeMultiplier", &cloudEffectInfo.farRaymarchStepsizeMultiplier, 0.01f, 0.01f);
				//ImGui::DragFloat("Cloud max raymarch length", &cloudEffectInfo.maxRaymarchLength);
				ImGui::DragFloat4("Cloud phase Parameters", &cloudEffectInfo.phaseParameters.x);
				ImGui::Checkbox("Cloud do blur pass", &cloudEffectInfo.doBlurPass);
				ImGui::DragFloat("Cloud jitter scale", &cloudEffectInfo.cameraPosJitterScale);
				ImGui::DragFloat("Cloud apply ss effect density", &ShaderExtCloud_effect::cloudEffectDensity);

				ImGui::Checkbox("Show Cloud noise view", &showCloudNoiseView);
				if (showCloudNoiseView)
				{
					ImGui::InputInt("Cloud noise channel", &debugCloudNoiseChannel);
					ImGui::DragFloat("Cloud noise view layer", &debugCloudNoiseLayerNum);
				}

				ImGui::Checkbox("Toggle Cloud TAA", &doCloudHistoryTAA);
				if (!doCloudHistoryTAA)
					ImGui::Checkbox("Toggle Cloud Color Floodfill", &doCloudColorFloodFill);
				else
					ImGui::Text("*NOTE: to show option for cloud color floodfill, disable cloud taa");
				ImGui::Checkbox("Toggle Cloud Denoise (non-temporal method)", &doCloudDenoiseNontemporal);

				static bool cloudHistoryTAAVelocityBufferTemp = false;
				ImGui::Checkbox("Show Cloud History TAA Buffer", &cloudHistoryTAAVelocityBufferTemp);
				if (cloudHistoryTAAVelocityBufferTemp)
				{
					ImGui::Image((void*)(intptr_t)cloudEffectBlurTexture->getHandle(), ImVec2(1024, 576));
				}

				static bool showLuminanceTextures = false;
				ImGui::Checkbox("Show Luminance Texture", &showLuminanceTextures);
				if (showLuminanceTextures)
				{
					ImGui::Image((void*)(intptr_t)hdrLumDownsampling->getHandle(), ImVec2(256, 256));
					ImGui::Image((void*)(intptr_t)hdrLumAdaptation1x1, ImVec2(256, 256));
					ImGui::Image((void*)(intptr_t)hdrLumAdaptationProcessed->getHandle(), ImVec2(256, 256));
				}
				ImGui::Separator();

				static bool showSSAOTexture = false;
				ImGui::Checkbox("Show SSAO Texture", &showSSAOTexture);
				if (showSSAOTexture)
				{
					ImGui::Image((void*)(intptr_t)ssaoTexture->getHandle(), ImVec2(512, 288));
				}

				ImGui::DragFloat("SSAO Scale", &ssaoScale, 0.001f);
				ImGui::DragFloat("SSAO Bias", &ssaoBias, 0.001f);
				ImGui::DragFloat("SSAO Radius", &ssaoRadius, 0.001f);
				ImGui::Separator();

				static bool showBloomProcessingBuffers = false;
				ImGui::Checkbox("Show Bloom preprocessing buffers", &showBloomProcessingBuffers);
				if (showBloomProcessingBuffers)
				{
					static int colBufNum = 0;
					ImGui::InputInt("Color Buffer Index", &colBufNum);
					ImGui::Image((void*)(intptr_t)bloomColorBuffers[colBufNum], ImVec2(512, 288));
				}


				ImGui::Separator();
				ImGui::DragFloat("Volumetric Lighting Strength", &volumetricLightingStrength);
				ImGui::DragFloat("External Volumetric Lighting Strength", &volumetricLightingStrengthExternal);

				ImGui::Separator();
				ImGui::DragFloat("Scene Tonemapping Exposure", &exposure);
				ImGui::DragFloat("Bloom Intensity", &bloomIntensity, 0.05f, 0.0f, 5.0f);

				ImGui::Separator();
				ImGui::DragFloat2("notifExtents", &notifExtents[0]);
				ImGui::DragFloat2("notifPosition", &notifPosition[0]);
				ImGui::DragFloat2("notifAdvance", &notifAdvance[0]);
				ImGui::DragFloat2("notifHidingOffset", &notifHidingOffset[0]);
				ImGui::ColorEdit3("notifColor1", &notifColor1[0]);
				ImGui::ColorEdit3("notifColor2", &notifColor2[0]);
				ImGui::DragFloat("notifMessageSize", &notifMessageSize);
				ImGui::DragFloat("notifAnimTime", &notifAnimTime);
				ImGui::DragFloat("notifHoldTime", &notifHoldTime);

				ImGui::Separator();
				ImGui::Text("Skybox properties");
				ImGui::DragFloat("Time of Day", &GameState::getInstance().dayNightTime, 0.005f);
				ImGui::DragFloat("Time of Day susumu sokudo", &GameState::getInstance().dayNightTimeSpeed, 0.001f);
				ImGui::Checkbox("Is Daynight Time moving", &GameState::getInstance().isDayNightTimeMoving);
				ImGui::DragFloat("Sun Radius", &skyboxParams.sunRadius, 0.01f);
				ImGui::ColorEdit3("sunColor", &skyboxParams.sunColor[0]);
				ImGui::ColorEdit3("skyColor1", &skyboxParams.skyColor1[0]);
				ImGui::ColorEdit3("groundColor", &skyboxParams.groundColor[0]);
				ImGui::DragFloat("Sun Intensity", &skyboxParams.sunIntensity, 0.01f);
				ImGui::DragFloat("Global Exposure", &skyboxParams.globalExposure, 0.01f);
				ImGui::DragFloat("Cloud Height", &skyboxParams.cloudHeight, 0.01f);
				ImGui::DragFloat("perlinDim", &skyboxParams.perlinDim, 0.01f);
				ImGui::DragFloat("perlinTime", &skyboxParams.perlinTime, 0.01f);

				ImGui::DragInt("Which ibl map", (int*)&whichMap, 1.0f, 0, 4);
				ImGui::DragFloat("Map Interpolation amount", &mapInterpolationAmt);

				ImGui::Separator();
				ImGui::Text("Player Stamina Properties");
				ImGui::DragInt("Stamina Max", &GameState::getInstance().maxPlayerStaminaAmount);
				ImGui::DragFloat("Stamina Current", &GameState::getInstance().currentPlayerStaminaAmount);
			}
			ImGui::End();
		}

		//
		// Render out the properties panels of selected object
		//
		auto objs = getSelectedObjects();
		if (objs.size() > 0)
		{
			ImGui::Begin("Selected Object Properties");
			if (objs.size() == 1)
			{
				BaseObject* obj = objs[0];

				//
				// Property panel stuff that's the same with all objects
				//
				ImGui::Text((obj->name + " -- Properties").c_str());
				ImGui::Text(("GUID: " + obj->guid).c_str());
				ImGui::Separator();

				ImGui::InputText("Name", &obj->name);
				ImGui::Separator();

				glm::mat4& transOriginal = obj->getTransform();
				glm::mat4 transCopy = transOriginal;
				PhysicsUtils::imguiTransformMatrixProps(glm::value_ptr(transCopy));

				// To propogate the transform!!
				if (!PhysicsUtils::epsilonEqualsMatrix(transOriginal, transCopy))
					obj->setTransform(transCopy);

				ImGui::Separator();

				// Other property panel stuff
				obj->imguiPropertyPanel();
			}
			else if (objs.size() > 1)
			{
				ImGui::Text("Multiple objects are currently selected");
			}
			ImGui::End();
		}

		//
		// Loaded resources window
		//
		static int currentSelectLoadedResource = -1;
		if (showLoadedResourcesWindow)
		{
			if (ImGui::Begin("Loaded Resources", &showLoadedResourcesWindow))
			{
				std::map<std::string, void*>::iterator it;
				static std::string selectedKey;
				std::map<std::string, void*>& resMapRef = Resources::getResourceMap();
				if (ImGui::BeginListBox("##listbox Loaded Resources", ImVec2(300, 25 * ImGui::GetTextLineHeightWithSpacing())))
				{
					//
					// Display all of the objects in the scene
					//
					int index = 0;
					for (it = resMapRef.begin(); it != resMapRef.end(); it++, index++)
					{
						const bool isSelected = (currentSelectLoadedResource == index);
						if (ImGui::Selectable(
							(it->first).c_str(),
							isSelected
						))
						{
							selectedKey = it->first;
							currentSelectLoadedResource = index;
						}

						// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndListBox();
				}

				//
				// Reload prompt
				//
				if (currentSelectLoadedResource != -1)
				{
					ImGui::Separator();
					if (ImGui::Button("Reload Resource"))
					{
						Resources::reloadResource(selectedKey);
					}
				}

			}
			ImGui::End();
		}
		
		//
		// ImGuizmo (NOTE: keep this at the very end so that imguizmo stuff can be rendered after everything else in the background draw list has been rendered)
		//
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 work_pos = viewport->WorkPos;			// Use work area to avoid menu-bar/task-bar, if any!
		ImVec2 work_size = viewport->WorkSize;

		if (!objs.empty() && !MainLoop::getInstance().playMode && !tempDisableImGuizmoManipulateForOneFrame)
		{
			ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
			ImGuizmo::SetRect(work_pos.x, work_pos.y, work_size.x, work_size.y);

			transOperation = ImGuizmo::OPERATION::TRANSLATE;
			if (imGuizmoTransformOperation == 1)
				transOperation = ImGuizmo::OPERATION::ROTATE;
			if (imGuizmoTransformOperation == 2)
				transOperation = ImGuizmo::OPERATION::SCALE;
			if (imGuizmoTransformOperation == 3)
				transOperation = ImGuizmo::OPERATION::BOUNDS;

			ImGuizmo::MODE transMode = ImGuizmo::MODE::LOCAL;
			if (imGuizmoTransformMode == 1)
				transMode = ImGuizmo::MODE::WORLD;

			// Implement snapping if holding down control button(s)
			float snapAmount = 0.0f;
			if (glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
				glfwGetKey(MainLoop::getInstance().window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
			{
				if (transOperation == ImGuizmo::OPERATION::ROTATE)
					snapAmount = 45.0f;
				else
					snapAmount = 0.5f;
			}
			glm::vec3 snapValues(snapAmount);

			glm::vec3 averagePosition(0.0f);
			glm::quat latestOrientation;
			if (transOperation == ImGuizmo::OPERATION::SCALE && ImGuizmo::IsUsing())
			{
				// Okay, I thought that this would be the fix for scale... but it's now just super buggy hahahaha
				// I don't really care about scale, so we can keep it like this
				//   -Timo
				averagePosition = INTERNALselectionSystemAveragePosition;
				latestOrientation = INTERNALselectionSystemLatestOrientation;
			}
			else
			{
				for (size_t i = 0; i < objs.size(); i++)
				{
					averagePosition += PhysicsUtils::getPosition(objs[i]->getTransform());
					latestOrientation = PhysicsUtils::getRotation(objs[i]->getTransform());
				}
				averagePosition /= (float)objs.size();
			}

			glm::mat4 tempTrans = glm::translate(glm::mat4(1.0f), averagePosition) * glm::toMat4(latestOrientation);
			glm::mat4 deltaMatrix;
			bool changed =
				ImGuizmo::Manipulate(
					glm::value_ptr(cameraInfo.view),
					glm::value_ptr(cameraInfo.projection),
					transOperation,
					transMode,
					glm::value_ptr(tempTrans),
					glm::value_ptr(deltaMatrix),
					&snapValues.x
				);

			if (changed)
			{
				for (size_t i = 0; i < objs.size(); i++)
					objs[i]->setTransform(deltaMatrix * objs[i]->getTransform());

				INTERNALselectionSystemAveragePosition = averagePosition;
				INTERNALselectionSystemLatestOrientation = latestOrientation;
			}
		}
		tempDisableImGuizmoManipulateForOneFrame = false;
	}

	//
	// Timeline editor window
	//
	if (showTimelineEditorWindow)
	{
		if (ImGui::Begin("Timeline Editor", &showTimelineEditorWindow))
		{
			if (MainLoop::getInstance().timelineViewerMode && !timelineViewerState.editor_isASMPreviewMode)
			{
				MainLoop::getInstance().playMode = false;		// Ehhhh, this is probably overkill, but just in case, I don't want any funny business!

				static bool saveAndApplyChangesFlag = false;

				if (ImGui::Button("Exit Timeline Viewer Mode"))
				{
					routinePurgeTimelineViewerModel();
					MainLoop::getInstance().timelineViewerMode = false;
					return;
				}
				
				if (saveAndApplyChangesFlag)
				{
					ImGui::SameLine();
					if (ImGui::Button("Save and Apply Changes"))
					{
						//
						// SAVE: Do a big save of everything
						//
						nlohmann::json j = FileLoading::loadJsonFile(modelMetadataFname);

						// Material References
						nlohmann::json materialPathsContainer;
						for (auto it = timelineViewerState.materialPathsMap.begin(); it != timelineViewerState.materialPathsMap.end(); it++)
						{
							materialPathsContainer[it->first] = it->second;
						}
						j["material_paths"] = materialPathsContainer;

						// Included Animations
						std::vector<std::string> includedAnimations;
						for (size_t i = 0; i < timelineViewerState.animationNameAndIncluded.size(); i++)
							if (timelineViewerState.animationNameAndIncluded[i].included)
								includedAnimations.push_back(timelineViewerState.animationNameAndIncluded[i].name);
						j["included_anims"] = includedAnimations;

						// Animation Details
						nlohmann::json animationDetailsContainer;
						for (auto it = timelineViewerState.animationDetailMap.begin(); it != timelineViewerState.animationDetailMap.end(); it++)
						{
							nlohmann::json animDetail_j;
							animDetail_j["track_xz_root_motion"] = it->second.trackXZRootMotion;
							animDetail_j["timestamp_speed"] = it->second.timestampSpeed;
							
							animationDetailsContainer[it->first] = animDetail_j;
						}
						j["animation_details"] = animationDetailsContainer;

						// Animation State Machine stuff
						nlohmann::json animationStateMachineContainer;
						animationStateMachineContainer["asm_variables"] = nlohmann::json::array();
						for (size_t i = 0; i < timelineViewerState.animationStateMachineVariables.size(); i++)
						{
							ASMVariable& asmVariable = timelineViewerState.animationStateMachineVariables[i];
							nlohmann::json asmVariable_j;
							asmVariable_j["var_name"] = asmVariable.varName;
							asmVariable_j["variable_type"] = (int)asmVariable.variableType;
							asmVariable_j["value"] = asmVariable.value;

							animationStateMachineContainer["asm_variables"].push_back(asmVariable_j);
						}

						animationStateMachineContainer["asm_nodes"] = nlohmann::json::array();
						for (size_t i = 0; i < timelineViewerState.animationStateMachineNodes.size(); i++)
						{
							ASMNode& asmNode = timelineViewerState.animationStateMachineNodes[i];
							nlohmann::json asmNode_j;
							asmNode_j["node_name"] = asmNode.nodeName;
							asmNode_j["animation_name_1"] = asmNode.animationName1;
							asmNode_j["animation_name_2"] = asmNode.animationName2;
							asmNode_j["animation_blend_variable"] = asmNode.varFloatBlend;
							asmNode_j["animation_blend_boundary_1"] = asmNode.animationBlend1;
							asmNode_j["animation_blend_boundary_2"] = asmNode.animationBlend2;
							asmNode_j["loop_animation"] = asmNode.loopAnimation;
							asmNode_j["dont_transition_until_animation_finished"] = asmNode.doNotTransitionUntilAnimationFinished;
							asmNode_j["transition_time"] = asmNode.transitionTime;

							nlohmann::json transitionConditionGroups_j = nlohmann::json::array();
							for (size_t j = 0; j < asmNode.transitionConditionGroups.size(); j++)
							{
								nlohmann::json transitionConditions_j = nlohmann::json::array();
								for (size_t k = 0; k < asmNode.transitionConditionGroups[j].transitionConditions.size(); k++)
								{
									ASMTransitionCondition& asmTranCondition = asmNode.transitionConditionGroups[j].transitionConditions[k];
									nlohmann::json asmTranCondition_j;
									asmTranCondition_j["var_name"] = asmTranCondition.varName;
									asmTranCondition_j["comparison_operator"] = (int)asmTranCondition.comparisonOperator;

									if (asmTranCondition.varName == ASMTransitionCondition::specialCaseKey)
									{
										for (auto nodeNameRef : asmTranCondition.specialCaseCurrentASMNodeNames)
											asmTranCondition_j["current_asm_node_name_ref"].push_back(nodeNameRef);
									}
									else
										asmTranCondition_j["compare_to_value"] = asmTranCondition.compareToValue;

									transitionConditions_j.push_back(asmTranCondition_j);
								}
								transitionConditionGroups_j.push_back(transitionConditions_j);
							}
							asmNode_j["node_transition_condition_groups"] = transitionConditionGroups_j;

							// Get the grapheditor delegate position in there
							float gedPos[2] = { 0.0f, 0.0f };
							const size_t gedNodeIndex = timelineViewerState.editor_ASMNodePreviewerDelegate.mNodeNameToIndex[asmNode.nodeName];
							if (gedNodeIndex < timelineViewerState.editor_ASMNodePreviewerDelegate.GetNodeCount())
							{
								auto gedPosRaw =
									timelineViewerState.editor_ASMNodePreviewerDelegate.GetNode(gedNodeIndex).mRect.Min;
								gedPos[0] = gedPosRaw.x;
								gedPos[1] = gedPosRaw.y;
							}
							asmNode_j["ged_pos"] = gedPos;

							animationStateMachineContainer["asm_nodes"].push_back(asmNode_j);
						}
						j["animation_state_machine"] = animationStateMachineContainer;
						j["animation_state_machine"]["asm_start_node_index"] = timelineViewerState.animationStateMachineStartNode;

						FileLoading::saveJsonFile(modelMetadataFname, j);

						// Reimport the model now
						routinePurgeTimelineViewerModel();
						routineCreateAndInsertInTheModel(modelMetadataFname.c_str(), j, true);

						saveAndApplyChangesFlag = false;
						MainLoop::getInstance().renderManager->pushMessage("TIMELINE_EDITOR: Saved and Applied Changes");
					}
				}

				//
				// Materials
				//
				ImGui::Separator();
				if (ImGui::TreeNode("Materials"))
				{
					std::vector<std::string> materialNameList = modelForTimelineViewer->getMaterialNameList();
					static int selectedMaterial = -1;
					static std::string stagedMaterialName;		// When "apply" is clicked, the material is verified and then assigned.
					static bool showApplyMaterialFlag = false;
					static bool showApplyingMaterialAbortedErrorMessage = false;
					static bool showApplyingMaterialSuccessMessage = false;

					if (ImGui::BeginCombo("Selected Material", selectedMaterial == -1 ? "Select..." : materialNameList[selectedMaterial].c_str()))
					{
						if (ImGui::Selectable("Select...", selectedMaterial == -1))
						{
							selectedMaterial = -1;
							stagedMaterialName = "";
							showApplyMaterialFlag = false;
							showApplyingMaterialAbortedErrorMessage = false;
							showApplyingMaterialSuccessMessage = false;
						}

						for (size_t i = 0; i < materialNameList.size(); i++)
						{
							const bool isSelected = (selectedMaterial == i);
							if (ImGui::Selectable(materialNameList[i].c_str(), isSelected))
							{
								selectedMaterial = i;
								stagedMaterialName =
									(timelineViewerState.materialPathsMap.find(materialNameList[i]) == timelineViewerState.materialPathsMap.end()) ?
									"" :
									timelineViewerState.materialPathsMap[materialNameList[i]];
								showApplyMaterialFlag = false;
								showApplyingMaterialAbortedErrorMessage = false;
								showApplyingMaterialSuccessMessage = false;
							}

							// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}

						ImGui::EndCombo();
					}

					if (selectedMaterial >= 0)
					{
						ImGui::Text("Assign Material");
						if (ImGui::InputText("Material Name to Assign", &stagedMaterialName))
						{
							showApplyMaterialFlag = true;
							showApplyingMaterialAbortedErrorMessage = false;
							showApplyingMaterialSuccessMessage = false;
						}

						if (showApplyMaterialFlag &&
							ImGui::Button("Apply new Material Name"))
						{
							try
							{
								//
								// Load in the new material		@COPYPASTA
								//
								Material* materialToAssign = (Material*)Resources::getResource("material;" + stagedMaterialName);	// @NOTE: this line should fail if the material isn't inputted correctly.
								if (materialToAssign == nullptr)
									throw nullptr;		// Just get it to the catch statement

								std::map<std::string, Material*> materialAssignmentMap;
								materialAssignmentMap[materialNameList[selectedMaterial]] = materialToAssign;
								modelForTimelineViewer->getModelFromIndex(0).model->setMaterials(materialAssignmentMap);	// You'll get a butt ton of errors since there's only one material name in here, but who cares eh  -Timo

								// Assign to the timelineViewerState
								timelineViewerState.materialPathsMap[materialNameList[selectedMaterial]] = stagedMaterialName;

								showApplyingMaterialAbortedErrorMessage = false;	// Undo the error message if succeeded
								showApplyingMaterialSuccessMessage = true;
								MainLoop::getInstance().renderManager->pushMessage("TIMELINE_EDITOR: SUCCESS! Material was assigned.");

								saveAndApplyChangesFlag = true;		// Since changes were applied material-wise safely, we prompt a save changes flag
							}
							catch (...)
							{
								// Abort applying the material name and show an error message.  @HACK: Bad patterns
								showApplyingMaterialAbortedErrorMessage = true;
								MainLoop::getInstance().renderManager->pushMessage("TIMELINE_EDITOR: ERROR: the material assignment failed. Try a different material path.");
							}
						}

						if (showApplyingMaterialAbortedErrorMessage)
							ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ERROR: the material assignment failed. Try a different material path.");
						if (showApplyingMaterialSuccessMessage)
							ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "SUCCESS! Material was assigned.");
					}

					ImGui::TreePop();
				}

				ImGui::Separator();
				if (timelineViewerState.animationNameAndIncluded.size() == 0)
					ImGui::Text("No animations found. Zannnen.");
				else
				{
					//
					// Animations
					//
					if (ImGui::TreeNode("Animations"))
					{
						//
						// Import animations
						//
						if (ImGui::TreeNode("Include Animations"))
						{
							//
							// Show imgui for the animations
							//
							for (size_t i = 0; i < timelineViewerState.animationNameAndIncluded.size(); i++)
							{
								saveAndApplyChangesFlag |= ImGui::Checkbox(timelineViewerState.animationNameAndIncluded[i].name.c_str(), &timelineViewerState.animationNameAndIncluded[i].included);
							}

							ImGui::TreePop();
						}

						//
						// Select Animation to edit
						//
						ImGui::Separator();
						std::vector<Animation> modelAnimations = modelForTimelineViewer->getModelFromIndex(0).model->getAnimations();
						if (ImGui::BeginCombo("Selected Animation", timelineViewerState.editor_selectedAnimation == -1 ? "Select..." : modelAnimations[timelineViewerState.editor_selectedAnimation].getName().c_str()))
						{
							if (ImGui::Selectable("Select...", timelineViewerState.editor_selectedAnimation == -1))
							{
								timelineViewerState.editor_selectedAnimation = -1;
								timelineViewerState.editor_selectedAnimationPtr = nullptr;
								timelineViewerState.editor_isAnimationPlaying = false;
								timelineViewerState.editor_animationPlaybackTimestamp = 0.0f;
							}

							for (size_t i = 0; i < modelAnimations.size(); i++)
							{
								const bool isSelected = (timelineViewerState.editor_selectedAnimation == i);
								if (ImGui::Selectable(modelAnimations[i].getName().c_str(), isSelected))
								{
									timelineViewerState.editor_selectedAnimation = i;
									timelineViewerState.editor_selectedAnimationPtr = &timelineViewerState.animationDetailMap[modelAnimations[i].getName()];
									timelineViewerState.editor_isAnimationPlaying = false;
									timelineViewerState.editor_animationPlaybackTimestamp = 0.0f;
								}

								// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
								if (isSelected)
									ImGui::SetItemDefaultFocus();
							}

							ImGui::EndCombo();
						}

						//
						// Edit animation and stuff (if selected)
						//
						if (timelineViewerState.editor_selectedAnimationPtr != nullptr)
						{
							//
							// Edit selected animation
							//
							if (ImGui::TreeNode("Edit Selected Animation"))
							{
								ImGui::Text("General Settings");
								saveAndApplyChangesFlag |= ImGui::Checkbox("Import with XZ Root Motion ##import_selected_animation_information", &timelineViewerState.editor_selectedAnimationPtr->trackXZRootMotion);
								saveAndApplyChangesFlag |= ImGui::DragFloat("Timestamp Speed ##import_selected_animation_information", &timelineViewerState.editor_selectedAnimationPtr->timestampSpeed);

								ImGui::TreePop();
							}

							//
							// Preview Animation
							//
							Animation& currentAnim = modelForTimelineViewer->getModelFromIndex(0).model->getAnimations()[timelineViewerState.editor_selectedAnimation];
							float animationDuration = currentAnim.getDuration() / currentAnim.getTicksPerSecond();
							if (animationDuration > 0.0f)
							{
								ImGui::Separator();
								ImGui::Text("Preview Animation");
								ImGui::Checkbox("Preview Animation##This is the checkbox", &timelineViewerState.editor_isAnimationPlaying);
								ImGui::SliderFloat("Animation Point in Time", &timelineViewerState.editor_animationPlaybackTimestamp, 0.0f, animationDuration);

								if (timelineViewerState.editor_isAnimationPlaying)
								{
									timelineViewerState.editor_isASMPreviewMode = false;
									timelineViewerState.editor_animationPlaybackTimestamp += timelineViewerState.editor_selectedAnimationPtr->timestampSpeed * MainLoop::getInstance().deltaTime;
								}
								timelineViewerState.editor_animationPlaybackTimestamp = fmod(timelineViewerState.editor_animationPlaybackTimestamp, animationDuration);
							}
							else
							{
								ImGui::Text("Previewing disabled for this animation (animation duration is 0.0)");
							}
						}

						//
						// Update the animation state (Preview Animation)
						// NOTE: this is only available when the animation tab is open.
						//
						if (animatorForModelForTimelineViewer != nullptr && timelineViewerState.editor_selectedAnimationPtr != nullptr)
						{
							if (timelineViewerState.editor_currentlyPlayingAnimation != timelineViewerState.editor_selectedAnimation)
							{
								timelineViewerState.editor_currentlyPlayingAnimation = timelineViewerState.editor_selectedAnimation;
								animatorForModelForTimelineViewer->playAnimation((size_t)timelineViewerState.editor_currentlyPlayingAnimation, 0.0f, true);
							}

							animatorForModelForTimelineViewer->animationSpeed = 1.0f;		// Prevents any internal animator funny business
							animatorForModelForTimelineViewer->updateAnimation(timelineViewerState.editor_animationPlaybackTimestamp - animatorForModelForTimelineViewer->getCurrentTime() / animatorForModelForTimelineViewer->getCurrentAnimation()->getTicksPerSecond());
						}
						
						ImGui::TreePop();
					}
					else
					{
						timelineViewerState.editor_currentlyPlayingAnimation = false;
					}

					//
					// Animations State Machine Builder
					//
					ImGui::Separator();
					if (ImGui::TreeNode("Animation State Machine"))
					{
						//
						// Previewing the ASM
						//
						ImGui::Text("Preview ASM");
						if (timelineViewerState.animationStateMachineNodes.size() > 0)
						{
							if (ImGui::Button("Enter Preview ASM Mode##This is the button this time"))
							{
								// Reimport the model first (for ASM Preview Mode)
								routinePurgeTimelineViewerModel();
								nlohmann::json j = FileLoading::loadJsonFile(modelMetadataFname);
								routineCreateAndInsertInTheModel(modelMetadataFname.c_str(), j, false);

								// Load in all the variables
								timelineViewerState.editor_previewModeCurrentASMNode = timelineViewerState.animationStateMachineStartNode;
								timelineViewerState.editor_asmVarCopyForPreview = timelineViewerState.animationStateMachineVariables;

								// Change the state
								timelineViewerState.editor_isAnimationPlaying = false;
								timelineViewerState.editor_isASMPreviewMode = true;

								// Play the initial animation
								routinePlayCurrentASMAnimation();
							}
							ImGui::SameLine();
							ImGui::Text("(THIS WILL DISCARD CHANGES)");
						}
						else
							ImGui::Text("Add ASM Nodes to Enable Preview Mode");

						//
						// ASM Variables
						//
						ImGui::Spacing();
						ImGui::Text("Animation State Machine (ASM) Variables");

						static std::string asmVarNameCollisionsError = "";
						std::vector<ASMVariable>& animationStateMachineVariables = timelineViewerState.animationStateMachineVariables;
						if (ImGui::BeginTable("##ASM Var Details Table", 4))
						{
							ImGui::TableSetupColumn("ASM Var Name", ImGuiTableColumnFlags_WidthStretch);
							ImGui::TableSetupColumn("Var Type", ImGuiTableColumnFlags_WidthStretch);
							ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
							ImGui::TableSetupColumn("Delete button column ## ASM Variables", ImGuiTableColumnFlags_WidthFixed);

							for (size_t i = 0; i < animationStateMachineVariables.size(); i++)
							{
								ASMVariable& asmVariable = animationStateMachineVariables[i];
								ImGui::TableNextColumn();
								
								std::string varNameCopy = asmVariable.varName;
								if (ImGui::InputText(("##ASM Var Name" + std::to_string(i)).c_str(), &varNameCopy))
								{
									saveAndApplyChangesFlag |= true;

									if (!varNameCopy.empty())	// Only allow assignment if first varNameCopy is !empty
									{
										// I know, I know, it's bad practice, but I need to know if there are any name collisions!
										asmVarNameCollisionsError = "";
										for (size_t j = 0; j < animationStateMachineVariables.size(); j++)
										{
											if (j == i)
												continue;

											if (varNameCopy == animationStateMachineVariables[j].varName)
											{
												asmVarNameCollisionsError = varNameCopy;
											}
										}
										if (asmVarNameCollisionsError.empty())
										{
											// Update all references of the ASM transition conditions (@NOTE: they reference these variables by name)
											for (size_t j = 0; j < timelineViewerState.animationStateMachineNodes.size(); j++)
											{
												for (size_t l = 0; l < timelineViewerState.animationStateMachineNodes[j].transitionConditionGroups.size(); l++)
												{
													for (size_t k = 0; k < timelineViewerState.animationStateMachineNodes[j].transitionConditionGroups[l].transitionConditions.size(); k++)
													{
														ASMTransitionCondition& tranCondition = timelineViewerState.animationStateMachineNodes[j].transitionConditionGroups[l].transitionConditions[k];
														if (tranCondition.varName == asmVariable.varName)
														{
															tranCondition.varName = varNameCopy;	// Update to new variable.
														}
													}

													if (timelineViewerState.animationStateMachineNodes[j].varFloatBlend == asmVariable.varName)
														timelineViewerState.animationStateMachineNodes[j].varFloatBlend = varNameCopy;		// Update to new variable name.
												}
											}

											// Write to the varName if no name collisions are found.
											asmVariable.varName = varNameCopy;
										}
									}
								}

								int variableTypeAsInt = (int)asmVariable.variableType;
								ImGui::TableNextColumn();
								saveAndApplyChangesFlag |= ImGui::Combo(("##ASM Var Type" + std::to_string(i)).c_str(), &variableTypeAsInt, "Bool\0Int\0Float");
								asmVariable.variableType = (ASMVariable::ASMVariableType)variableTypeAsInt;

								switch (asmVariable.variableType)
								{
								case ASMVariable::ASMVariableType::BOOL:
								{
									bool valueAsBool = (bool)asmVariable.value;
									ImGui::TableNextColumn();
									saveAndApplyChangesFlag |= ImGui::Checkbox(("##ASM Var Value As Bool" + std::to_string(i)).c_str(), &valueAsBool);
									asmVariable.value = (float)valueAsBool;
								}
								break;

								case ASMVariable::ASMVariableType::INT:
								{
									int valueAsInt = (int)asmVariable.value;
									ImGui::TableNextColumn();
									saveAndApplyChangesFlag |= ImGui::InputInt(("##ASM Var Value As Int" + std::to_string(i)).c_str(), &valueAsInt);
									asmVariable.value = (float)valueAsInt;
								}
								break;

								case ASMVariable::ASMVariableType::FLOAT:
								{
									ImGui::TableNextColumn();
									saveAndApplyChangesFlag |= ImGui::DragFloat(("##ASM Var Value As Float" + std::to_string(i)).c_str(), &asmVariable.value, 0.1f);
								}
								break;
								}

								// Delete Button
								ImGui::TableNextColumn();
								if (ImGui::Button(("X##DELETE BUTTON FOR ASM VAR" + std::to_string(i)).c_str()))
								{
									saveAndApplyChangesFlag |= true;
									animationStateMachineVariables.erase(animationStateMachineVariables.begin() + i);
									break;	// To prevent further iteration and crashing the program
								}
							}

							ImGui::EndTable();
						}

						if (!asmVarNameCollisionsError.empty())
						{
							ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ("ERROR: name \"" + asmVarNameCollisionsError + "\" collides").c_str());
						}

						if (ImGui::Button("Add new ASM var"))
						{
							saveAndApplyChangesFlag |= true;
							ASMVariable newASMVar;
							newASMVar.varName = "New Variable";

							bool varNameCollides;
							int suffix = 1;
							do
							{
								varNameCollides = false;
								for (size_t i = 0; i < animationStateMachineVariables.size(); i++)
								{
									if (newASMVar.varName == animationStateMachineVariables[i].varName)
									{
										varNameCollides = true;
										break;
									}
								}

								if (varNameCollides)
									newASMVar.varName = "New Variable" + std::to_string(suffix++);
							}
							while (varNameCollides);

							animationStateMachineVariables.push_back(newASMVar);
						}


						//
						// ASM Nodes
						//
        				ImGui::Spacing();
						ImGui::Text("Animation State Machine (ASM) Nodes");

						std::string selectedASMNodeLabelText = "Select...";
						if (timelineViewerState.editor_selectedASMNode >= 0)
						{
							selectedASMNodeLabelText = timelineViewerState.animationStateMachineNodes[timelineViewerState.editor_selectedASMNode].nodeName;
						}
						if (ImGui::BeginCombo("Selected ASM Node", selectedASMNodeLabelText.c_str()))
						{
							if (ImGui::Selectable("Select...", timelineViewerState.editor_selectedASMNode == -1))
							{
								timelineViewerState.editor_selectedASMNode = -1;
							}

							for (size_t i = 0; i < timelineViewerState.animationStateMachineNodes.size(); i++)
							{
								const bool isSelected = (timelineViewerState.editor_selectedASMNode == i);
								if (ImGui::Selectable((timelineViewerState.animationStateMachineNodes[i].nodeName + (timelineViewerState.animationStateMachineStartNode == i ? " (START NODE)" : "")).c_str(), isSelected))
								{
									timelineViewerState.editor_selectedASMNode = i;

									//
									// Move all the nodes to easy to see spots
									//
									const float totalHeight = (timelineViewerState.editor_ASMNodePreviewerDelegate.mNodes.size() - 1) * 200.0f;
									float currentHeight = 0.0f;
									for (auto& mNode : timelineViewerState.editor_ASMNodePreviewerDelegate.mNodes)
									{
										if (mNode.name == timelineViewerState.animationStateMachineNodes[i].nodeName)
										{
											mNode.x = 400;
											mNode.y = totalHeight * 0.5f;
										}
										else
										{
											mNode.x = 0;
											mNode.y = currentHeight;
											currentHeight += 200.0f;
										}
									}

									//
									// Label only certain links as relevant
									//
									size_t relevantToNodeIndex = timelineViewerState.editor_ASMNodePreviewerDelegate.mNodeNameToIndex[timelineViewerState.animationStateMachineNodes[i].nodeName];
									for (auto& mLink : timelineViewerState.editor_ASMNodePreviewerDelegate.mLinks)
									{
										mLink.irrelevant = (mLink.mOutputNodeIndex != relevantToNodeIndex);
									}

									timelineViewerState.editor_flag_ASMNodePreviewerRunFitAllNodes = true;
								}

								// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
								if (isSelected)
									ImGui::SetItemDefaultFocus();
							}

							ImGui::EndCombo();
						}

						if (timelineViewerState.animationStateMachineStartNode >= timelineViewerState.animationStateMachineNodes.size())
						{
							saveAndApplyChangesFlag |= true;
							timelineViewerState.animationStateMachineStartNode = 0;
						}

						if (timelineViewerState.editor_selectedASMNode >= 0)
						{
							ImGui::SameLine();
							if (ImGui::Button("X## Delete the currently selected ASM Node"))
							{
								saveAndApplyChangesFlag |= true;
								timelineViewerState.animationStateMachineNodes.erase(
									timelineViewerState.animationStateMachineNodes.begin() + timelineViewerState.editor_selectedASMNode
								);
								timelineViewerState.editor_selectedASMNode = -1;
							}
						}

						static std::string createNewASMNodePopupInputText;
						static bool showASMNewNodeNameAlreadyExistsError = false;
						if (ImGui::Button("Create new ASM Node.."))
						{
							ImGui::OpenPopup("create_new_asm_node_popup");
							createNewASMNodePopupInputText = "";
							showASMNewNodeNameAlreadyExistsError = false;
						}
						if (ImGui::BeginPopup("create_new_asm_node_popup"))
						{
							if (ImGui::InputText("New ASM node Name", &createNewASMNodePopupInputText))
								showASMNewNodeNameAlreadyExistsError = false;

							if (!createNewASMNodePopupInputText.empty() && ImGui::Button("Create"))
							{
								// Check if name exists (I know it's super slow Jon)
								showASMNewNodeNameAlreadyExistsError = false;
								for (size_t i = 0; i < timelineViewerState.animationStateMachineNodes.size(); i++)
									if (timelineViewerState.animationStateMachineNodes[i].nodeName == createNewASMNodePopupInputText)
									{
										showASMNewNodeNameAlreadyExistsError = true;
										break;
									}

								if (!showASMNewNodeNameAlreadyExistsError)
								{
									saveAndApplyChangesFlag |= true;

									// Create the new Node
									auto newNode = ASMNode();
									newNode.nodeName = createNewASMNodePopupInputText;
									timelineViewerState.animationStateMachineNodes.push_back(newNode);
									timelineViewerState.editor_selectedASMNode = timelineViewerState.animationStateMachineNodes.size() - 1;
									ImGui::CloseCurrentPopup();
								}
							}

							if (showASMNewNodeNameAlreadyExistsError)
								ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Name already exists");

							ImGui::EndPopup();
						}

						if (timelineViewerState.editor_selectedASMNode >= 0)
						{
							//
							// Edit ASM Node Props
							//
							ImGui::Spacing();
							ImGui::Text("Edit ASM Node Properties");
							ASMNode& asmNode = timelineViewerState.animationStateMachineNodes[timelineViewerState.editor_selectedASMNode];

							if (timelineViewerState.editor_selectedASMNode == timelineViewerState.animationStateMachineStartNode)
							{
								ImGui::SameLine();
								ImGui::Text("(This is the START NODE)");
							}
							else
							{
								ImGui::SameLine();
								ImGui::Text("(This is NOT the START NODE)");
								ImGui::SameLine();
								if (ImGui::Button("Set as START NODE"))
								{
									saveAndApplyChangesFlag |= true;
									timelineViewerState.animationStateMachineStartNode = timelineViewerState.editor_selectedASMNode;
								}
							}

							static bool showASMChangeNodeNameAlreadyExistsError = false;
							std::string asmChangeNameText = asmNode.nodeName;
							if (ImGui::InputText("New ASM node Name", &asmChangeNameText))
							{
								saveAndApplyChangesFlag |= true;

								// Check if name exists (I know it's super slow Jon)
								showASMChangeNodeNameAlreadyExistsError = false;
								bool exists = false;
								for (size_t i = 0; i < timelineViewerState.animationStateMachineNodes.size(); i++)
									if (timelineViewerState.animationStateMachineNodes[i].nodeName == asmChangeNameText)
									{
										showASMChangeNodeNameAlreadyExistsError = true;
										break;
									}

								if (!exists && !asmChangeNameText.empty())	// @NOTE: we gotta prevent assigning empty strings too
								{
									// Go Thru all the references (Specifically from transitionconditions) to edit
									for (size_t i = 0; i < timelineViewerState.animationStateMachineNodes.size(); i++)
									{
										ASMNode& asmNodeRef = timelineViewerState.animationStateMachineNodes[i];
										for (size_t k = 0; k < asmNodeRef.transitionConditionGroups.size(); k++)
										{
											for (size_t j = 0; j < asmNodeRef.transitionConditionGroups[k].transitionConditions.size(); j++)
											{
												ASMTransitionCondition& asmTranConditionRef = asmNodeRef.transitionConditionGroups[k].transitionConditions[j];
												if (asmTranConditionRef.varName != ASMTransitionCondition::specialCaseKey)
													continue;

												if (CONTAINS_IN_VECTOR(asmTranConditionRef.specialCaseCurrentASMNodeNames, asmNode.nodeName))
												{
													REMOVE_FROM_VECTOR(asmTranConditionRef.specialCaseCurrentASMNodeNames, asmNode.nodeName);
													asmTranConditionRef.specialCaseCurrentASMNodeNames.push_back(asmChangeNameText);
												}
											}
										}
									}

									// Assign the new name
									asmNode.nodeName = asmChangeNameText;
								}
							}

							// animationName1
							std::vector<AnimationNameAndIncluded>& animationNameAndIncluded = timelineViewerState.animationNameAndIncluded;
							if (ImGui::BeginCombo("Selected Animation 1", asmNode.animationName1.size() == 0 ? "Select..." : asmNode.animationName1.c_str()))
							{
								if (ImGui::Selectable("Select...", asmNode.animationName1.size() == 0))
								{
									saveAndApplyChangesFlag |= true;
									asmNode.animationName1 = "";
								}

								for (size_t i = 0; i < animationNameAndIncluded.size(); i++)
								{
									if (!animationNameAndIncluded[i].included)
										continue;

									const bool isSelected = (asmNode.animationName1 == animationNameAndIncluded[i].name);
									if (ImGui::Selectable(animationNameAndIncluded[i].name.c_str(), isSelected))
									{
										saveAndApplyChangesFlag |= true;
										asmNode.animationName1 = animationNameAndIncluded[i].name;
									}

									// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
									if (isSelected)
										ImGui::SetItemDefaultFocus();
								}

								ImGui::EndCombo();
							}

							// animationName2  @COPYPASTA
							if (ImGui::BeginCombo("Selected Animation 2", asmNode.animationName2.size() == 0 ? "Select..." : asmNode.animationName2.c_str()))
							{
								if (ImGui::Selectable("Select...", asmNode.animationName2.size() == 0))
								{
									saveAndApplyChangesFlag |= true;
									asmNode.animationName2 = "";
								}

								for (size_t i = 0; i < animationNameAndIncluded.size(); i++)
								{
									if (!animationNameAndIncluded[i].included)
										continue;

									const bool isSelected = (asmNode.animationName2 == animationNameAndIncluded[i].name);
									if (ImGui::Selectable(animationNameAndIncluded[i].name.c_str(), isSelected))
									{
										saveAndApplyChangesFlag |= true;
										asmNode.animationName2 = animationNameAndIncluded[i].name;
									}

									// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
									if (isSelected)
										ImGui::SetItemDefaultFocus();
								}

								ImGui::EndCombo();
							}

							if (asmNode.animationName2.size() != 0)
							{
								// Choose the blendtree variable
								std::vector<ASMVariable>& animationStateMachineVariables = timelineViewerState.animationStateMachineVariables;
								if (ImGui::BeginCombo("Blendtree Variable", asmNode.varFloatBlend.size() == 0 ? "Select..." : asmNode.varFloatBlend.c_str()))
								{
									if (ImGui::Selectable("Select...", asmNode.varFloatBlend.size() == 0))
									{
										saveAndApplyChangesFlag |= true;
										asmNode.varFloatBlend = "";
									}

									for (size_t i = 0; i < animationStateMachineVariables.size(); i++)
									{
										const bool isSelected = (asmNode.varFloatBlend == animationStateMachineVariables[i].varName);
										if (ImGui::Selectable(animationStateMachineVariables[i].varName.c_str(), isSelected))
										{
											saveAndApplyChangesFlag |= true;
											asmNode.varFloatBlend = animationStateMachineVariables[i].varName;
										}

										// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
										if (isSelected)
											ImGui::SetItemDefaultFocus();
									}

									ImGui::EndCombo();
								}

								// Choose blendtree float boundaries (Default is 0-1)
								saveAndApplyChangesFlag |= ImGui::DragFloat("Blendtree Variable Bound 1", &asmNode.animationBlend1, 0.1f);
								saveAndApplyChangesFlag |= ImGui::DragFloat("Blendtree Variable Bound 2", &asmNode.animationBlend2, 0.1f);
							}
							else
								asmNode.varFloatBlend = "";
							
							saveAndApplyChangesFlag |= ImGui::Checkbox("Loop Animation##ASM Node Prop", &asmNode.loopAnimation);
							saveAndApplyChangesFlag |= ImGui::Checkbox("Don't Transition Until Animation is Finished##ASM Node Prop", &asmNode.doNotTransitionUntilAnimationFinished);
							saveAndApplyChangesFlag |= ImGui::DragFloat("Animation Transition Time##ASM Node Prop", &asmNode.transitionTime);

							if (showASMChangeNodeNameAlreadyExistsError)
								ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Name already exists");



							//
							// ASM Node Transition Conditions
							//
							ImGui::Spacing();
							ImGui::Text("Edit ASM Node Transition Conditions");
							
							for (size_t a = 0; a < asmNode.transitionConditionGroups.size(); a++)
							{
								if (ImGui::TreeNode(("ASM Transition Condition Group " + std::to_string(a)).c_str()))
								{
									std::vector<ASMTransitionCondition>& transitionConditions = asmNode.transitionConditionGroups[a].transitionConditions;
									if (ImGui::BeginTable("##ASM Transition Conditions Table", 4))
									{
										ImGui::TableSetupColumn("Var Name Ref", ImGuiTableColumnFlags_WidthStretch);
										ImGui::TableSetupColumn("Compare Operator", ImGuiTableColumnFlags_WidthStretch);
										ImGui::TableSetupColumn("Compare Value", ImGuiTableColumnFlags_WidthStretch);
										ImGui::TableSetupColumn("Delete button column ## ASM Transition Conditions", ImGuiTableColumnFlags_WidthFixed);

										for (size_t i = 0; i < transitionConditions.size(); i++)
										{
											ASMTransitionCondition& tranCondition = transitionConditions[i];

											// Variable assignment
											ImGui::TableNextColumn();
											bool isSpecialCaseCurrentASMNodeNameVarName = (tranCondition.varName == ASMTransitionCondition::specialCaseKey);
											std::string tranConditionVarNameReferencePreviewText = isSpecialCaseCurrentASMNodeNameVarName ? "{Current ASM Node}" : tranCondition.varName.empty() ? "Select..." : tranCondition.varName;
											if (ImGui::BeginCombo(("##ASM Transition Condition Var Name Reference" + std::to_string(i)).c_str(), tranConditionVarNameReferencePreviewText.c_str()))
											{
												if (ImGui::Selectable("Select...", tranCondition.varName.empty()))
												{
													saveAndApplyChangesFlag |= true;
													tranCondition.varName = "";
												}

												if (ImGui::Selectable("{Current ASM Node}", isSpecialCaseCurrentASMNodeNameVarName))
												{
													saveAndApplyChangesFlag |= true;
													tranCondition.varName = ASMTransitionCondition::specialCaseKey;
													isSpecialCaseCurrentASMNodeNameVarName = true;
												}

												bool resetVarName = !isSpecialCaseCurrentASMNodeNameVarName;
												for (size_t j = 0; j < animationStateMachineVariables.size(); j++)
												{
													ASMVariable& asmVariable = animationStateMachineVariables[j];
													const bool isSelected = (asmVariable.varName == tranCondition.varName);
													if (ImGui::Selectable(asmVariable.varName.c_str(), isSelected))
													{
														saveAndApplyChangesFlag |= true;
														tranCondition.varName = asmVariable.varName;
														resetVarName = false;
													}

													if (isSelected)
													{
														ImGui::SetItemDefaultFocus();
														resetVarName = false;
													}
												}
												if (resetVarName)
													tranCondition.varName = "";		// @NOTE: will need to both make this check upon saving and make sure that the relationship doesn't get cut off  -Timo

												ImGui::EndCombo();
											}

											// Comparison operator
											ImGui::TableNextColumn();
											int comparisonOperatorAsInt = (int)tranCondition.comparisonOperator;
											if (isSpecialCaseCurrentASMNodeNameVarName)
											{
												saveAndApplyChangesFlag |= ImGui::Combo(("##ASM Transition Condition Comparison Operator" + std::to_string(i)).c_str(), &comparisonOperatorAsInt, "EQUAL");
												if (comparisonOperatorAsInt > (int)ASMTransitionCondition::ASMComparisonOperator::EQUAL)
												{
													saveAndApplyChangesFlag |= true;
													comparisonOperatorAsInt = 0;
												}
											}
											else
											{
												saveAndApplyChangesFlag |= ImGui::Combo(("##ASM Transition Condition Comparison Operator" + std::to_string(i)).c_str(), &comparisonOperatorAsInt, "EQUAL\0NEQUAL\0LESSER\0GREATER\0LEQUAL\0GEQUAL");
											}
											tranCondition.comparisonOperator = (ASMTransitionCondition::ASMComparisonOperator)comparisonOperatorAsInt;

											if (isSpecialCaseCurrentASMNodeNameVarName)
											{
												tranCondition.compareToValue = 0.0f;

												// List all of the ASM Nodes to choose from
												ImGui::TableNextColumn();
												if (ImGui::Button(("Edit Nodes..###ASM Transition Condition Special Case Selected ASM Nodes" + std::to_string(i)).c_str()))
													ImGui::OpenPopup(("###ASM Transition Condition Special Case Selected ASM Nodes" + std::to_string(i)).c_str());
												if (ImGui::BeginPopup(("###ASM Transition Condition Special Case Selected ASM Nodes" + std::to_string(i)).c_str()))
												{
													for (size_t j = 0; j < timelineViewerState.animationStateMachineNodes.size(); j++)
													{
														ASMNode& asmNodeRef = timelineViewerState.animationStateMachineNodes[j];
														//const bool isSelected = (asmNodeRef.nodeName == tranCondition.specialCaseCurrentASMNodeName);
														bool isSelected = CONTAINS_IN_VECTOR(tranCondition.specialCaseCurrentASMNodeNames, asmNodeRef.nodeName);
														if (ImGui::Checkbox((asmNodeRef.nodeName + "##ASM Transition Condition Special Case Selector Checkbox " + std::to_string(i)).c_str(), &isSelected))
														{
															saveAndApplyChangesFlag |= true;
															if (isSelected)
																tranCondition.specialCaseCurrentASMNodeNames.push_back(asmNodeRef.nodeName);
															else
																REMOVE_FROM_VECTOR(tranCondition.specialCaseCurrentASMNodeNames, asmNodeRef.nodeName);
														}
													}
													ImGui::EndMenu();
												}
												// if (ImGui::BeginCombo(("##ASM Transition Condition Special Case Current ASM Node Combo box" + std::to_string(i)).c_str(), tranCondition.specialCaseCurrentASMNodeName.empty() ? "Select..." : tranCondition.specialCaseCurrentASMNodeName.c_str()))
												// {
												// 	if (ImGui::Selectable("Select...", tranCondition.specialCaseCurrentASMNodeName.empty()))
												// 	{
												// 		saveAndApplyChangesFlag |= true;
												// 		tranCondition.specialCaseCurrentASMNodeName = "";
												// 	}

												// 	for (size_t j = 0; j < timelineViewerState.animationStateMachineNodes.size(); j++)
												// 	{
												// 		ASMNode& asmNodeRef = timelineViewerState.animationStateMachineNodes[j];
												// 		const bool isSelected = (asmNodeRef.nodeName == tranCondition.specialCaseCurrentASMNodeName);
												// 		if (ImGui::Selectable((asmNodeRef.nodeName + "##ASM Transition Condition Special Case " + std::to_string(i)).c_str(), isSelected))
												// 		{
												// 			saveAndApplyChangesFlag |= true;
												// 			tranCondition.specialCaseCurrentASMNodeName = asmNodeRef.nodeName;
												// 		}

												// 		if (isSelected)
												// 			ImGui::SetItemDefaultFocus();
												// 	}

												// 	ImGui::EndCombo();
												// }
											}
											else
											{
												tranCondition.specialCaseCurrentASMNodeNames.clear();

												// Find the variable with .varName
												ASMVariable* tempVarPtr = nullptr;
												for (size_t j = 0; j < animationStateMachineVariables.size(); j++)
												{
													if (animationStateMachineVariables[j].varName == tranCondition.varName)
													{
														tempVarPtr = &animationStateMachineVariables[j];
														break;
													}
												}
												if (tempVarPtr != nullptr)
												{
													switch (tempVarPtr->variableType)
													{
													case ASMVariable::ASMVariableType::BOOL:
													{
														bool valueAsBool = (bool)tranCondition.compareToValue;
														ImGui::TableNextColumn();
														saveAndApplyChangesFlag |= ImGui::Checkbox(("##ASM Transition Condition Var Value As Bool" + std::to_string(i)).c_str(), &valueAsBool);
														tranCondition.compareToValue = (float)valueAsBool;
													}
													break;

													case ASMVariable::ASMVariableType::INT:
													{
														int valueAsInt = (int)tranCondition.compareToValue;
														ImGui::TableNextColumn();
														saveAndApplyChangesFlag |= ImGui::InputInt(("##ASM Transition Condition Var Value As Int" + std::to_string(i)).c_str(), &valueAsInt);
														tranCondition.compareToValue = (float)valueAsInt;
													}
													break;

													case ASMVariable::ASMVariableType::FLOAT:
													{
														ImGui::TableNextColumn();
														saveAndApplyChangesFlag |= ImGui::DragFloat(("##ASM Transition Condition Var Value As Float" + std::to_string(i)).c_str(), &tranCondition.compareToValue, 0.1f);
													}
													break;
													}
												}
												else
												{
													ImGui::TableNextColumn();
													ImGui::Text("Select ASM variable");
												}
											}

											// Delete Button
											ImGui::TableNextColumn();
											if (ImGui::Button(("X##DELETE BUTTON FOR ASM TRANSITION CONDITIONS" + std::to_string(i)).c_str()))
											{
												saveAndApplyChangesFlag |= true;
												transitionConditions.erase(transitionConditions.begin() + i);
												break;	// To prevent further iteration and crashing the program
											}
										}

										ImGui::EndTable();
									}

									if (ImGui::Button("Add new ASM Transition Condition"))
									{
										saveAndApplyChangesFlag |= true;
										transitionConditions.push_back(ASMTransitionCondition());
									}

									if (ImGui::Button("[X] Delete this ASM Transition Condition Group"))
									{
										saveAndApplyChangesFlag |= true;
										asmNode.transitionConditionGroups.erase(asmNode.transitionConditionGroups.begin() + a);
									}

									ImGui::TreePop();
								}
							}

							if (ImGui::Button("Add new ASM Transition Condition Group"))
							{
								asmNode.transitionConditionGroups.push_back({});
							}
						}

						ImGui::TreePop();
					}
					else
					{
						timelineViewerState.editor_isASMPreviewMode = false;
					}
				}
			}
			else if (MainLoop::getInstance().timelineViewerMode && timelineViewerState.editor_isASMPreviewMode)
			{
				//
				// TimelineViewerMode Preview ASM Mode
				//
				if (ImGui::Button("Exit Preview ASM Mode"))
				{
					// Reimport the model now (for ASM Preview Mode)
					routinePurgeTimelineViewerModel();
					nlohmann::json j = FileLoading::loadJsonFile(modelMetadataFname);
					routineCreateAndInsertInTheModel(modelMetadataFname.c_str(), j, true);

					// Change the state
					timelineViewerState.editor_isASMPreviewMode = false;
				}

				//
				// Simulate the ASM with the relevant information
				// and the copy of the variables
				//
				if (timelineViewerState.editor_isASMPreviewMode && animatorForModelForTimelineViewer != nullptr)
				{
					const size_t& currentNode = timelineViewerState.editor_previewModeCurrentASMNode;

					if (!timelineViewerState.editor_ASMPreviewModeIsPaused)
					{
						bool checkTransitionConditions = true;
						if (timelineViewerState.animationStateMachineNodes[currentNode].doNotTransitionUntilAnimationFinished)
						{
							// @NOTE: With blend trees too, only animationName1 is used for isAnimationFinished()
							size_t i = 0;
							for (auto anai : timelineViewerState.animationNameAndIncluded)
							{
								if (!anai.included)
									continue;
								if (anai.name == timelineViewerState.animationStateMachineNodes[currentNode].animationName1)
									break;
								i++;
							}
							checkTransitionConditions = animatorForModelForTimelineViewer->isAnimationFinished(i, MainLoop::getInstance().deltaTime);
						}

						if (checkTransitionConditions)
						{
							// Check all other nodes for transition conditions and go to the first one fulfilled
							for (size_t i = 0; i < timelineViewerState.animationStateMachineNodes.size(); i++)
							{
								if (i == currentNode)
									continue;

								bool tranGroupPassed = false;
								for (size_t j = 0; j < timelineViewerState.animationStateMachineNodes[i].transitionConditionGroups.size(); j++)
								{
									bool tranConditionPasses = true;
									for (size_t k = 0; k < timelineViewerState.animationStateMachineNodes[i].transitionConditionGroups[j].transitionConditions.size(); k++)
									{
										ASMTransitionCondition& tranCondition = timelineViewerState.animationStateMachineNodes[i].transitionConditionGroups[j].transitionConditions[k];
										
										// Get Variable Value
										float compareToValue;
										bool isCompareCurrentNode = (tranCondition.varName == ASMTransitionCondition::specialCaseKey);
										if (!isCompareCurrentNode)
										{
											for (size_t k = 0; k < timelineViewerState.editor_asmVarCopyForPreview.size(); k++)
											{
												ASMVariable& asmVariable = timelineViewerState.editor_asmVarCopyForPreview[k];
												if (asmVariable.varName == tranCondition.varName)
												{
													compareToValue = asmVariable.value;
													break;
												}
											}
										}

										// Compare the values
										switch (tranCondition.comparisonOperator)
										{
										case ASMTransitionCondition::ASMComparisonOperator::EQUAL:
											if (isCompareCurrentNode)
												tranConditionPasses &= CONTAINS_IN_VECTOR(tranCondition.specialCaseCurrentASMNodeNames, timelineViewerState.animationStateMachineNodes[currentNode].nodeName);
											else
												tranConditionPasses &= (compareToValue == tranCondition.compareToValue);
											break;

										case ASMTransitionCondition::ASMComparisonOperator::NEQUAL:
											tranConditionPasses &= (compareToValue != tranCondition.compareToValue);
											break;

										case ASMTransitionCondition::ASMComparisonOperator::LESSER:
											tranConditionPasses &= (compareToValue < tranCondition.compareToValue);
											break;

										case ASMTransitionCondition::ASMComparisonOperator::GREATER:
											tranConditionPasses &= (compareToValue > tranCondition.compareToValue);
											break;

										case ASMTransitionCondition::ASMComparisonOperator::LEQUAL:
											tranConditionPasses &= (compareToValue <= tranCondition.compareToValue);
											break;

										case ASMTransitionCondition::ASMComparisonOperator::GEQUAL:
											tranConditionPasses &= (compareToValue >= tranCondition.compareToValue);
											break;
										
										default:
											std::cout << "ERROR: The comparison operator doesn't exist" << std::endl;
											break;
										}

										if (!tranConditionPasses)
											break;
									}

									if (tranConditionPasses)
									{
										// Switch to this new node
										timelineViewerState.editor_previewModeCurrentASMNode = i;
										if (timelineViewerState.animationStateMachineNodes[timelineViewerState.editor_previewModeCurrentASMNode].animationName1.empty())
											std::cout << "ASM: ERROR: animationName1 is empty" << std::endl;
										else
											// Start playing the new animation
											routinePlayCurrentASMAnimation();

										tranGroupPassed = true;
										break;
									}
								}

								if (tranGroupPassed)
									break;
							}
						}

						// Update the animation with the animator
						if (!timelineViewerState.animationStateMachineNodes[timelineViewerState.editor_previewModeCurrentASMNode].varFloatBlend.empty())
						{
							float blendVarValue = 0.0f;
							for (size_t k = 0; k < timelineViewerState.editor_asmVarCopyForPreview.size(); k++)
							{
								ASMVariable& asmVariable = timelineViewerState.editor_asmVarCopyForPreview[k];
								if (asmVariable.varName == timelineViewerState.animationStateMachineNodes[timelineViewerState.editor_previewModeCurrentASMNode].varFloatBlend)
								{
									blendVarValue = asmVariable.value;
									break;
								}
							}
							animatorForModelForTimelineViewer->setBlendTreeVariable("blendVar", blendVarValue);
						}
						animatorForModelForTimelineViewer->animationSpeed = 1.0f;
						animatorForModelForTimelineViewer->updateAnimation(MainLoop::getInstance().deltaTime);
					}

					//
					// Report back the current node in preview
					//
					ImGui::Text(("Current Node: " + timelineViewerState.animationStateMachineNodes[currentNode].nodeName).c_str());
					if (ImGui::Button(timelineViewerState.editor_ASMPreviewModeIsPaused ? "Click to Resume" : "Click to Pause"))
						timelineViewerState.editor_ASMPreviewModeIsPaused = !timelineViewerState.editor_ASMPreviewModeIsPaused;

					//
					// Edit the copied variables
					//
					ImGui::Spacing();
					if (ImGui::TreeNode("Edit Preview Variables (NOTE: These do not change the saved values)"))
					{
						for (size_t i = 0; i < timelineViewerState.editor_asmVarCopyForPreview.size(); i++)
						{
							ASMVariable& asmVariable = timelineViewerState.editor_asmVarCopyForPreview[i];
							switch (asmVariable.variableType)
							{
							case ASMVariable::ASMVariableType::BOOL:
							{
								bool valueAsBool = (bool)asmVariable.value;
								ImGui::Checkbox((asmVariable.varName + "##ASM Preview Mode Var Value As Bool" + std::to_string(i)).c_str(), &valueAsBool);
								asmVariable.value = (float)valueAsBool;
							}
							break;

							case ASMVariable::ASMVariableType::INT:
							{
								int valueAsInt = (int)asmVariable.value;
								ImGui::InputInt((asmVariable.varName + "##ASM Preview Mode Var Value As Int" + std::to_string(i)).c_str(), &valueAsInt);
								asmVariable.value = (float)valueAsInt;
							}
							break;

							case ASMVariable::ASMVariableType::FLOAT:
							{
								ImGui::DragFloat((asmVariable.varName + "##ASM Preview Mode Var Value As Float" + std::to_string(i)).c_str(), &asmVariable.value, 0.1f);
							}
							break;
							}
						}

						ImGui::TreePop();
					}

					ImGui::Separator();
				}
			}
			else
			{
				//
				// Enter into timeline viewer mode with a model
				//
				if (ImGui::Button("Edit Model Metadata in Timeline..."))
				{
					// Open that file
					const char* filters[] = { "*.hsmm" };
					char* path = FileLoading::openFileDialog("Open Model for Timeline Editing", "res/model/", filters, "Game Model Metadata Files (*.hsmm)");
					if (path)
					{
						nlohmann::json j = FileLoading::loadJsonFile(path);

						if (j.contains("model_path"))
						{
							routineCreateAndInsertInTheModel(path, j, true);
							const static glm::vec3 camPos = { -4.132358074188232, 4.923120498657227, 9.876399993896484 };
							MainLoop::getInstance().camera.position = camPos;
							MainLoop::getInstance().camera.orientation = glm::normalize(-camPos + glm::vec3(0.0f, 1.5f, 0.0f));
							MainLoop::getInstance().timelineViewerMode = true;
						}
						else
						{
							std::cout << "TIMELINE VIEWER: ERROR: Model path \"" << path << "\" Does not contain key 'model_path'." << std::endl;
						}
					}
					else
					{
						std::cout << "TIMELINE VIEWER: ERROR: Model path not selected." << std::endl;
					}
				}

				if (ImGui::Button("Create new Model Metadata in Timeline..."))
				{
					// Create a new file
					const char* filters[] = { "*.hsmm" };
					char* path = FileLoading::saveFileDialog("Create Model Metadata for Timeline Editing", "res/model/", filters, "Game Model Metadata Files (*.hsmm)");
					if (path)
					{
						nlohmann::json j;
						FileLoading::saveJsonFile(path, j);

						// Load a model
						const char* modelFilters[] = { "*.glb" };
						char* modelPath = FileLoading::openFileDialog("Open GLTF Binary Model", "res/model/", modelFilters, "GLTF Binary Model (*.glb)");

						if (modelPath)
						{
							j["model_path"] = std::filesystem::relative(modelPath).string();
							FileLoading::saveJsonFile(path, j);

							routineCreateAndInsertInTheModel(path, j, true);
							MainLoop::getInstance().timelineViewerMode = true;
						}
						else
						{
							std::cout << "TIMELINE VIEWER: ERROR: Model path \"" << path << "\" Does not exist." << std::endl;
						}
					}
					else
					{
						std::cout << "TIMELINE VIEWER: ERROR: Saving path does not exist." << std::endl;
					}
				}
			}
		}
		ImGui::End();

		//
		// Timeline Nodes Viewer
		//
		if (MainLoop::getInstance().timelineViewerMode && timelineViewerState.editor_selectedASMNode >= 0)
		{
			if (ImGui::Begin("Timeline Nodes Viewer"))
			{
				// Graph Editor
				static GraphEditor::Options options;
				options.mMinimap = ImRect(ImVec2(0, 0), ImVec2(0, 0));
				static GraphEditor::ViewState viewState;
				static GraphEditor::FitOnScreen fit = GraphEditor::Fit_None;
				if (ImGui::Button("Fit all nodes") || timelineViewerState.editor_flag_ASMNodePreviewerRunFitAllNodes)
				{
					timelineViewerState.editor_flag_ASMNodePreviewerRunFitAllNodes = false;
					fit = GraphEditor::Fit_AllNodes;
				}
				ImGui::SameLine();
				if (ImGui::Button("Fit selected nodes"))
				{
					fit = GraphEditor::Fit_SelectedNodes;
				}
				GraphEditor::Show(timelineViewerState.editor_ASMNodePreviewerDelegate, options, viewState, true, &fit);
			}
			ImGui::End();
		}
	} 
}
#endif

void RenderManager::renderText(TextRenderer& tr)		// @Cleanup: this needs to go in some kind of text utils... it's super useful however, soooooo.
{
	// Activate corresponding render state
	text_program_id->use();
	text_program_id->setVec3("diffuseTint", tr.color);
	text_program_id->setMat4("modelMatrix", tr.modelMatrix);

	// Check to see what the width of the text is
	float x = 0;
	if (tr.horizontalAlign != TextAlignment::LEFT)
	{
		for (std::string::const_iterator c = tr.text.begin(); c != tr.text.end(); c++)
			x += (characters[*c].advanceX >> 6); // @NOTE: the advance of the glyph is the amount to move to get the next glyph. ALSO, bitshift by 6 to get value in pixels (2^6 = 64)
		
		if (tr.horizontalAlign == TextAlignment::CENTER)
			x = -x / 2.0f;		// NOTE: x is total width rn so we're using total_width to calculate the starting point of the glyph drawing.
		else if (tr.horizontalAlign == TextAlignment::RIGHT)
			x = -x;				// NOTE: this means that the start of the text will be -total_width
	}

	// Check to see what the total height of the text is	@TODO: @FIXME: FOR SOME REASON THIS DOESN'T WORK?????!?!??!!?!?
	float y = 0;
	if (tr.verticalAlign != TextAlignment::TOP)
	{
		// @TODO: be able to support multiple lines here... this solution atm only supports one line
		/*for (*/ std::string::const_iterator c = tr.text.begin();// c != tr.text.end(); c++)
			y += (characters[*c].advanceY >> 6);

		if (tr.verticalAlign == TextAlignment::CENTER)
			y = y / 2.0f;		// NOTE: positive bc we scoot negatively here
		else if (tr.verticalAlign == TextAlignment::BOTTOM)
			y = y;				// @TODO: this is redundant. Lol.
	}

	// iterate through all characters
	glBindVertexArray(textVAO);
	std::string::const_iterator c;
	for (c = tr.text.begin(); c != tr.text.end(); c++)
	{
		TextCharacter ch = characters[*c];

		float xpos = x + ch.bearing.x;
		float ypos = y - (ch.size.y - ch.bearing.y);

		float w = (float)ch.size.x;
		float h = (float)ch.size.y;
		// update VBO for each character
		float vertices[6][4] = {
			{ xpos,     ypos + h,   0.0f, 0.0f },
			{ xpos,     ypos,       0.0f, 1.0f },
			{ xpos + w, ypos,       1.0f, 1.0f },

			{ xpos,     ypos + h,   0.0f, 0.0f },
			{ xpos + w, ypos,       1.0f, 1.0f },
			{ xpos + w, ypos + h,   1.0f, 0.0f }
		};
		// render glyph texture over quad
		glBindTextureUnit(0, ch.textureId);
		// update content of VBO memory
		glBindBuffer(GL_ARRAY_BUFFER, textVBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		// render quad
		glDrawArrays(GL_TRIANGLES, 0, 6);
		// now advance cursors for next glyph (note that advance is number of 1/64 pixels)
		x += (ch.advanceX >> 6); // bitshift by 6 to get value in pixels (2^6 = 64)
	}
	glBindVertexArray(0);
}

#ifdef _DEVELOP
void RenderManager::createPickingBuffer()			// @Copypasta with createHDRBuffer()
{
	glGenFramebuffers(1, &pickingFBO);
	// Create floating point color buffer
	glGenTextures(1, &pickingColorBuffer);
	glBindTexture(GL_TEXTURE_2D, pickingColorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height, 0, GL_RED, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// Create depth buffer (renderbuffer)
	glGenRenderbuffers(1, &pickingRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, pickingRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, (GLsizei)MainLoop::getInstance().camera.width, (GLsizei)MainLoop::getInstance().camera.height);
	// Attach buffers
	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickingColorBuffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickingRBO);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete! (picking Framebuffer)" << std::endl;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderManager::destroyPickingBuffer()
{
	glDeleteTextures(1, &pickingColorBuffer);
	glDeleteRenderbuffers(1, &pickingRBO);
	glDeleteFramebuffers(1, &pickingFBO);
}

PixelInfo RenderManager::readPixelFromPickingBuffer(uint32_t x, uint32_t y)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, pickingFBO);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	PixelInfo pixel;
	glReadPixels(x, y, 1, 1, GL_RED, GL_FLOAT, &pixel);

	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	return pixel;
}
#endif

void RenderManager::createSkeletalAnimationUBO()
{
	glCreateBuffers(1, &skeletalAnimationUBO);
	glNamedBufferData(skeletalAnimationUBO, sizeof(glm::mat4x4) * 100, nullptr, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, skeletalAnimationUBO);
}

void RenderManager::destroySkeletalAnimationUBO()
{
	glDeleteBuffers(1, &skeletalAnimationUBO);
}

void RenderManager::INTERNALupdateSkeletalBonesUBO(const std::vector<glm::mat4>* boneTransforms)
{
	if (!repopulateAnimationUBO && boneTransforms == assignedBoneMatricesMemAddr)
		return;

	assignedBoneMatricesMemAddr = boneTransforms;

	glNamedBufferSubData(skeletalAnimationUBO, 0, sizeof(glm::mat4x4) * boneTransforms->size(), &(*boneTransforms)[0]);
}

void RenderManager::createLightInformationUBO()
{
	glCreateBuffers(1, &lightInformationUBO);
	glNamedBufferData(lightInformationUBO, sizeof(lightInformation), nullptr, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 2, lightInformationUBO);
}

void RenderManager::updateLightInformationUBO()
{
	bool TEMPJOJOJOJOJOJOO = false;

	// Insert lights into the struct for the UBO
	int shadowIndex = 0;
	const size_t numLights = std::min((size_t)RenderLightInformation::MAX_LIGHTS, MainLoop::getInstance().lightObjects.size());
	for (size_t i = 0; i < numLights; i++)
	{
		LightComponent* light = MainLoop::getInstance().lightObjects[i];
		glm::vec3 lightDirection = glm::vec3(0.0f);

		if (light->lightType != LightType::POINT)
			lightDirection = glm::normalize(light->facingDirection);	// NOTE: If there is no direction (magnitude: 0), then that means it's a point light ... Check this first in the shader

		glm::vec4 lightPosition = glm::vec4(glm::vec3(light->baseObject->getTransform()[3]), light->lightType == LightType::DIRECTIONAL ? 0.0f : 1.0f);		// NOTE: when a directional light, position doesn't matter, so that's indicated with the w of the vec4 to be 0
		glm::vec3 lightColorWithIntensity = light->color * light->colorIntensity;

		lightInformation.lightPositions[i] = lightPosition;
		lightInformation.lightDirections[i] = glm::vec4(lightDirection, 0);
		lightInformation.lightColors[i] = glm::vec4(lightColorWithIntensity, 0.0f);

		if (!TEMPJOJOJOJOJOJOO && light->lightType == LightType::DIRECTIONAL)
		{
			// MAIN DIREC LUIGHT
			sunColorForClouds = lightColorWithIntensity;
			TEMPJOJOJOJOJOJOO = true;
		}

		if (shadowIndex < ShaderExtShadow::MAX_SHADOWS && light->castsShadows)
		{
			lightInformation.lightDirections[i].a = 1;
			shadowIndex++;
		}
	}
	lightInformation.viewPosition = glm::vec4(MainLoop::getInstance().camera.position, 0.0f);
	lightInformation.numLightsToRender = glm::ivec4(numLights, 0, 0, 0);

	// Stuff into the UBO
	const GLintptr lightPosOffset =		sizeof(glm::vec4) * RenderLightInformation::MAX_LIGHTS * 0;
	const GLintptr lightDirOffset =		sizeof(glm::vec4) * RenderLightInformation::MAX_LIGHTS * 1;
	const GLintptr lightColOffset =		sizeof(glm::vec4) * RenderLightInformation::MAX_LIGHTS * 2;
	const GLintptr viewPosOffset =		sizeof(glm::vec4) * RenderLightInformation::MAX_LIGHTS * 3;
	const GLintptr numLightsOffset =	sizeof(glm::vec4) * RenderLightInformation::MAX_LIGHTS * 3 + sizeof(glm::vec4);

	glNamedBufferSubData(lightInformationUBO, lightPosOffset, sizeof(glm::vec4) * numLights, &lightInformation.lightPositions[0]);
	glNamedBufferSubData(lightInformationUBO, lightDirOffset, sizeof(glm::vec4) * numLights, &lightInformation.lightDirections[0]);
	glNamedBufferSubData(lightInformationUBO, lightColOffset, sizeof(glm::vec4) * numLights, &lightInformation.lightColors[0]);
	glNamedBufferSubData(lightInformationUBO, viewPosOffset, sizeof(glm::vec4), glm::value_ptr(lightInformation.viewPosition));
	glNamedBufferSubData(lightInformationUBO, numLightsOffset, sizeof(lightInformation.numLightsToRender), &lightInformation.numLightsToRender);
}


void RenderManager::destroyLightInformationUBO()
{
	glDeleteBuffers(1, &lightInformationUBO);
}


// renderCube() renders a 1x1 3D cube in NDC.
// -------------------------------------------------
unsigned int cubeVAO = 0;
unsigned int cubeVBO = 0;
void renderCube()
{
	// initialize (if necessary)
	if (cubeVAO == 0)
	{
		float vertices[] = {
			// back face
			-1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
			 1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
			 1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f, // bottom-right         
			 1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
			-1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
			-1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f, // top-left
			// front face
			-1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
			 1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f, // bottom-right
			 1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
			 1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
			-1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f, // top-left
			-1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
			// left face
			-1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
			-1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-left
			-1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
			-1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
			-1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-right
			-1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
			// right face
			 1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
			 1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
			 1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-right         
			 1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
			 1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
			 1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-left     
			// bottom face
			-1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
			 1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f, // top-left
			 1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
			 1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
			-1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f, // bottom-right
			-1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
			// top face
			-1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
			 1.0f,  1.0f , 1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
			 1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f, // top-right     
			 1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
			-1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
			-1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f  // bottom-left        
		};
		glGenVertexArrays(1, &cubeVAO);
		glGenBuffers(1, &cubeVBO);
		// fill buffer
		glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
		// link vertex attributes
		glBindVertexArray(cubeVAO);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}
	// render Cube
	glBindVertexArray(cubeVAO);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
}

// renderQuad() renders a 1x1 XY quad in NDC
// -----------------------------------------
unsigned int quadVAO = 0;
unsigned int quadVBO;
void renderQuad()
{
	if (quadVAO == 0)
	{
		float quadVertices[] = {
			// positions        // texture Coords
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		// setup plane VAO
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	}
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}
