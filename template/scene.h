#pragma once

// -----------------------------------------------------------
// scene.h
// Simple test scene for ray tracing experiments. Goals:
// - Super-fast scene intersection
// - Easy interface: scene.FindNearest / IsOccluded
// - With normals and albedo: GetNormal / GetAlbedo
// - Area light source (animated), for light transport
// - Primitives can be hit from inside - for dielectrics
// - Can be extended with other primitives and/or a BVH
// - Optionally animated - for temporal experiments
// - Not everything is axis aligned - for cache experiments
// - Can be evaluated at arbitrary time - for motion blur
// - Has some high-frequency details - for filtering
// Some speed tricks that severely affect maintainability
// are enclosed in #ifdef SPEEDTRIX / #endif. Mind these
// if you plan to alter the scene in any way.
// -----------------------------------------------------------

#define SPEEDTRIX

#define PLANE_X(o,i) {if((t=-(ray.O.x+o)*ray.rD.x)<ray.t)ray.t=t,ray.objIdx=i;}
#define PLANE_Y(o,i) {if((t=-(ray.O.y+o)*ray.rD.y)<ray.t)ray.t=t,ray.objIdx=i;}
#define PLANE_Z(o,i) {if((t=-(ray.O.z+o)*ray.rD.z)<ray.t)ray.t=t,ray.objIdx=i;}

namespace Tmpl8 {
	class material;
	class diffuse;
	class metal;
	class Triangle;
	class DataCollector;
	enum MAT_TYPE {
		DIFFUSE = 1,
		METAL = 2,
		GLASS = 3,
	};
	__declspec(align(64)) class Ray
	{
	public:
		Ray() = default;
		Ray(float3 origin, float3 direction, float3 color, float distance = 1e34f)
		{
			exists = true;
			O = origin, D = direction, t = distance;
			// calculate reciprocal ray direction for triangles and AABBs
			rD = float3(1 / D.x, 1 / D.y, 1 / D.z);
#ifdef SPEEDTRIX
			d0 = d1 = d2 = 0;
#endif
		}
		float3 IntersectionPoint() const { return O + t * D; }
		void SetMaterial(material* mat) { m = mat; }
		material* GetMaterial() { return m; }
		void SetNormal(float3 normal) {
			hitNormal = normal;
		}
		// ray data
#ifndef SPEEDTRIX
		float3 O, D, rD;
#else
		union { struct { float3 O; float d0; }; __m128 O4; };
		union { struct { float3 D; float d1; }; __m128 D4; };
		union { struct { float3 rD; float d2; }; __m128 rD4; };
#endif
		float t = 1e34f;
		int objIdx = -1;
		bool inside = false; // true when in medium
		bool exists = false;
		float3 color = 0;
		float3 hitNormal;
		material* m;
	};

	class Light {
	public:
		Light() = default;
		Light(int idx, float3 p, float str, float3 c, float3 n, bool rt)
			: objIdx(idx), pos(p), strength(str), col(c), normal(n), raytracer(rt) {}
		float3 GetNormal() { return normal; }
		virtual float3 GetLightPosition() { return pos; }
		float3 GetLightColor() { return col; }
		virtual float3 GetLightIntensityAt(float3 p, float3 n) { return 1; }
		virtual void Intersect (Ray& ray, float t_min) { return; }
		float3 pos;
		bool raytracer;
		float3 col;
		float strength;
		int objIdx;
		float3 normal;
	};

	class AreaLight : public Light {
	public:
		AreaLight() = default;
		AreaLight(int idx, float3 p, float str, float3 c, float r, float3 n, int s, bool rt)
			: Light(idx, p, str, c, n, rt) {
			radius = r;
			radius2 = r * r;
			samples = s;
			area = 2 * radius2 * PI;
		}

		void Intersect(Ray& ray, float t_min) override {
			float d = dot(normal, ray.D);
			float3 dir = pos - ray.O;
			float t = dot(dir, normal) / d;
				if (t >= t_min) {
					float3 intersection = ray.O +ray.D * t;
					float3 v = intersection - pos;
					float dis2 = dot(v, v);
					if (sqrtf(dis2) <= radius) {
						ray.t = t - 1e-6, ray.SetNormal(normal), ray.color = col;
							ray.objIdx = objIdx;
					}
			}


		}
		float3 GetLightIntensityAt(float3 p, float3 n) override {
			float dis = length(pos - p);
			float3 dir = pos - p;
			float cos_ang = dot(normalize(n), normalize(dir));

			float relStr = 1 / (dis * PI) * strength;
			float str = dot(n, normalize(dir));
			if (dis <= radius && isZero(cos_ang)) return float3(strength);
			return relStr * str * GetLightColor();
		}
		float3 GetLightPosition() override {
			if (raytracer) return pos;
			float newRad = radius * sqrt(RandomFloat());
			float theta = random(-1.0f, 1.0f) * 2 * PI;
			return float3(pos.x + newRad * cos(theta), pos.y + newRad * sin(theta), pos.z);
		}
		int samples;
		float radius, radius2;
		float area;
	};

	class DirectionalLight : public Light {
	public:
		DirectionalLight() = default;
		DirectionalLight(int idx, float3 p, float str, float3 c, float3 n, float r, bool rt) : Light(idx, p, str, c, n, rt) {
			sinAngle = sin(r * PI / 2);
		}
		void Intersect(Ray& ray, float t_min) override {
			return;
		}
		float3 GetLightPosition() override {
			return pos;
		}
		float3 GetLightIntensityAt(float3 p, float3 n) override {
			float3 dir = p - pos;
			float sTheta = length(cross(dir, normal)) / length(dir) * length(normal);
			if (dot(dir, normal) < 0) {
				return 0;
			}
			float dis = length(dir);
			float str = sinAngle - sTheta > 0 ? asin(sinAngle) - asin(sTheta) : 0;

			return 1 / dis * str * strength;
		}

		float sinAngle;
	};
	
