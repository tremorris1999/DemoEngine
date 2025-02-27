#include "ShaderExtCloud_effect.h"

#include "../Shader.h"
#include "../../../mainloop/MainLoop.h"
#include "../../render_manager/RenderManager.h"


unsigned int ShaderExtCloud_effect::cloudEffect;
unsigned int ShaderExtCloud_effect::cloudDepthTexture;
unsigned int ShaderExtCloud_effect::atmosphericScattering;
glm::vec3 ShaderExtCloud_effect::mainCameraPosition;
float ShaderExtCloud_effect::cloudEffectDensity = 0.025f;		// Seemed like a good value to me.  -Timo


ShaderExtCloud_effect::ShaderExtCloud_effect(Shader* shader) : ShaderExt(shader)
{
}

void ShaderExtCloud_effect::setupExtension()
{
	shader->setSampler("cloudEffect", cloudEffect);
	shader->setSampler("cloudDepthTexture", cloudDepthTexture);
	shader->setSampler("atmosphericScattering", atmosphericScattering);
	shader->setVec3("mainCameraPosition", mainCameraPosition);
	shader->setFloat("cloudEffectDensity", cloudEffectDensity);
}
