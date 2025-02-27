#pragma once

#include "ShaderExt.h"

class ShaderExtShadow : public ShaderExt
{
public:
	ShaderExtShadow(Shader* shader);
	void setupExtension();

	static const int MAX_SHADOWS = 8;

	static unsigned int spotLightShadows[MAX_SHADOWS];
	static unsigned int pointLightShadows[MAX_SHADOWS];
	static float pointLightShadowFarPlanes[MAX_SHADOWS];
};