	// -----------------------------------------------------------
	// Triangle Primitive
	// 
	// 
	// -----------------------------------------------------------
	class Triangle {
	public:
		Triangle() = default;
		Triangle(int idx, material* m, float3 ver0, float3 ver1, float3 ver2) : objIdx(idx), v0(ver0), v1(ver1), v2(ver2), mat(m) {
			e1 = v1 - v0;
			e2 = v2 - v0;
			N = normalize(cross(e1, e2));
			centroid = (v0 + v1 + v2) * 0.333f;
		}
		Triangle(int idx, material* m, int3 facesIdx, vector<float3> vertices) : objIdx(idx), v0(vertices[facesIdx.x]), v1(vertices[facesIdx.y]), v2(vertices[facesIdx.z]), mat(m) {
			e1 = v1 - v0;
			e2 = v2 - v0;
			N = normalize(cross(e1, e2));
			centroid = (v0 + v1 + v2) * 0.333f;
		}
		void Intersect(Ray& ray, float t_min) const {		 //scratchapixel implementation
			float NdotRayDir = dot(N, ray.D);
			if (fabs(NdotRayDir) < t_min) return;
			float d = -dot(N, v0);
			float t = -(dot(N, ray.O) + d) / NdotRayDir;
			if (t < 0) return;
			float3 p = ray.O + t * ray.D;
			float3 c ;

			float3 vp0 = p - v0;
			c = cross(e1, vp0);
			if (dot(N, c) < 0) return ;
			float3 vp1 = p - v1;
			float3 e3 = v2 - v1;
			c = cross(e3, vp1);
			if (dot(N, c) < 0) return;
			float3 e4 = v0 - v2;
			float3 vp2 = p - v2;
			c = cross(e4, vp2);
			if (dot(N, c) < 0) return;

			if (t < ray.t && t > t_min) {
				ray.t = t, ray.objIdx = objIdx, ray.m = mat,
					ray.SetNormal(N);
			}
		}
		bool IsOccluding(Ray& ray, float t_min) const {		 //scratchapixel implementation
			float NdotRayDir = dot(N, ray.D);
			if (fabs(NdotRayDir) < t_min) return false;
			float d = -dot(N, v0);
			float t = -(dot(N, ray.O) + d) / NdotRayDir;
			if (t < 0) return false;
			float3 p = ray.O + t * ray.D;
			float3 c;

			float3 vp0 = p - v0;
			c = cross(e1, vp0);
			if (dot(N, c) < 0) return false;
			float3 vp1 = p - v1;
			float3 e3 = v2 - v1;
			c = cross(e3, vp1);
			if (dot(N, c) < 0) return false;
			float3 e4 = v0 - v2;
			float3 vp2 = p - v2;
			c = cross(e4, vp2);
			if (dot(N, c) < 0) return false;
			if (t < ray.t && t > t_min) return true;
		}
		void update(int3 faces, vector<float3> vertices) {
			v0 = vertices[faces.x];
			v1 = vertices[faces.y];
			v2 = vertices[faces.z];
			e1 = v1 - v0;
			e2 = v2 - v0;
			N = normalize(cross(e1, e2));
			centroid = (v0 + v1 + v2) * 0.333f;
		}
		float3 GetNormal(const float3 I) const { return N; }
		float3 v0, v1, v2, e1, e2, centroid, N;
		int objIdx = -1;
		material* mat;
	};

	// -----------------------------------------------------------
	// Mesh Primitive
	// 
	// 
	// -----------------------------------------------------------
	class Mesh {
	public:
		Mesh() = default;
		Mesh(int idGroup, const char* path, material* m) : groupIdx(idGroup), mat(m) {
			FILE* file = fopen(path, "r");
			float a, c, d, e, f, g, h, i, j;
			int res = 1;
			int count = 0;
			while (res > 0) {
				
				res = fscanf(file, "%f %f %f %f %f %f %f %f %f\n",
					&a, &c, &d, &e, &f, &g, &h, &i, &j);
				originalVerts.push_back(float3(a, c, d));
				vertices.push_back(float3(a, c, d));
				originalVerts.push_back(float3(e, f, g));
				vertices.push_back(float3(e, f, g));
				originalVerts.push_back(float3(h, i, j));
				vertices.push_back(float3(h, i, j));
				faces.push_back(int3(count*3,count*3+1,count*3+2));
				tri.push_back(Triangle(1000 * idGroup + i, m, float3(a, c, d), float3(e, f, g), float3(h, i, j)));
				count++;
			}
			fclose(file);
		}
		Mesh(int idGroup, string path, material* m, float3 pos, float scale) : groupIdx(idGroup), mat(m) {
			ifstream file(path, ios::in);
			if (!file)
			{
				std::cerr << "Cannot open " << path << std::endl;
				exit(1);
			}
			string line;
			float x, y, z;
			while (getline(file, line))
			{
				if (line.substr(0, 2) == "v ") {
					istringstream v(line.substr(2));
					v >> x; v >> y; v >> z;
					vertices.push_back(float3(x * scale + pos.x, y * scale + pos.y, z * scale + pos.z));
					originalVerts.push_back(float3(x * scale + pos.x, y * scale + pos.y, z * scale + pos.z));
				}
				else if (line.substr(0, 2) == "f ") {
					int v0, v1, v2;
					int temp;
					const char* constL = line.c_str();
					sscanf(constL, "f %i//%i %i//%i %i//%i", &v0, &temp, &v1, &temp, &v2, &temp);
					faces.push_back(int3(v0, v1, v2));
				}
			}
			for (uint i = 0; i < size(faces); i++) {
				tri.push_back(Triangle(1000 * idGroup + i, m, (faces[i] - 1), vertices));
			}
		}
		uint getSize() {
			return size(faces);
		}
		bool IsOccluding(Ray& ray, float t_min) const {
			for (int i = 0; i < size(faces); i++) {
				tri[i].IsOccluding(ray, t_min);
			}

		}
		void Intersect(Ray& ray, float t_min) const {
			float3 v0, v1, v2;
			for (int i = 0; i < size(faces); i++) {
				tri[i].Intersect(ray, t_min);
			}
		}
		void update() {
			for (int i = 0; i < size(faces); i++) {
				tri[i].update((faces[i] - 1), vertices);
			}
		}
		vector<float3> vertices;
		vector<int3> faces;
		vector<Triangle> tri;
		vector<float3> originalVerts;
		material* mat;
		int groupIdx = -1;
	};

