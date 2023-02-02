#include "precomp.h"
// -----------------------------------------------------------
// Initialize the renderer
// -----------------------------------------------------------
void Renderer::Init()
{
	// create fp32 rgb pixel buffer to render to
	accumulator = (float4*)MALLOC64( SCRWIDTH * SCRHEIGHT * 16 );
	memset( accumulator, 0, SCRWIDTH * SCRHEIGHT * 16 );
	ifstream f( (scene.sceneName + "-" + to_string(qTable->GetLearningPhaseTime() / 10) + ".qtable").c_str() );
	if (f.good()) {
		qTable->parseQTable(scene.sceneName + "-" + to_string(qTable->GetLearningPhaseTime() / 10) + ".qtable", scene);
		qTable ->trainingPhase = false;
		cout << "Parsing qTable\n";
	}
	else {
		qTable->GeneratePoints(scene);
		cout << "Generating qTable\n";
	}
}

enum MAT_TYPE {
	DIFFUSE = 1,
	METAL = 2,
	GLASS = 3,
};
// -----------------------------------------------------------
// Evaluate light transport
// -----------------------------------------------------------
float3 Renderer::Trace(Ray& ray, int depth, float3 energy)
{
	if (depth <= 0) return float3(0, 0, 0);
	float t_min = 1e-6;
	scene.FindNearest(ray, t_min);
	if (ray.objIdx == -1) return scene.GetSkyColor(ray);
	if (ray.objIdx >= 11 && ray.objIdx < 11 + size(scene.lights))
		return scene.lights[ray.objIdx - 11]->GetLightIntensityAt(ray.IntersectionPoint(), ray.hitNormal, ray.IntersectionPoint());
	float3 totCol = float3(0);
	material* m = ray.GetMaterial();
	float3 f = m->col;

	if (!scene.raytracer) {
		double p = f.x > f.y && f.x > f.z ? f.x : f.y > f.z ? f.y : f.z;
		if (depth < 5 || !p) {
			if (RandomFloat() < p) {
				f = f * (1 / p);
			}
			else {
				return totCol;
			}
		}
	}
	switch (m->type) {
	case GLASS:	{
		glass* g = (glass*)m;
		float3 refractionColor = 0;
		// compute fresnel
		float kr;
		g->fresnel(normalize(ray.D), normalize(ray.hitNormal), g->ir, kr);
		bool outside = dot(ray.D, ray.hitNormal) < 0;
		float3 bias = 0.0001f * ray.hitNormal;
		float3 norm = outside ? ray.hitNormal : -ray.hitNormal;
		float r = !outside ? g->ir : (1/g->ir);
		if (outside)
		{
			energy.x *= exp(g->absorption.x * -ray.t);
			energy.y *= exp(g->absorption.y * -ray.t);
			energy.z *= exp(g->absorption.z * -ray.t);
		}
		else {
			energy = energy;
		} 

		if (kr < 1) {
			float3 refractionDirection = normalize(g->RefractRay(ray.D, norm,r));
			float3 refractionRayOrig = outside ? ray.IntersectionPoint() - bias : ray.IntersectionPoint() + bias;
			Ray refrRay = Ray(refractionRayOrig, refractionDirection, ray.color);
			float3 tempCol = g->col * energy;
			refractionColor = tempCol * Trace(refrRay, depth - 1, energy);
		}

		float3 reflectionDirection = normalize(reflect(ray.D, norm));
		float3 reflectionRayOrig = outside ? ray.IntersectionPoint() + bias : ray.IntersectionPoint() - bias;
		Ray reflRay = Ray(reflectionRayOrig, reflectionDirection, ray.color);
		float3 reflectionColor = g->col * Trace(reflRay, depth - 1, energy);
		// mix the two
		totCol += reflectionColor * kr + refractionColor * (1 - kr);
		break;
	}
	case METAL:	{
		Ray reflected;
		((metal*)m)->scatter(ray, reflected, ray.hitNormal, energy);
		totCol += m->col * Trace(reflected, depth - 1, energy) * energy;
		break;
	}
	case DIFFUSE:
		Ray scattered;
		for (int i = 0; i < size(scene.lights); i++)
		{
			float3 attenuation;
			float3 pickedPos = scene.lights[i]->GetLightPosition();
			float3 lightRayDirection = pickedPos  - ray.IntersectionPoint();
			float len2 = dot(lightRayDirection, lightRayDirection);
			lightRayDirection = normalize(lightRayDirection);
			Ray r = Ray(ray.IntersectionPoint() + lightRayDirection * 1e-4f ,lightRayDirection, ray.color, sqrt(len2));
			((diffuse*)m)->scatter(ray, attenuation, scattered, lightRayDirection,
				scene.lights[i]->GetLightIntensityAt(ray.IntersectionPoint(), ray.hitNormal, pickedPos), ray.hitNormal, energy);
			if (scene.IsOccluded(r)) continue;

			if (((diffuse*)m)->shinieness != 0)
				totCol += ((diffuse*)m)->shinieness * m->col * Trace(Ray(ray.IntersectionPoint(), reflect(ray.D, ray.hitNormal), ray.color), depth - 1, energy) * energy;

			totCol += (1- ((diffuse*)m)->shinieness) * m->col * attenuation * energy;
		}

		if(!scene.raytracer){
			float3 indirectLightning = 0;

			int N = 1;
			//float BRDF = 1 * INV2PI;
			for (int i = 0; i < N; i++) {
				float3 cos_i = dot(scattered.D, N);			 
				indirectLightning += cos_i * Trace(scattered,
					depth - 1, energy) * 2*PI ;
			}

			indirectLightning /= (float)N;
			totCol *= INVPI;
			totCol += indirectLightning;
		}
		break;
	}
	
	return totCol;
}

