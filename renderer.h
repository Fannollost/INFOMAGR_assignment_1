#pragma once
#include <iostream>
namespace Tmpl8
{

class Renderer : public TheApp
{
public:
	// game flow methods
	void Init();
	float3 Trace( Ray& ray, int depth );
	void Tick( float deltaTime );
	void Shutdown() { /* implement if you want to do something on exit */ }
	// input handling
	void MouseUp( int button ) { 
		mousePressed = false;
	}
	void MouseDown(int button) { mousePressed = true; }
	void MouseMove(int x, int y) {
		if (mousePressed) {
			camera.RotateScreenY((float(x) - float(mousePos.x))/SCRWIDTH);
			camera.RotateScreenX((float(y) - float(mousePos.y)) / SCRWIDTH);
		}
		mousePos.x = x, mousePos.y = y;
		
	}
	void MouseWheel(float y) { 
		y > 0 ? camera.speed += 0.1f : camera.speed -= 0.1f;
	}
	void KeyUp( int key ) {
		
		/* implement if you want to handle keys */ }
	void KeyDown( int key ) {
		switch (key) {
			case KEYBOARD_W:
				camera.MoveCameraY(1);
				break;
			case KEYBOARD_S:
				camera.MoveCameraY(-1);
				break;
			case KEYBOARD_D:
				camera.MoveCameraX(1);
				break;
			case KEYBOARD_A:
				camera.MoveCameraX(-1);
				break;
			case KEYBOARD_SPACE:
				camera.TogglePause();
				break;
			case KEYBOARD_PLUS:
				camera.FOV(1.f);
				break;
			case KEYBOARD_MINUS:
				camera.FOV(-1.f);
				break;
		}
		/* implement if you want to handle keys */ }
	// data members
	int2 mousePos;
	bool mousePressed = false;
	float4* accumulator;
	Scene scene;
	Camera camera;
	float2 xBox = float2(-1,1), yBox = float2(-1, 1), zBox = float2(-1, 1);	//makeboudningbox
	enum UserInput {
		KEYBOARD_W = 87,
		KEYBOARD_D = 68,
		KEYBOARD_S = 83,
		KEYBOARD_A = 65,
		KEYBOARD_SPACE = 32,
		KEYBOARD_PLUS = 334,
		KEYBOARD_MINUS = 333
	};
};

} // namespace Tmpl8