	// -----------------------------------------------------------
	// Sphere primitive
	// Basic sphere, with explicit support for rays that start
	// inside it. Good candidate for a dielectric material.
	// -----------------------------------------------------------
	class Sphere {
	public:
		Sphere() = default;
		Sphere(int idx, material* m, float3 p, float r) : pos(p), r2(r* r), invr(1 / r), objIdx(idx), mat(m), r(r) { }
		void Intersect(Ray& ray, float t_min) const {
			float3 oc = ray.O - this->pos;
			float b = dot(oc, ray.D);
			float c = dot(oc, oc) - this->r2;
			float t, d = b * b - c;
			if (d <= 0) return;
			d = sqrtf(d), t = -b - d;
			if (t < ray.t && t > t_min)
			{
				ray.t = t, ray.objIdx = objIdx, ray.m = mat;
				ray.SetNormal(GetNormal(ray.IntersectionPoint()));
				return;
			}
			t = d - b;
			if (t < ray.t && t > t_min)
			{
				ray.t = t, ray.objIdx = objIdx, ray.m = mat;
				ray.SetNormal(GetNormal(ray.IntersectionPoint()));
				return;
			}
		}
		bool IsOccluding(Ray& ray, float t_min) const {
			float3 oc = ray.O - this->pos;
			float b = dot(oc, ray.D);
			float c = dot(oc, oc) - this->r2;
			float t, d = b * b - c;
			if (d <= 0) return false;
			d = sqrtf(d), t = -b - d;
			float t2 = d - b;
			return ((t < ray.t && t > t_min) || (t2 < ray.t&& t2 > t_min));
		}
		float3 GetNormal(const float3 I) const
		{
			return (I - this->pos) * invr;
		}
		float3 GetAlbedo(const float3 I) const
		{
			return float3(0.93f);
		}
		float3 pos = 0;
		float r2 = 0, invr = 0, r;
		int objIdx = -1;
		material* mat;
	};

	// -----------------------------------------------------------
	// Plane primitive
	// Basic infinite plane, defined by a normal and a distance
	// from the origin (in the direction of the normal).
	// -----------------------------------------------------------
	class Plane {
	public:
		Plane() = default;
		Plane(int idx, material* m, float3 normal, float dist) : N(normal), d(dist), objIdx(idx), mat(m) {}
		void Intersect(Ray& ray, float t_min) const
		{
			float t = -(dot(ray.O, this->N) + this->d) / (dot(ray.D, this->N));
			if (t < ray.t && t > t_min) ray.t = t, ray.objIdx = objIdx, ray.m = mat,
				ray.SetNormal(N);
		}
		bool IsOccluding(Ray& ray, float t_min) const
		{
			float t = -(dot(ray.O, this->N) + this->d) / (dot(ray.D, this->N));
			return (t < ray.t && t > t_min);
		}
		float3 GetNormal(const float3 I) const
		{
			return N;
		}
		float3 GetAlbedo(const float3 I) const
		{
			if (N.y == 1)
			{
				// floor albedo: checkerboard
				int ix = (int)(I.x * 2 + 96.01f);
				int iz = (int)(I.z * 2 + 96.01f);
				// add deliberate aliasing to two tile
				if (ix == 98 && iz == 98) ix = (int)(I.x * 32.01f), iz = (int)(I.z * 32.01f);
				if (ix == 94 && iz == 98) ix = (int)(I.x * 64.01f), iz = (int)(I.z * 64.01f);
				return float3(((ix + iz) & 1) ? 1 : 0.3f);
			}
			else if (N.z == -1)
			{
				// back wall: logo
				static Surface logo("assets/logo.png");
				int ix = (int)((I.x + 4) * (128.0f / 8));
				int iy = (int)((2 - I.y) * (64.0f / 3));
				uint p = logo.pixels[(ix & 127) + (iy & 63) * 128];
				uint3 i3((p >> 16) & 255, (p >> 8) & 255, p & 255);
				return float3(i3) * (1.0f / 255.0f);
			}
			return float3(0.93f);
		}
		float3 N;
		float d;
		int objIdx = -1;
		material* mat;
	};