HemisphereSampling::Sample Renderer::SampleDirection(const Ray& r) {
	HemisphereSampling::Sample sample;

	float a = qTable->kdTree->getNearestDist(qTable->kdTree->rootNode, r.IntersectionPoint(), 0);
	int idx = qTable->kdTree->nearestNode->idx;

	qTable->SampleDirection(idx, sample);

	float3 temp = sample.dir;
	float3 v1 = r.hitNormal;
	float3 v2 = float3(0, 1, 0);

	if (dot(v1, v2) == -1) sample.dir = float3(sample.dir.x, -sample.dir.y, -sample.dir.z);
	else if (dot(v1, v2) == 0) {
		if (v1.z == v2.z) {
			if (v1.x == 1) sample.dir = float3(sample.dir.y, -sample.dir.x, sample.dir.z);
			else if (v1.x == -1) sample.dir = float3(-sample.dir.y, sample.dir.x, sample.dir.z);
		}
		else if (v1.x == v2.x) {
			if (v1.z == 1) sample.dir = float3(sample.dir.x, -sample.dir.z, sample.dir.y);
			else if (v1.z == -1) sample.dir = float3(sample.dir.x, sample.dir.z, -sample.dir.y);
		}
	}
	else{
		float angleX = computeAngle(float3(0, v1.y, v1.z), float3(0, v2.y, v2.z));
		if (v1.y * v2.z - v1.z * v2.y < 0) angleX = -angleX;
		float angleY = computeAngle(float3(v1.x, 0,v1.z), float3(v2.x, 0, v2.z));
		if (v1.x * v2.z - v1.z * v2.x < 0) angleY = -angleY;

		sample.dir = TransformVector(sample.dir, mat4::RotateX(angleX) * mat4::RotateY(angleY));
	}
	qTable->nextDir = sample.dir;

	return sample;
}

