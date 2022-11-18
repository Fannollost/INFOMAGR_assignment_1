#include "precomp.h"

// -----------------------------------------------------------
// Initialize the renderer
// -----------------------------------------------------------
void Renderer::Init()
{
	// create fp32 rgb pixel buffer to render to
	accumulator = (float4*)MALLOC64( SCRWIDTH * SCRHEIGHT * 16 );
	memset( accumulator, 0, SCRWIDTH * SCRHEIGHT * 16 );
}

// -----------------------------------------------------------
// Evaluate light transport
// -----------------------------------------------------------
float3 Renderer::Trace( Ray& ray, int depth )
{
	if (depth <= 0) {
		return float3(0, 0, 0);
	}

	float t_min = 0.001;
	scene.FindNearest( ray, t_min );
	if (ray.objIdx == -1) return 0; // or a fancy sky color	

	float3 I = ray.O + ray.t * ray.D;
	float3 N = scene.GetNormal(ray.objIdx, I, ray.D);
	float3 albedo = scene.GetAlbedo(ray.objIdx, I);

	float3 target = normalize(ray.IntersectionPoint() + N + RandomInHemisphere(N));
	Ray r = Ray(ray.IntersectionPoint(), normalize(target - ray.IntersectionPoint()));

	//return 0.5 * Trace(r, depth - 1);

	//GENERATES SKYBOX
	/*float3 unit_dir = UnitVector(ray.D);
	auto t = 0.5 * (unit_dir.y + 1.0);
	return (1.0 - t) * float3(1.0, 1.0, 1.0) + t * float3(0.5, 0.7, 1.0);  */

	scene.FindNearest(r, t_min);
	if (r.t < length(normalize(r.D))) {
		return float3(0, 0, 0);
	}						 


	/* visualize normal */ return (N + 1) * 0.5f;

	/* visualize distance */ // return 0.1f * float3( ray.t, ray.t, ray.t );
	/* visualize albedo */ // return albedo;
}

// -----------------------------------------------------------
// Main application tick function - Executed once per frame
// -----------------------------------------------------------
void Renderer::Tick( float deltaTime )
{
	// animation
	if (!camera.paused) {
		static float animTime = 0;
		scene.SetTime(animTime += deltaTime * 0.002f);
	}
	// pixel loop
	Timer t;
	// lines are executed as OpenMP parallel tasks (disabled in DEBUG)
	#pragma omp parallel for schedule(dynamic)
	for (int y = 0; y < SCRHEIGHT; y++)
	{
		// trace a primary ray for each pixel on the line
		for (int x = 0; x < SCRWIDTH; x++)
			accumulator[x + y * SCRWIDTH] =
				float4( Trace( camera.GetPrimaryRay( x, y ) , 1), 0 );
		// translate accumulator contents to rgb32 pixels
		for (int dest = y * SCRWIDTH, x = 0; x < SCRWIDTH; x++)
			screen->pixels[dest + x] = 
				RGBF32_to_RGB8( &accumulator[x + y * SCRWIDTH] );
	}
	// performance report - running average - ms, MRays/s
	static float avg = 10, alpha = 1;
	avg = (1 - alpha) * avg + alpha * t.elapsed() * 1000;
	if (alpha > 0.05f) alpha *= 0.5f;
	float fps = 1000 / avg, rps = (SCRWIDTH * SCRHEIGHT) * fps;
	printf( "%5.2fms (%.1fps) - %.1fMrays/s %.1fCameraSpeed\n", avg, fps, rps / 1000000, camera.speed );
}