	// -----------------------------------------------------------
	// Cube primitive
	// Oriented cube. Unsure if this will also work for rays that
	// start inside it; maybe not the best candidate for testing
	// dielectrics.
	// -----------------------------------------------------------
	class Cube {
	public:
		Cube() = default;
		Cube(int idx, material* m, float3 pos, float3 size, mat4 transform = mat4::Identity())
		{
			objIdx = idx;
			b[0] = pos - 0.5f * size, b[1] = pos + 0.5f * size;
			mat = m;
			M = transform, invM = transform.FastInvertedTransformNoScale();
		}
		void Intersect(Ray& ray, float t_min) const {
			// 'rotate' the cube by transforming the ray into object space
			// using the inverse of the cube transform.
			float3 O = TransformPosition(ray.O, invM);
			float3 D = TransformVector(ray.D, invM);
			float rDx = 1 / D.x, rDy = 1 / D.y, rDz = 1 / D.z;
			int signx = D.x < 0, signy = D.y < 0, signz = D.z < 0;
			float tmin = (b[signx].x - O.x) * rDx;
			float tmax = (b[1 - signx].x - O.x) * rDx;
			float tymin = (b[signy].y - O.y) * rDy;
			float tymax = (b[1 - signy].y - O.y) * rDy;
			if (tmin > tymax || tymin > tmax) return;
			tmin = max(tmin, tymin), tmax = min(tmax, tymax);
			float tzmin = (b[signz].z - O.z) * rDz;
			float tzmax = (b[1 - signz].z - O.z) * rDz;
			if (tmin > tzmax || tzmin > tmax) return;
			tmin = max(tmin, tzmin), tmax = min(tmax, tzmax);
			if (tmin > t_min)
			{
				if (tmin < ray.t) ray.t = tmin, ray.objIdx = objIdx, ray.m = mat,
					ray.SetNormal(GetNormal(ray.IntersectionPoint()));
			}
			else if (tmax > t_min)
			{
				if (tmax < ray.t) ray.t = tmax, ray.objIdx = objIdx, ray.m = mat,
					ray.SetNormal(GetNormal(ray.IntersectionPoint()));
			}
		}
		bool IsOccluding(Ray& ray, float t_min) const {
			// 'rotate' the cube by transforming the ray into object space
			// using the inverse of the cube transform.
			float3 O = TransformPosition(ray.O, invM);
			float3 D = TransformVector(ray.D, invM);
			float rDx = 1 / D.x, rDy = 1 / D.y, rDz = 1 / D.z;
			int signx = D.x < 0, signy = D.y < 0, signz = D.z < 0;
			float tmin = (b[signx].x - O.x) * rDx;
			float tmax = (b[1 - signx].x - O.x) * rDx;
			float tymin = (b[signy].y - O.y) * rDy;
			float tymax = (b[1 - signy].y - O.y) * rDy;
			if (tmin > tymax || tymin > tmax) return false;
			tmin = max(tmin, tymin), tmax = min(tmax, tymax);
			float tzmin = (b[signz].z - O.z) * rDz;
			float tzmax = (b[1 - signz].z - O.z) * rDz;
			if (tmin > tzmax || tzmin > tmax) return false;
			tmin = max(tmin, tzmin), tmax = min(tmax, tzmax);
			return (tmin > t_min || tmax > t_min);
		}
		float3 GetNormal(const float3 I) const
		{
			// transform intersection point to object space
			float3 objI = TransformPosition(I, invM);
			// determine normal in object space
			float3 N = float3(-1, 0, 0);
			float d0 = fabs(objI.x - b[0].x), d1 = fabs(objI.x - b[1].x);
			float d2 = fabs(objI.y - b[0].y), d3 = fabs(objI.y - b[1].y);
			float d4 = fabs(objI.z - b[0].z), d5 = fabs(objI.z - b[1].z);
			float minDist = d0;
			if (d1 < minDist) minDist = d1, N.x = 1;
			if (d2 < minDist) minDist = d2, N = float3(0, -1, 0);
			if (d3 < minDist) minDist = d3, N = float3(0, 1, 0);
			if (d4 < minDist) minDist = d4, N = float3(0, 0, -1);
			if (d5 < minDist) minDist = d5, N = float3(0, 0, 1);
			// return normal in world space
			return TransformVector(N, M);
		}
		float3 GetAlbedo(const float3 I) const
		{
			return float3(1, 1, 1);
		}
		float3 b[2];
		mat4 M, invM;
		int objIdx = -1;
		material* mat;
	};

	// -----------------------------------------------------------
	// Quad primitive
	// Oriented quad, intended to be used as a light source.
	// -----------------------------------------------------------
	class Quad {
	public:
		Quad() = default;
		Quad(int idx, material* m, float s, mat4 transform = mat4::Identity())
		{
			objIdx = idx;
			size = s * 0.5f;
			mat = m;
			T = transform, invT = transform.FastInvertedTransformNoScale();
		}
		void Intersect(Ray& ray, float t_min) const {
			const float3 O = TransformPosition(ray.O, invT);
			const float3 D = TransformVector(ray.D, invT);
			const float t = O.y / -D.y;
			if (t < ray.t && t > t_min)
			{
				float3 I = O + t * D;
				if (I.x > -size && I.x < size && I.z > -size && I.z < size)
					ray.t = t, ray.objIdx = objIdx, ray.m = mat,
					ray.SetNormal(GetNormal(ray.IntersectionPoint()));
			}
		}
		float3 GetNormal(const float3 I) const
		{
			// TransformVector( float3( 0, -1, 0 ), T ) 
			return float3(-T.cell[1], -T.cell[5], -T.cell[9]);
		}
		float3 GetAlbedo(const float3 I) const
		{
			return float3(10);
		}
		float size;
		mat4 T, invT;
		int objIdx = -1;
		material* mat;
	};

	class material {
	public:
		material(float3 c, bool rt) : col(c), raytracer(rt) {}

		void SetColor(float3 c) { col = c; }
		float3 col, albedo = 0, emission = 0;
		int type;
		bool raytracer;
	};

	
	class diffuse : public material {
	public:
		diffuse(float3 a = 0, float3 c = 0, float ks = 0.2, float kd = 0.8, int n = 2, bool rt = true, float e = 0, float s = 0)
			: specu(ks), diffu(kd), N(n), material(c, rt) {
			type = DIFFUSE;
			albedo = a;
			emission = e;
			shinieness = s;
		}
		void SetSpecularity(float ks) { specu = ks; }
		void SetDiffuse(float kd) { diffu = kd; }
		void SetN(int n) { N = n; }
		virtual bool scatter(const Ray& ray, float3& att, Ray& scattered,  float3 lightDir, float3 lightIntensity, float3 normal, float3& energy) {
			float3 reflectionDirection = reflect(-lightDir, normal);
			float3 specularColor, lightAttenuation;
			specularColor = powf(fmax(0.0f, -dot(reflectionDirection, ray.D)), N) * lightIntensity;
			lightAttenuation = lightIntensity;
			att = albedo * lightAttenuation * diffu + specularColor * specu;
			float3 dir;
			if (!raytracer) {
				dir = RandomInHemisphere(normal);
			}
			scattered = Ray(ray.IntersectionPoint(), dir, ray.color);
			float3 retention = float3(1) - albedo;
			float3 newEnergy(energy - retention);
			energy = newEnergy.x > 0 ? newEnergy : 0;
			return true;
		}

	public:
		float specu, diffu, shinieness;
		int N;
	};

	class metal : public material {
	public:
		metal(float f, float3 c, bool rt) : fuzzy(f < 1 ? f : 1), material(c, rt) { type = METAL; emission = 0; }
		virtual bool scatter(const Ray& ray, Ray& reflected, float3 normal, float3& energy) {
			float3 dir = reflect(ray.D, normal);
			reflected = Ray(ray.IntersectionPoint() + normal * 0.001f, dir, ray.color * col);
			energy = energy;
			return dot(reflected.D, normal) > 0;
		}

	public:
		float fuzzy;
	};