float3 Renderer::Sample(Ray& ray, int depth, float3 energy, const int sampleIdx = -1) {
	if (depth < 0) {
		scene.AddRayBounces(maxRayDepth);
		return float3(0.00f);
	}//float3(0.05f);
	float3 totCol = 0;
	float t_min = 0.001f;
	float eps = 0.0001f;
	scene.FindNearest(ray, t_min);

	if (ray.objIdx == -1) { 
		scene.AddRayBounces(maxRayDepth - depth);
		return scene.GetSkyColor(ray); 
	}
	if (ray.objIdx >= 11 && ray.objIdx < 11 + size(scene.lights)) {
		scene.AddRayBounces(maxRayDepth - depth);
		scene.AddConnectedRay();
		return scene.lights[ray.objIdx - 11]->GetLightIntensityAt(ray.IntersectionPoint(), ray.hitNormal, ray.IntersectionPoint());
	}
	//return float3(0);
	float3 intersectionPoint = ray.IntersectionPoint();
	float3 normal = ray.hitNormal;
	material* m = ray.GetMaterial();
	float3 f = m->col;
	/*if (scene.raytracer) {
		double p = f.x > f.y && f.x > f.z ? f.x : f.y > f.z ? f.y : f.z;
		if (depth < 5 || !p) {
			if (RandomFloat() < p) {
				f = f * (1 / p);
			}
			else {
				return totCol;
			}
		}
	}*/
	float3 attenuation;
	switch (m->type)
	{
		case DIFFUSE: {
			if (learningEnabled) {
				float3 directLightning = 0;
				for (int i = 0; i < size(scene.lights); i++) {

					float3 pickedPos = scene.lights[i]->GetLightPosition();
					float3 lightRayDirection = pickedPos - ray.IntersectionPoint();
					float len2 = dot(lightRayDirection, lightRayDirection);
					lightRayDirection = normalize(lightRayDirection);
					Ray r = Ray(ray.IntersectionPoint() + ray.hitNormal * 1e-4f, lightRayDirection, ray.color, sqrt(len2));
					bool isOccluded = scene.IsOccluded(r);
					if (isOccluded) continue;
					Ray scattered;
					((diffuse*)m)->scatter(ray, attenuation, scattered, lightRayDirection,
						scene.lights[i]->GetLightIntensityAt(ray.IntersectionPoint(), normal, pickedPos), normal, energy);

					directLightning += (1 - ((diffuse*)m)->shinieness) * m->col * attenuation;
					//if (ray.O.x > 2.5 && ray.O.z > 1) cout << isOccluded<< endl;
				}
				float3 indirectLightning = 0;
				int N = 1;
				float BRDF = 1 * INV2PI;
				float prob;
				for (int i = 0; i < N; ++i) {

					float3 rayToHemi;
					int samIdx;
					if (learningEnabled) {
						auto sam = SampleDirection(ray);
						rayToHemi = sam.dir;
						samIdx = sam.idx;
						prob = sam.prob;
						//cout << prob << endl;
					}
					else {
						rayToHemi = RandomInHemisphere(normal);
						samIdx = -1;
						prob = 1;
					}

					//if (!qTable->trainingPhase) cout << prob << endl;
					float3 cos_i = dot(rayToHemi, normal);
					indirectLightning += m->col * Sample(Ray(intersectionPoint + rayToHemi * eps, rayToHemi, float3(1.0f)),
						depth - 1, energy, samIdx);

				}

				indirectLightning /= (float)N;
				totCol = (directLightning * INV2PI * INVPI + fminf(1, indirectLightning) * m->albedo);

				if (learningEnabled && qTable->trainingPhase && sampleIdx >= 0) {
					qTable->Update(ray.O, ray.IntersectionPoint(), sampleIdx,
						fmaxf(dot(ray.D, normalize(scene.lights[0]->GetLightPosition() - ray.O)), 0) * directLightning,
						ray, m->albedo * INVPI, scene);
				}
				break;
			}
			else {
				float3 directLightning = 0;
				for (int i = 0; i < size(scene.lights); i++) {

					float3 pickedPos = scene.lights[i]->GetLightPosition();
					float3 lightRayDirection = pickedPos - ray.IntersectionPoint();
					float len2 = dot(lightRayDirection, lightRayDirection);
					lightRayDirection = normalize(lightRayDirection);
					Ray r = Ray(ray.IntersectionPoint() + lightRayDirection * 1e-4f, lightRayDirection, ray.color, sqrt(len2));
					if (scene.IsOccluded(r)) continue;
					Ray scattered;
					float3 attenuation;
					((diffuse*)m)->scatter(ray, attenuation, scattered, lightRayDirection,
						scene.lights[i]->GetLightIntensityAt(ray.IntersectionPoint(), normal, pickedPos), normal, energy);


					if (((diffuse*)m)->shinieness != 0)
						directLightning += ((diffuse*)m)->shinieness * m->col * Sample(Ray(ray.IntersectionPoint(), reflect(ray.D, ray.hitNormal), ray.color), depth - 1, energy);

					directLightning += (1 - ((diffuse*)m)->shinieness) * m->col * attenuation * energy;
				}
				float3 indirectLightning = 0;

				int N = 1;
				float BRDF = 1 * INV2PI;
				for (int i = 0; i < N; ++i) {
					float3 rayToHemi = RandomInHemisphere(normal);
					float3 cos_i = dot(rayToHemi, normal);
					indirectLightning += m->col * cos_i * Sample(Ray(intersectionPoint, rayToHemi, float3(0)),
						depth - 1, energy);
				}

				indirectLightning /= (float)N;
				totCol = (directLightning * INVPI + 2 * indirectLightning) * m->albedo;
				break;
			}
		}
		case METAL:{
			Ray reflected;
			((metal*)m)->scatter(ray, reflected, normal, energy);
			totCol += m->col * Sample(reflected, depth - 1, energy);
			break;
		}
		case GLASS: {
			glass* g = (glass*)m;
			float3 refractionColor = 0, reflectionColor = 0;
			// compute fresnel
			float kr;
			g->fresnel(normalize(ray.D), normalize(ray.hitNormal), g->ir, kr);
			bool outside = dot(ray.D, ray.hitNormal) < 0;
			float3 bias = 0.0001f * ray.hitNormal;
			float3 norm = outside ? ray.hitNormal : -ray.hitNormal;
			float r = !outside ? g->ir : (1 / g->ir);
			if (outside)
			{
				energy.x *= exp(g->absorption.x * -ray.t);
				energy.y *= exp(g->absorption.y * -ray.t);
				energy.z *= exp(g->absorption.z * -ray.t);
			}
			else {
				energy = energy;
			}
			float odds = kr;
			if (odds < RandomFloat()) {
				float3 refractionDirection = normalize(g->RefractRay(ray.D, norm, r));
				float3 refractionRayOrig = outside ? ray.IntersectionPoint() - bias : ray.IntersectionPoint() + bias;
				Ray refrRay = Ray(refractionRayOrig, refractionDirection, ray.color);
				float3 tempCol = g->col * energy;
				refractionColor = tempCol * Sample(refrRay, depth - 1, energy);
				totCol += refractionColor * (1 - kr);
			} else{
				float3 reflectionDirection = normalize(reflect(ray.D, norm));
				float3 reflectionRayOrig = outside ? ray.IntersectionPoint() + bias : ray.IntersectionPoint() - bias;
				Ray reflRay = Ray(reflectionRayOrig, reflectionDirection, ray.color);
				float3 reflectionColor = g->col * Sample(reflRay, depth - 1, energy);
				totCol += reflectionColor * kr;
			}
			break;
		}
	}
	return totCol;
}

