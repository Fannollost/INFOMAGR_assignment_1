#pragma once

// default screen resolution
#define SCRWIDTH	1280
#define SCRHEIGHT	720
// #define FULLSCREEN
// #define DOUBLESIZE

namespace Tmpl8 {

class Camera
{
public:
	Camera()
	{
		// setup a basic view frustum
		camPos = float3( 0, 0, -2 );
		topLeft = float3( -aspect, 1, 0 );
		topRight = float3( aspect, 1, 0 );
		bottomLeft = float3( -aspect, -1, 0 );
		speed = 0.1f;
	}
	Ray GetPrimaryRay( const int x, const int y )
	{
		// calculate pixel position on virtual screen plane
		const float u = (float)x * (1.0f / SCRWIDTH);
		const float v = (float)y * (1.0f / SCRHEIGHT);
		const float3 P = topLeft + u * (topRight - topLeft) + v * (bottomLeft - topLeft);
		return Ray( camPos, normalize( P - camPos ), float3(0) );
	}
	float aspect = (float)SCRWIDTH / (float)SCRHEIGHT;
	float3 camPos;
	float3 topLeft, topRight, bottomLeft;
	float speed;
	float yAngle = 0;
	float2 mov = float2(0.f);
	float fovChange = 0.f;
	bool paused = false;

	void MoveTick() {
		float3 velocity = float3(0, speed * mov[1], 0) + (speed * mov[0] * normalize(topRight - topLeft));
		if (length(velocity) > 0) {
			camPos += velocity;
			topLeft += velocity;
			topRight += velocity;
			bottomLeft += velocity;
		}
	}

	void FOVTick() {
		const float3 screenCenter = topLeft + .5f * (topRight - topLeft) + .5f * (bottomLeft - topLeft);
		if (fovChange != 0 && (length(screenCenter - camPos) > 0.1f || fovChange > 0)) {
			topLeft += normalize(screenCenter - camPos) * 0.1f * fovChange;
			topRight += normalize(screenCenter - camPos) * 0.1f * fovChange;
			bottomLeft += normalize(screenCenter - camPos) * 0.1f * fovChange;
		}
	}

	void MoveCameraY(int dir) {
		mov[1] += dir;
	}

	void MoveCameraX(int dir) {
		mov[0] += dir;
	}

	void RotateScreenX(float theta) {
		topLeft = RotateX(topLeft, camPos, theta);
		topRight = RotateX(topRight, camPos, theta);
		bottomLeft = RotateX(bottomLeft, camPos, theta);
	}

	void RotateScreenY(float theta) {
		yAngle += theta;
		topLeft = RotateY(topLeft, camPos,theta);
		topRight = RotateY(topRight, camPos, theta);
		bottomLeft = RotateY(bottomLeft, camPos, theta);
	}

	float3 RotateY(float3 p, float3 center, float theta) {
		double c = cos(theta);
		double s = sin(theta);
		float3 res = float3(0.f);

		float3 vect = p - center;
		float3 xTransform = float3(c, 0, -s);
		float3 zTransform = float3(s, 0, c);

		res[0] = dot(vect, xTransform);
		res[1] = vect[1];
		res[2] = dot(vect, zTransform);

		return res + center;
	}

	float3 RotateX(float3 p, float3 center, float theta) {
		double c = cos(theta);
		double s = sin(theta);
		float3 res = float3(0.f);

		float3 vect = p - center;
		vect = RotateY(vect, float3(0.f), -yAngle);
		float3 zTransform = float3(0, -s, c);
		float3 yTransform = float3(0, c, s);

		res[0] = vect[0];
		res[1] = dot(vect, yTransform);
		res[2] = dot(vect, zTransform);

		res = RotateY(res, float3(0.f), yAngle);

		return res + center;
	}

	void FOV(float x) {
		fovChange += x;
	}



	void TogglePause() {
		paused = !paused;
	}
};

}