	class glass : public material{
	public: 
		glass(float refIndex, float3 c, float3 a, float r, float n, bool rt)
			: ir(refIndex), absorption(a), specu(r), N(n), material(c, rt) {
			type = GLASS; invIr = 1 / ir;
		}
		void fresnel(const float3& I, const float3& N, const float& ior, float& kr)
		{
			float cosi = clamp(dot(I, N) ,-1.0f, 1.0f);
			float etai = 1, etat = ior;				 
			if (cosi > 0) { std::swap(etai, etat); }
			float sint = etai / etat * sqrtf(fmaxf(0.f, 1 - cosi * cosi));
			// Total internal reflection
			if (sint >= 1) {
				kr = 1;
			}
			else {
				float cost = sqrtf(fmaxf(0.f, 1 - sint * sint));
				cosi = fabsf(cosi);
				float Rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
				float Rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
				kr = (Rs * Rs + Rp * Rp) / 2;
			}
			// As a consequence of the conservation of energy, transmittance is given by:
			// kt = 1 - kr;
		}
		float3 RefractRay(const float3& oRayDir, const float3& normal, const float& refRatio) {
			float theta = fmin(dot(-oRayDir, normal), 1.0);	  
			float3 perpendicular = refRatio * (oRayDir + theta * normal);
			float3	parallel = -sqrt(fabs(1.0 - pow(length(perpendicular), 2))) * normal;
			return perpendicular + parallel;									  
		}

		float ir, fresnelVal, specu, N, invIr;
		float3 absorption;
	};

	// -----------------------------------------------------------
	// Scene class
	// We intersect this. The query is internally forwarded to the
	// list of primitives, so that the nearest hit can be returned.
	// For this hit (distance, obj id), we can query the normal and
	// albedo.
	// -----------------------------------------------------------
	class Scene
	{
	public:
		Scene()
		{
			
			//Instantiate scene
			
			
			instantiateScene5();
			b = new bvh(this);
			b->Build();
			
			SetTime(0);

			// Note: once we have triangle support we should get rid of the class
			// hierarchy: virtuals reduce performance somewhat.
		}
		
		void ExportData() {
			std::ofstream myFile(exportFile);
			for (int i = 0; i < size(names); ++i) {
				myFile << names[i];
				if (i != size(names) - 1) myFile << ",";
			}
			myFile << "\n";
			//cout << b->dataCollector->GetTreeDepth() << endl;
			//Insert for loop over all BVH's?
			// {
			myFile << b->dataCollector->GetNodeCount() << ",";
			myFile << b->dataCollector->GetSummedNodeArea() << ",";
			//We should check if we want this per ray or per screen. Per screen gives some big ass numbers haha
			myFile << b->dataCollector->GetIntersectedPrimitives(totIterationNumber) / (1280 * 720) << ",";	
			myFile << b->dataCollector->GetAverageTraversalSteps(totIterationNumber) / (1280 * 720) << ",";
			myFile << b->dataCollector->GetTreeDepth();
			myFile << "\n";
			b->dataCollector->ResetDataCollector();
			// }
			myFile.close();

		}


		void instantiateScene1() {
			defaultAnim = true;
			animOn = raytracer && defaultAnim;
			//Loading sky texture
			skydome = stbi_load("Resources/sky.hdr", &skydomeX, &skydomeY, &skydomeN, 3);
			
			glass* standardGlass = new glass(1.5f, white, float3(0.00f), 0.0f, 0, raytracer);
			diffuse* specularDiff = new diffuse(float3(0.8f), white, 0.6f, 0.4f, 2, raytracer, 0);
			diffuse* lightDiff = new diffuse(float3(0.8f), white, 0.6f, 0.4f, 1200, raytracer, 1.2f);
			diffuse* greenDiff = new diffuse(float3(0.8f), green, 0.6f, 0.4f, 2, raytracer);
			diffuse* blueDiff = new diffuse(float3(0.8f), blue, 0.2f, 0.8f, 4, raytracer);
			diffuse* redDiff = new diffuse(float3(0.8f), red, 0.6f, 0.4f, 2, raytracer);
			diffuse* specReflDiff = new diffuse(float3(0.7f), white, 0.6f, 0.4f, 50, raytracer, 0.0f);
			metal* standardMetal = new metal(0.7f, white, raytracer);
			lights.push_back(new AreaLight(11, float3(0.1f, 1.95f, 1.5f), 4.0f, white, 1.0f, float3(0, -1, 0), 4, raytracer));
			//lights.push_back(new AreaLight(12, float3(0, -0.95, 0.5f), 2.0f, white, 0.5f, float3(0, 1, 0), 4, raytracer));
			planes.push_back(Plane(0, redDiff, float3(1, 0, 0), 3));			// 0: left wall
			planes.push_back(Plane(1, greenDiff, float3(-1, 0, 0), 2.99f));		// 1: right wall
			planes.push_back(Plane(2, specReflDiff, float3(0, 1, 0), 1));			// 2: floor
			planes.push_back(Plane(3, lightDiff, float3(0, -1, 0), 2));			// 3: ceiling
			planes.push_back(Plane(4, lightDiff, float3(0, 0, 1), 3));			// 4: front wall
			planes.push_back(Plane(5, specularDiff, float3(0, 0, -1), 3.99f));		// 5: back wall

			if (animOn) spheres.push_back(Sphere(7, standardGlass, float3(-0.7f, -0.4f, 2.0f), 0.5f));			// 1: bouncing ball
			else spheres.push_back(Sphere(7, standardGlass, float3(-1.5f, -0.5, 2), 0.5f));		    // 1: static ball
			spheres.push_back(Sphere(8, new diffuse(0.8f, white, 0, 0.3f, 0.7f, raytracer), float3(0, 2.5f, -3.07f), 8));		// 2: rounded corners
			if (animOn) cubes.push_back(Cube(9, blueDiff, float3(0), float3(1.15f)));		// 3: spinning cube			
			else cubes.push_back(Cube(9, standardGlass, float3(1.2f, -0.5f, 2.5f), float3(1)));
			meshes.push_back(Mesh(1,"Resources/ico.obj", greenDiff,  float3(0.1f, -0.6f, 1.5f), 0.5f));

		}