float3 Renderer::Debug(Ray& ray, float3 totCol)
{
	float t_min = 1e-6; ray.debug = true;
	scene.FindNearest(ray, t_min);
	material* m = ray.GetMaterial();

	if (m != nullptr && m->type == DEBUG) {
		return m->col * scene.aaSamples;
	}
	return totCol;
}
// -----------------------------------------------------------
// Main application tick function - Executed once per frame
// -----------------------------------------------------------
void Renderer::Tick(float deltaTime)
{
	scene.totIterationNumber++;
	// animation
	if (!camera.paused && scene.raytracer) {
		static float animTime = 0;
		scene.SetTime(animTime += deltaTime * 0.002f);
	}
	int it = scene.GetIterationNumber();

	camera.MoveTick();
	camera.FOVTick();

	if (camera.GetChange() && !scene.raytracer) {
		scene.SetIterationNumber(1);
	}
	// pixel loop
	Timer t;
	// lines are executed as OpenMP parallel tasks (disabled in DEBUG)
	float energy;
	//#pragma omp parallel for schedule(dynamic)
	for (int y = 0; y < SCRHEIGHT; ++y)
	{
		// trace a primary ray for each pixel on the line
		for (int x = 0; x < SCRWIDTH; ++x) {
			float3 totCol = float3(0);				//antialiassing
			for (int s = 0; s < scene.aaSamples; ++s) {
				if (scene.raytracer) {
					float newX = x; 
					float newY = y;
					totCol += Trace(camera.GetPrimaryRay(newX, newY), 4, float3(1));
					totCol = Debug(camera.GetPrimaryRay(newX, newY), totCol);
					accumulator[x + y * SCRWIDTH] = (totCol / scene.aaSamples);
				}
				else {
					if (camera.GetChange())	{
						accumulator[x + y * SCRWIDTH] = float3(0);
					}
					float newX = x + (RandomFloat() * 2 - 1);
					float newY = y + (RandomFloat() * 2 - 1);
					totCol += Sample(camera.GetPrimaryRay(newX, newY),maxRayDepth, float3(1), -1);
					if(!qTable->trainingPhase || !learningEnabled) {
						energy += (totCol.x + totCol.y + totCol.z);
						accumulator[x + y * SCRWIDTH] += totCol;
					}
					//cout << y << ", " << t.elapsed() << endl;
				}
			}
			
		}

		// translate accumulator contents to rgb32 pixels
		for (int dest = y * SCRWIDTH, x = 0; x < SCRWIDTH; ++x)	{
			float4 acc = accumulator[x + y * SCRWIDTH] / it; /// iteration;
			screen->pixels[dest + x] = (RGBF32_to_RGB8(&acc));///iteration ;
		}
	}

	cout << "average bounces: " << ((float)scene.GetTotalRayBounces() / (SCRHEIGHT * SCRWIDTH)) << endl;
	scene.totRayBounces = 0; 

	cout << "Total Connected Rays: " << scene.GetTotalConnectedRays() << endl;
	scene.WriteTotalConnectedRays();
	scene.totNonTerminatedRays = 0;

	if (!scene.raytracer && !camera.GetChange() && !qTable->trainingPhase || !learningEnabled)
		scene.SetIterationNumber(it + 1);

	camera.SetChange(false);
	// performance report - running average - ms, MRays/s
	static float avg = 10, alpha = 1;
	avg = (1 - alpha) * avg + alpha * t.elapsed() * 1000;
	if (alpha > 0.05f) alpha *= 0.5f;
	float fps = 1000 / avg, rps = (SCRWIDTH * SCRHEIGHT) * fps;
	scene.SetFPS(fps);
	scene.runTime += t.elapsed();

	if(scene.totIterationNumber > qTable->GetExportedFrames() & !scene.framesExported)
		scene.ExportQLearningData();


	if (scene.runTime > 20 && !scene.exported) {
		scene.ExportData();
	}		

	if (scene.runTime > qTable->GetLearningPhaseTime() & qTable->trainingPhase) {
		qTable->trainingPhase = false;
		qTable->exportQTable(scene.sceneName + "-" + to_string(qTable->GetLearningPhaseTime() / 10) + ".qtable");
		cout << "Learning phase ended\n";
	}

	printf( "%5.2fms (%.1ffps) - %.1fMrays/s %.1fCameraSpeed\n", avg, fps, rps / 1000000, camera.speed );
	cout << "Energy level: " << energy << endl;
	energy = 0;
}

