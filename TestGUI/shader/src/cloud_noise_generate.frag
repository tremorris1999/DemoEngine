#version 430

in vec2 texCoord;
out vec4 fragmentColor;

uniform float currentRenderDepth;
uniform bool includePerlin;
uniform int gridSize;

layout (std140, binding = 4) uniform WorleyPoints
{
    vec4 worleyPoints[1024];
};

//// PERLIN NOISE (https://www.shadertoy.com/view/XsX3zB) ////

/* discontinuous pseudorandom uniformly distributed in [-0.5, +0.5]^3 */
vec3 random3(vec3 c) {
	float j = 4096.0*sin(dot(c,vec3(17.0, 59.4, 15.0)));
	vec3 r;
	r.z = fract(512.0*j);
	j *= .125;
	r.x = fract(512.0*j);
	j *= .125;
	r.y = fract(512.0*j);
	return r-0.5;
}

/* skew constants for 3d simplex functions */
const float F3 =  0.3333333;
const float G3 =  0.1666667;

/* 3d simplex noise */
float simplex3d(vec3 p) {
	 /* 1. find current tetrahedron T and it's four vertices */
	 /* s, s+i1, s+i2, s+1.0 - absolute skewed (integer) coordinates of T vertices */
	 /* x, x1, x2, x3 - unskewed coordinates of p relative to each of T vertices*/
	 
	 /* calculate s and x */
	 vec3 s = floor(p + dot(p, vec3(F3)));
	 vec3 x = p - s + dot(s, vec3(G3));
	 
	 /* calculate i1 and i2 */
	 vec3 e = step(vec3(0.0), x - x.yzx);
	 vec3 i1 = e*(1.0 - e.zxy);
	 vec3 i2 = 1.0 - e.zxy*(1.0 - e);
	 	
	 /* x1, x2, x3 */
	 vec3 x1 = x - i1 + G3;
	 vec3 x2 = x - i2 + 2.0*G3;
	 vec3 x3 = x - 1.0 + 3.0*G3;
	 
	 /* 2. find four surflets and store them in d */
	 vec4 w, d;
	 
	 /* calculate surflet weights */
	 w.x = dot(x, x);
	 w.y = dot(x1, x1);
	 w.z = dot(x2, x2);
	 w.w = dot(x3, x3);
	 
	 /* w fades from 0.6 at the center of the surflet to 0.0 at the margin */
	 w = max(0.6 - w, 0.0);
	 
	 /* calculate surflet components */
	 d.x = dot(random3(s), x);
	 d.y = dot(random3(s + i1), x1);
	 d.z = dot(random3(s + i2), x2);
	 d.w = dot(random3(s + 1.0), x3);
	 
	 /* multiply d by w^4 */
	 w *= w;
	 w *= w;
	 d *= w;
	 
	 /* 3. return the sum of the four surflets */
	 return dot(d, vec4(52.0));
}

/* const matrices for 3d rotation */
const mat3 rot1 = mat3(-0.37, 0.36, 0.85,-0.14,-0.93, 0.34,0.92, 0.01,0.4);
const mat3 rot2 = mat3(-0.55,-0.39, 0.74, 0.33,-0.91,-0.24,0.77, 0.12,0.63);
const mat3 rot3 = mat3(-0.71, 0.52,-0.47,-0.08,-0.72,-0.68,-0.7,-0.45,0.56);

/* directional artifacts can be reduced by rotating each octave */
float simplex3d_fractal(vec3 m) {
    return   0.5333333*simplex3d(m*rot1)
			+0.2666667*simplex3d(2.0*m*rot2)
			+0.1333333*simplex3d(4.0*m*rot3)
			+0.0666667*simplex3d(8.0*m);
}

//////////////////////////////////////////////////////////////

void main()
{
	fragmentColor = vec4(0.0);
	vec3 myPos = vec3(texCoord, currentRenderDepth);
	float closestDistance = 1.0;
	
	for (int i = 0; i < gridSize; i++)
		for (int j = 0; j < gridSize; j++)
			for (int k = 0; k < gridSize; k++)
			{
				int worleyIndex = int(mod(i * gridSize * gridSize + j * gridSize + k, 1024));
				vec3 worleyPoint = vec3(i, j, k) + worleyPoints[worleyIndex].xyz;
				worleyPoint /= float(gridSize);		// NOTE: this normalizes it.

				for (int x = -1; x <= 1; x++)
					for (int y = -1; y <= 1; y++)
						for (int z = -1; z <= 1; z++)
						{
							closestDistance = min(closestDistance, distance(myPos, worleyPoint + vec3(x, y, z)));
						}
			}
	float normalizedDistance = 1.0 - clamp(closestDistance * gridSize * 1.25, 0.0, 1.0);

	if (includePerlin && false)		// @TODO: review and see if this noise algorithm to include perlin noise too should get improved. I'm not really seeing the effect of it in the end product so yeah.  -Timo
	{
		float simplexFractal = simplex3d_fractal(myPos.xyz * 8.0 + 8.0) + 0.5;
		simplexFractal = clamp(0.5 + 0.5 * simplexFractal, 0.0, 1.0);
	
		//float floorAmount = 0.1;
		//float floorAmount2 = 0.1;
		//fragmentColor = vec4(vec3((floorAmount + (1.0 - floorAmount) * simplexFractal) * (floorAmount2 + (1.0 - floorAmount2) * smoothstep(0.0, 1.0, normalizedDistance))), 1.0);

		fragmentColor = vec4(vec3((simplexFractal) * smoothstep(0.0, 1.0, normalizedDistance) + (simplexFractal * 0.2)), 1.0);
		return;
	}

	fragmentColor = vec4(vec3(normalizedDistance), 1.0);
}