		void instantiateScene2() {

			//Loading sky texture
			skydome = stbi_load("Resources/sky.hdr", &skydomeX, &skydomeY, &skydomeN, 3);
			diffuse* blueDiff = new diffuse(float3(0.8f), blue, 0.8f, 0.2f, 1, raytracer);
			diffuse* redDiff = new diffuse(float3(0.8f), red, 0.8f, 0.2f, 1, raytracer);
			diffuse* whiteDiff = new diffuse(0.8f, white, 0.0f, 1.0f, 4, raytracer);
			diffuse* greenDiff = new diffuse(float3(0.8f), green, 0.6f, 0.4f, 2, raytracer);
			glass* standardGlass = new glass(1.5f, white, float3(0.00f), 0.0f, 0, raytracer);
			glass* blueGlass = new glass(1.5f, babyblue, float3(0.1f), 0.0f, 0, raytracer);
			metal* blueMetal = new metal(0.7f, blue, raytracer);
			metal* standardMetal = new metal(0.7f, white, raytracer);
			metal* greenMetal = new metal(0.7f, green, raytracer);
			metal* redMetal = new metal(0.7f, red, raytracer);
			metal* yellowMetal = new metal(0.7f, gold, raytracer);
			metal* pinkMetal = new metal(0.7f, pink, raytracer);
			// we store all primitives in one continuous buffer
			lights.push_back(new AreaLight(11, float3(1, 2.0f, 1), 10.0f, white, 1.0f, float3(0, -1, 0), 4, raytracer));
			lights.push_back(new AreaLight(12, float3(-1, 2.0f, -1), 5.0f, white, 1.0f, float3(0, -1, 0), 4, raytracer));

			planes.push_back(Plane(0, whiteDiff, float3(0, 1, 0), 1));			// 0: floor
			planes.push_back(Plane(4, blueDiff, float3(0, 0, -1), 10));			// 4: front wall
			planes.push_back(Plane(1, greenDiff, float3(-1, 0, 0), 2.99f));		// 1: right wall

			spheres.push_back(Sphere(7, blueMetal, float3(-0.7f, -0.5f, 2.0f), 0.5f));
			spheres.push_back(Sphere(8, greenMetal, float3(-1.9f, -0.5f, 2.0f), 0.5f));
			spheres.push_back(Sphere(9, yellowMetal, float3(-3.1f, -0.5f, 2.0f), 0.5f));
			spheres.push_back(Sphere(6, pinkMetal, float3(-4.3f, -0.5f, 2.0f), 0.5f));
			//triangles.push_back(Mesh(10, standardMetal, "Resources/icos.obj", float3(0.5f, -0.51f, 2), 0.5f));
			meshes.push_back(Mesh(1,"Resources/unity.tri", redMetal));
		}


		void instantiateScene3() {

			//Loading sky texture
			skydome = stbi_load("Resources/night.hdr", &skydomeX, &skydomeY, &skydomeN, 3);

			glass* standardGlass = new glass(1.5f, white, float3(0.00f), 0.0f, 0, raytracer);
			glass* greenGlass = new glass(1.5f, green, float3(0.00f), 0.0f, 0, raytracer);
			glass* pinkGlass = new glass(1.5f, pink, float3(0.00f), 0.0f, 0, raytracer);
			diffuse* specularDiff = new diffuse(float3(0.8f), white, 0.6f, 0.4f, 2, raytracer);
			diffuse* lightDiff = new diffuse(float3(0.8f), white, 0.6f, 0.4f, 1200, raytracer);
			diffuse* greenDiff = new diffuse(float3(0.8f), green, 0.6f, 0.4f, 2, raytracer);
			diffuse* blueDiff = new diffuse(float3(0.8f), blue, 0.8f, 0.2f, 1, raytracer);
			diffuse* goldDiff = new diffuse(float3(0.8f), gold, 0.8f, 0.2f, 1, raytracer);
			diffuse* pinkDiff = new diffuse(float3(0.8f), pink, 0.8f, 0.2f, 1, raytracer);
			diffuse* redDiff = new diffuse(float3(0.8f), red, 0.6f, 0.4f, 2, raytracer);
			metal* greenMetal = new metal(0.7f, green, raytracer);
			metal* goldMetal = new metal(0.7f, gold, raytracer);
			metal* blueMetal = new metal(0.7f, blue, raytracer);

			// we store all primitives in one continuous buffer
			lights.push_back(new AreaLight(11, float3(0.1f, 3, 1.5f), 10.0f, white, 1.0f, float3(0, -1, 0), 4, raytracer));
			lights.push_back(new DirectionalLight(12, float3(5, 3, -1), 10.0f, white, float3(-1, -1, 1), 1, raytracer));

			planes.push_back(Plane(0, new diffuse(0.8f, red, 0.0f, 1.0f, 4, raytracer), float3(0, 1, 0), 1));

			float3 threePos = float3(0, 0, 2);
			float threeScale = 2.5f;
			meshes.push_back(Mesh(1,"Resources/three.obj", greenDiff, threePos, threeScale));
			spheres.push_back(Sphere(1, blueMetal, float3(0.410241, -0.085121, -0.122131) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(2, pinkGlass, float3(0.122131, -0.085121, 0.410241) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(3, blueMetal, float3(-0.410241, -0.085121, 0.122131) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(4, standardGlass, float3(-0.122131, -0.085121, -0.410241) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(5, greenMetal, float3(0.500000, -0.367977, -0.001909) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(6, pinkGlass, float3(0.001909, -0.367977, 0.500000) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(7, greenMetal, float3(-0.500000, -0.367977, 0.001909) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(8, standardGlass, float3(-0.001909, -0.367977, -0.500000) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(8, blueMetal, float3(0.236091, 0.198982, -0.236091) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(8, pinkGlass, float3(0.236091, 0.198982, 0.236091) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(8, greenMetal, float3(-0.236091, 0.198982, 0.236091) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			spheres.push_back(Sphere(8, standardGlass, float3(-0.236091, 0.198982, -0.236091) * threeScale + threePos - float3(0, 0.05f, 0), 0.05f));
			cubes.push_back(Cube(9, goldDiff, float3(2, -0.5, 2.5), float3(1)));
			cubes.push_back(Cube(9, pinkDiff, float3(2.2, -0.75, 1), float3(0.5)));
			cubes.push_back(Cube(9, blueDiff, float3(1.5, -0.875, 1.5), float3(0.25)));
			cubes.push_back(Cube(9, redDiff, float3(2.2, -0.875, 1.8), float3(0.25)));
			cubes.push_back(Cube(9, greenDiff, float3(1.55, -0.925, 1.25), float3(0.15)));
			meshes.push_back(Mesh(2,"Resources/stellatedDode.obj", goldMetal, float3(0.000000, 0.561019, 0.000000) * threeScale + threePos + float3(0, 0.25f, 0), 0.4f));

			meshes.push_back(Mesh(3,"Resources/three.obj", greenDiff,  threePos, threeScale));
			spheres.push_back(Sphere(1, standardGlass, float3(0.410241, -0.085121, -0.122131) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(2, standardGlass, float3(0.122131, -0.085121, 0.410241) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(3, standardGlass, float3(-0.410241, -0.085121, 0.122131) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(4, standardGlass, float3(-0.122131, -0.085121, -0.410241) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(5, standardGlass, float3(0.500000, -0.367977, -0.001909) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(6, standardGlass, float3(0.001909, -0.367977, 0.500000) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(7, standardGlass, float3(-0.500000, -0.367977, 0.001909) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(8, standardGlass, float3(-0.001909, -0.367977, -0.500000) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(8, standardGlass, float3(0.236091, 0.198982, -0.236091) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(8, standardGlass, float3(0.236091, 0.198982, 0.236091) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(8, standardGlass, float3(-0.236091, 0.198982, 0.236091) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			spheres.push_back(Sphere(8, standardGlass, float3(-0.236091, 0.198982, -0.236091) * threeScale + threePos- float3(0, 0.05f,0) , 0.05f));
			cubes.push_back(Cube(9, goldDiff, float3(2, -0.5, 2.5), float3(1)));
			cubes.push_back(Cube(9, pinkDiff, float3(2.2, -0.75, 1), float3(0.5)));
			cubes.push_back(Cube(9, blueDiff, float3(1.5, -0.875, 1.5), float3(0.25)));
			cubes.push_back(Cube(9, redDiff, float3(2.2, -0.875, 1.8), float3(0.25)));
			cubes.push_back(Cube(9, greenDiff, float3(1.55, -0.925, 1.25), float3(0.15)));
			meshes.push_back(Mesh(4,"Resources/stellatedDode.obj", goldMetal,  float3(0.000000, 0.561019, 0.000000) * threeScale + threePos +float3(0, 0.25f, 0), 0.4f));
		}

		void instantiateScene4() {

			//Loading sky texture
			skydome = stbi_load("Resources/sky.hdr", &skydomeX, &skydomeY, &skydomeN, 3);

			glass* standardGlass = new glass(1.5f, white, float3(0.00f), 0.0f, 0, raytracer);
			glass* blueGlass = new glass(1.5f, babyblue, float3(0.0f), 0.0f, 0, raytracer);
			metal* standardMetal = new metal(0.7f, white, raytracer);
			diffuse* lightDiff = new diffuse(float3(0.8f), pink, 0.6f, 0.4f, 30, raytracer);
			diffuse* goldDiff = new diffuse(float3(0.8f), gold, 0.6f, 0.4f, 30, raytracer);
			// we store all primitives in one continuous buffer
			lights.push_back(new AreaLight(11, float3(1.8f, 2.0f, 5.5f), 10.0f, white, 2.0f, float3(0, 1, 0), 4, raytracer));
			//lights.push_back(new AreaLight(12, float3(0.1f, 1.8f, 1.5f), 5.0f, white, 1.0f, float3(0, -1, 0), 4, raytracer));

			planes.push_back(Plane(0, new diffuse(0.8f, white, 0.0f, 1.0f, 4, raytracer), float3(0, 1, 0), 1));			// 2: floor

			spheres.push_back(Sphere(7, blueGlass, float3(-0.7f, -0.5f, 2.0f), 0.5f));
			spheres.push_back(Sphere(8, standardMetal, float3(-2.2f, -0.5f, 2.0f), 0.5f));
			spheres.push_back(Sphere(9, lightDiff, float3(-3.7f, -0.5f, 2.0f), 0.5f));
			spheres.push_back(Sphere(5, goldDiff, float3(1.8f, -0.5f, 2.0f), 0.5f));
			meshes.push_back(Mesh(1,"Resources/ico.obj", standardMetal, float3(0.5f, -0.51f, 2), 0.5f));
		}

		void instantiateScene5() {

			//Loading sky texture
			skydome = stbi_load("Resources/sky.hdr", &skydomeX, &skydomeY, &skydomeN, 3);

			diffuse* goldDiff = new diffuse(float3(0.8f), gold, 0.6f, 0.4f, 30, raytracer);

			lights.push_back(new AreaLight(11, float3(1, 2.0f, 1), 10.0f, white, 1.0f, float3(0, -1, 0), 4, raytracer));
			lights.push_back(new AreaLight(12, float3(-1, 2.0f, -1), 5.0f, white, 1.0f, float3(0, -1, 0), 4, raytracer));

			planes.push_back(Plane(0, new diffuse(0.8f, white, 0.0f, 1.0f, 4, raytracer), float3(0, 1, 0), 0));			// 2: floor

			meshes.push_back(Mesh(1,"Resources/lowBigB.obj", goldDiff, float3(0), 1));
			//meshes.push_back(Mesh(1,"Resources/BigB.obj", goldDiff, float3(0,0.5f,0), 1));
		}


		/*void ParseUnityFile(char* path, material* m) {
			FILE* file = fopen(path, "r");
			float a, c, d, e, f, g, h, i, j;
			int res = 1;
			int count = 0;
			while (res > 0) {
				count++;
				res = fscanf(file, "%f %f %f %f %f %f %f %f %f\n",
					&a, &c, &d, &e, &f, &g, &h, &i, &j);
				tri.push_back(Triangle(i, m, float3(a, c, d), float3(e, f, g), float3(h, i, j)));
				original.push_back(Triangle(i, m, float3(a, c, d), float3(e, f, g), float3(h, i, j)));
			}
			fclose(file);
		}*/

		void SetTime(float t)
		{
			// default time for the scene is simply 0. Updating/ the time per frame 
			// enables animation. Updating it per ray can be used for motion blur.
			// light source animation: swing
			animTime = t;

			/*if (animOn) {
				// cube animation: spin
				mat4 M2base = mat4::RotateX(PI / 4) * mat4::RotateZ(PI / 4);
				mat4 M2 = mat4::Translate(float3(1.4f, 0, 2)) * mat4::RotateY(animTime * 0.5f) * M2base;
				if (size(cubes) >= 1) cubes[0].M = M2, cubes[0].invM = M2.FastInvertedTransformNoScale();

				// sphere animation: bounce
				float tm = 1 - sqrf(fmodf(animTime, 2.0f) - 1);
				if (size(spheres) >= 1) spheres[0].pos = float3(-1.4f, -0.5f + tm, 2);
			}*/

			if (animOn) {
				float r = fmodf(t, 2 * PI);
				float a = sinf(r) * 0.5f;
				for (int i = 0; i < size(meshes); i++)
				{
					for (int j = 0; j < size(meshes[i].vertices); j++)
					{
						float3 o = meshes[i].originalVerts[j];
						float s = a * o.y * 0.2f;
						float x = o.x * cosf(s) - o.y * sinf(s);
						float y = o.x * sinf(s) + o.y * cosf(s);
						meshes[i].vertices[j] = float3(x, y, o.z);
					}
					meshes[i].update();
				}
			}
			if (animOn) b->Refit();
		}

		void FindNearest(Ray& ray, float t_min) const
		{
			ray.objIdx = -1;
			/*for (int i = 0; i < size(planes); ++i) planes[i].Intersect(ray, t_min);
			for (int i = 0; i < size(spheres); ++i) spheres[i].Intersect(ray, t_min);
			for (int i = 0; i < size(cubes); ++i) cubes[i].Intersect(ray, t_min);
			for (int i = 0; i < size(meshes); ++i) meshes[i].Intersect(ray, t_min);*/
			if (!raytracer) for (int i = 0; i < size(lights); ++i) lights[i]->Intersect(ray, t_min);
			
			b->Intersect(ray);
			
		}

		bool IsOccluded(Ray& ray, float t_min) const
		{
			float rayLength = ray.t;
			for (int i = 0; i < size(planes); ++i)
				if(planes[i].IsOccluding(ray, t_min)) return true;
			for (int i = 0; i < size(spheres); ++i) 
				if (spheres[i].IsOccluding(ray, t_min)) return true;
			for (int i = 0; i < size(cubes); ++i)
				if (cubes[i].IsOccluding(ray, t_min)) return true;
			for (int i = 0; i < size(meshes); ++i)
				if (meshes[i].IsOccluding(ray, t_min)) return true;

			return false;
			// - we potentially search beyond rayLength
		}
		float3 GetAlbedo(int objIdx, float3 I) const
		{
			if (objIdx == -1) return float3(0); // or perhaps we should just crash
			//if (objIdx >= 7 && objIdx < 7 + size(obj)) return obj[objIdx - 7]->GetAlbedo(I);
			return planes[objIdx].GetAlbedo(I);
			// once we have triangle support, we should pass objIdx and the bary-
			// centric coordinates of the hit, instead of the intersection location.
		}
		float GetReflectivity(int objIdx, float3 I) const
		{
			if (objIdx == 1 /* ball */) return 1;
			if (objIdx == 6 /* floor */) return 0.3f;
			return 0;
		}
		float GetRefractivity(int objIdx, float3 I) const
		{
			return objIdx == 3 ? 1.0f : 0.0f;
		}

		float3 GetSkyColor(Ray &r) const
		{	
			float3 horizontalProj = float3(r.D.x, 0, r.D.z);
			float cHeight = dot(r.D, float3(0, -1, 0));
			float cOrient = dot(float3(0, 0, 1), normalize(horizontalProj));
			float sOrient = dot(float3(1, 0, 0), normalize(horizontalProj));
			sOrient = sOrient > 0 ? 1 : -1;
			int y = ((cHeight + 1) / 2) * (skydomeY-1);
			int x = (((sOrient * acos(cOrient))+PI)/ TWOPI )* (skydomeX-1);
			if (x >= skydomeX) x  = skydomeX - 1;
			if (y >= skydomeY) y  = skydomeY - 1;
			uint8_t* pixelOffset = skydome + (x + skydomeX * y) * skydomeN;
			return float3(uint3(pixelOffset[0], pixelOffset[1], pixelOffset[2]))/255;
		}

		uint getTriangleNb() {
			uint acc = 0;
			for (int i = 0; i < size(meshes); i++) {
				acc += meshes[i].getSize();
			}
			return acc;
		}

		Triangle getTriangle(uint idx) {
			int i = 0;
			while (idx >= size(meshes[i].faces) && i < getTriangleNb()) {
				idx -= size(meshes[i].faces);
				i++;
			}
			return meshes[i].tri[idx];
		}

		void SetIterationNumber(int i) { iterationNumber = i; }

		int GetIterationNumber() { return iterationNumber; }

		void toogleRaytracer() {
			raytracer = !raytracer;
			SetIterationNumber(1);
			animOn = raytracer && defaultAnim;
		}

		__declspec(align(64)) // start a new cacheline here
			float animTime = 0;

		int skydomeX, skydomeY, skydomeN;
		string exportFile = "bvhData.csv";
		vector<string> names = { "Total Node Count", "Summed Node Area"
			, "Average Primitive Intersections per screen", "Average Traversal Steps per screen",
			"Max Tree Depth" };

		unsigned char* skydome;
		bvh* b;
		vector<Light*> lights;
		vector<Cube> cubes;
		vector<Sphere> spheres;
		vector<Mesh> meshes;
		vector<Plane> planes;
		int aaSamples = 1;
		int invAaSamples = 1 / aaSamples;
		int iterationNumber = 1;
		int totIterationNumber = 0;
		bool raytracer = true;
		float mediumIr = 1.0f;
		bool defaultAnim = true;
		bool animOn = raytracer && defaultAnim; // set to false while debugging to prevent some cast error from primitive object type
		const float3 white = float3(1.0, 1.0, 1.0);
		const float3 red = float3(255, 0, 0) / 255;
		const float3 blue = float3(0, 0, 255) / 255;
		const float3 babyblue = float3(0.6f, 0.6f, 1.0f);
		const float3 green = float3(0, 255, 0) / 255;
		const float3 gold = float3(255, 215, 11) / 255;
		const float3 pink = float3(255, 20, 147) / 255;
	};
}