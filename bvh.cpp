#include "precomp.h"

//#define USE_SSE			//FASTER without? very weird

bvh::bvh(Scene* s) {
	scene = s; 
	splitMethod = BINNEDSAH;
	dataCollector = new DataCollector();
	mesh = nullptr;
}
bvh::bvh(Mesh* m) {
	mesh = m;
	splitMethod = BINNEDSAH;
	scene = nullptr;
	dataCollector = new DataCollector();
}

void bvh::Build(bool isQ) {
	if (scene != nullptr) {
		NTri = scene->getTriangleNb();
		NSph = size(scene->spheres);
		NPla = size(scene->planes);
	}else if (mesh != nullptr) {
		NTri = size(mesh->faces);
		NSph = 0;
		NPla = 0;
	}
	cout << "#Tri : " << NTri << endl;
	cout << "#Sph : " << NSph << endl;
	cout << "#Pla : " << NPla << endl;
	N = NTri + NSph + NPla;
	primitiveIdx = new uint[N];
	bvhNode = new BVHNode[2 * (N + 1) - 1];
	isQBVH = isQ;
	Timer t;
	for (uint i = 0; i < N; ++i) {
		primitiveIdx[i] = i;
	}

	BVHNode& root = bvhNode[rootNodeIdx];
	root.primCount = N;
	root.leftFirst = 0;

	UpdateNodeBounds(rootNodeIdx);
	cout << "Subdivison star" << endl;
	if (isQBVH) QSubdivide(rootNodeIdx);
	else separatePlanes(rootNodeIdx);
	bounds.grow(root.aabbMin);
	bounds.grow(root.aabbMax);
	printf("BVH Build time : %5.2f ms \n", t.elapsed() * 1000);
	dataCollector->UpdateBuildTime(t.elapsed() * 1000);
	t.reset();
	Refit();
	printf("BVH Refit time : %5.2f ms \n", t.elapsed() * 1000);
	dataCollector->UpdateNodeCount(nodesUsed);
}

Triangle bvh::getTriangle(uint idx) {
	if (scene != nullptr) {
		return scene->getTriangle(idx);
	}
	else if (mesh != nullptr) {
		return mesh->tri[idx];
	}
}

void bvh::UpdateNodeBounds(uint nodeIdx) {
	BVHNode& node = bvhNode[nodeIdx];
	node.aabbMin = float3(1e30f);
	node.aabbMax = float3(-1e30f);
	for (uint first = node.leftFirst, i = 0; i < node.primCount; i++) {
		uint leafIdx = primitiveIdx[first + i];

		if (leafIdx < NTri ) {
			Triangle& leafTri = getTriangle(leafIdx);
			node.aabbMin = fminf(node.aabbMin, leafTri.v0);
			node.aabbMin = fminf(node.aabbMin, leafTri.v1);
			node.aabbMin = fminf(node.aabbMin, leafTri.v2);
			node.aabbMax = fmaxf(node.aabbMax, leafTri.v0);
			node.aabbMax = fmaxf(node.aabbMax, leafTri.v1);
			node.aabbMax = fmaxf(node.aabbMax, leafTri.v2);
			dataCollector->UpdateSummedArea(node.aabbMin, node.aabbMax);
		} else if (leafIdx >= NTri && leafIdx< NTri+NSph){
			leafIdx -= NTri;
			Sphere& leafSph = scene->spheres[leafIdx];
			node.aabbMin = fminf(node.aabbMin, leafSph.pos - float3(leafSph.r));
			node.aabbMax = fmaxf(node.aabbMax, leafSph.pos + float3(leafSph.r));
			dataCollector->UpdateSummedArea(node.aabbMin, node.aabbMax);
		}
		else {
			leafIdx -= NTri + NSph;
			Plane& leafPla = scene->planes[leafIdx];
			float3 normal = normalize(leafPla.N);
			if (normal.x + normal.y + normal.z == 1 && (normal.x == 1 || normal.y == 1 || normal.z == 1)) {
				if (normal.x == 1) {
					node.aabbMin = fminf(node.aabbMin, float3(0, -1e30f, -1e30f));
					node.aabbMax = fmaxf(node.aabbMax, float3(0, 1e30f, 1e30f));
				} else
				if (normal.y == 1) {
					node.aabbMin = fminf(node.aabbMin, float3(-1e30f, 0, -1e30f));
					node.aabbMax = fmaxf(node.aabbMax, float3(1e30f, 0, 1e30f));
				} else
				if (normal.z == 1) {
					node.aabbMin = fminf(node.aabbMin, float3(-1e30f, -1e30f, 0));
					node.aabbMax = fmaxf(node.aabbMax, float3(1e30f, 1e30f, 0));
				}
			} else{
				node.aabbMin = float3(-1e30f);
				node.aabbMax = float3(1e30f);
				return;
			}
		}
	}
}

float bvh::FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos)
{
	const int BINS = 8;
	float bestCost = 1e30f;
	for (int a = 0; a < 3; a++)
	{
		float boundsMin = 1e30f, boundsMax = -1e30f;
		for (int i = 0; i < node.primCount; i++)
		{
			uint primIdx = primitiveIdx[node.leftFirst + i];
			if (primIdx < NTri) {
				Triangle& triangle = getTriangle(primIdx);
				boundsMin = min(boundsMin, triangle.centroid[a]);
				boundsMax = max(boundsMax, triangle.centroid[a]);
			}else if (primIdx >= NTri && primIdx < NTri + NSph) {
				primIdx -= NTri;
				Sphere& sphere = scene->spheres[primIdx];
				boundsMin = min(boundsMin, sphere.pos[a]);
				boundsMax = max(boundsMax, sphere.pos[a]);
			}

		}
		if (boundsMin == boundsMax) continue;
		// populate the bins
		Bin bin[BINS];
		float scale = BINS / (boundsMax - boundsMin);
		for (uint i = 0; i < node.primCount; i++)
		{
			uint primIdx = primitiveIdx[node.leftFirst + i];
			int binIdx; 
			if (primIdx < NTri) {
				Triangle& triangle = getTriangle(primIdx);
				binIdx = min(BINS - 1,
					(int)((triangle.centroid[a] - boundsMin) * scale));
				bin[binIdx].primCount++;
				bin[binIdx].bounds.grow(triangle.v0);
				bin[binIdx].bounds.grow(triangle.v1);
				bin[binIdx].bounds.grow(triangle.v2);
			} else if (primIdx >= NTri && primIdx < NTri + NSph) {
				primIdx -= NTri;
				Sphere& sphere = scene->spheres[primIdx];
				binIdx = min(BINS - 1,
					(int)((sphere.pos[a] - boundsMin) * scale));
				bin[binIdx].primCount++;
				bin[binIdx].bounds.grow(sphere.pos - float3(2*sphere.r));
				bin[binIdx].bounds.grow(sphere.pos + float3(2*sphere.r));
			}
		}
		
		float leftArea[BINS - 1], rightArea[BINS - 1];
		int leftCount[BINS - 1], rightCount[BINS - 1];
		aabb leftBox, rightBox;
		int leftSum = 0, rightSum = 0;
		for (int i = 0; i < BINS - 1; i++)
		{
			leftSum += bin[i].primCount;
			leftCount[i] = leftSum;
			leftBox.grow(bin[i].bounds);
			leftArea[i] = leftBox.area();
			rightSum += bin[BINS - 1 - i].primCount;
			rightCount[BINS - 2 - i] = rightSum;
			rightBox.grow(bin[BINS - 1 - i].bounds);
			rightArea[BINS - 2 - i] = rightBox.area();
		}

		// calculate SAH cost for the 7 planes
		scale = (boundsMax - boundsMin) / BINS;
		for (int i = 0; i < BINS - 1; i++)
		{
			float planeCost =
				leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
			if (planeCost < bestCost)
				axis = a, splitPos = boundsMin + scale * (i + 1),
				bestCost = planeCost;
		}
	}
	return bestCost;
}


float bvh::CalculateNodeCost(BVHNode& node) {
	float3 e = node.aabbMax - node.aabbMin; // extent of parent
	float surfaceArea = e.x * e.y + e.y * e.z + e.z * e.x;
	return node.primCount * surfaceArea;
}

void bvh::separatePlanes(uint nodeIdx) {
	BVHNode& node = bvhNode[nodeIdx];
	if (NPla > 0 && (NSph + NTri > 0)) {
		// create child nodes
		int leftChildIdx = nodesUsed++;
		int rightChildIdx = nodesUsed++;
		bvhNode[leftChildIdx].leftFirst = 0;
		bvhNode[leftChildIdx].primCount = NTri + NSph;
		bvhNode[rightChildIdx].leftFirst = NTri + NSph;
		bvhNode[rightChildIdx].primCount = NPla;
		node.leftFirst = leftChildIdx;
		node.primCount = 0;
		UpdateNodeBounds(leftChildIdx);
		UpdateNodeBounds(rightChildIdx);
		// recurse
		Subdivide(leftChildIdx);
	} else {
		Subdivide(nodeIdx);
	}
}

void bvh::Subdivide(uint nodeIdx) {
	BVHNode& node = bvhNode[nodeIdx];
	// determine split axis using SAH
	int axis; float splitPos;
	switch (splitMethod) {
		case SplitMethod::BINNEDSAH: {
			float splitCost = FindBestSplitPlane(node, axis, splitPos);
			float nosplitCost = CalculateNodeCost(node);
			if (splitCost >= nosplitCost) return;
			break;
		}
		case SplitMethod::LONGESTAXIS: {
			float3 extent = node.aabbMax - node.aabbMin;
			axis = 0;
			if (extent.y > extent.x) axis = 1;
			if (extent.z > extent[axis]) axis = 2;
			splitPos = node.aabbMin[axis] + extent[axis] * 0.5f;
			break;
		}
		case SplitMethod::SAMESIZE: {
			float3 extent = node.aabbMax - node.aabbMin;
			axis = 0;
			if (extent.y > extent.x) axis = 1;
			if (extent.z > extent[axis]) axis = 2;
			int m = node.primCount / 2;
			vector<tuple<float, int>> sorted;
			float min = 1e-30f;
			for (uint i = 0; i < node.primCount; i++)
			{
				uint primIdx = primitiveIdx[node.leftFirst + i];
				if (primIdx < NTri) {
					Triangle& triangle = getTriangle(primIdx);
					sorted.push_back(make_tuple(triangle.centroid[axis],primIdx));
				}
				else if (primIdx >= NTri && primIdx < NTri + NSph) {
					primIdx -= NTri;
					Sphere& sphere = scene->spheres[primIdx];
					sorted.push_back(make_tuple(sphere.pos[axis], primIdx));
				}
			}
			sort(sorted.begin(), sorted.end());
			float mid = get<0>(sorted[m]);
			//splitPos = node.aabbMin[axis] + extent[axis] * 0.5f;
			splitPos = mid;
			break;
		}
		case SplitMethod::SAH: {
			int bestAxis = -1;
			float bestPos = 0, bestCost = 1e30f;
			float candidatePos = 0;
			for(int a = 0; a < 3; a++) for(uint i = 0; i < node.primCount; i++){
				uint primIdx = primitiveIdx[node.leftFirst + i];
				if (primIdx < NTri) {
					Triangle& triangle = getTriangle(primIdx);
					candidatePos = triangle.centroid[a];
				}
				else if (primIdx >= NTri && primIdx < NTri + NSph) {
					primIdx -= NTri;
					Sphere& sphere = scene->spheres[primIdx];
					candidatePos = sphere.pos[a];
				}
				float splitCost = EvaluateSAH(node, a, candidatePos);
				if (splitCost < bestCost)
					bestPos = candidatePos, bestAxis = a, bestCost = splitCost;
			}

			axis = bestAxis;
			splitPos = bestPos;
			break;
		}
	}

	// in-place partition
	int i = node.leftFirst;
	int j = i + node.primCount - 1;
	while (i <= j)
	{
		uint primIdx = primitiveIdx[i];
		if (primIdx < NTri) {
			if (getTriangle(primIdx).centroid[axis] < splitPos)
				i++;
			else
				swap(primitiveIdx[i], primitiveIdx[j--]);
		} else if (primIdx >= NTri && primIdx < N) {
			primIdx -= NTri;
			if (scene->spheres[primIdx].pos[axis] < splitPos)
				i++;
			else
				swap(primitiveIdx[i], primitiveIdx[j--]);
		}
	}
	// abort split if one of the sides is empty
	int leftCount = i - node.leftFirst;
	if (leftCount == 0 || leftCount == node.primCount) return;
	// create child nodes
	int leftChildIdx = nodesUsed++;
	int rightChildIdx = nodesUsed++;
	bvhNode[leftChildIdx].leftFirst = node.leftFirst;
	bvhNode[leftChildIdx].primCount = leftCount;
	bvhNode[rightChildIdx].leftFirst = i;
	bvhNode[rightChildIdx].primCount = node.primCount - leftCount;
	node.leftFirst = leftChildIdx;
	node.primCount = 0;
	dataCollector->UpdateTreeDepth(false);
	UpdateNodeBounds(leftChildIdx);
	UpdateNodeBounds(rightChildIdx);
	// recurse
	Subdivide(leftChildIdx);
	Subdivide(rightChildIdx);
	dataCollector->UpdateTreeDepth(true);
}

void bvh::Cut(uint nodeIdx, int& axis, float& splitPos) {
	BVHNode& node = bvhNode[nodeIdx];
	switch (splitMethod) {
		case SplitMethod::BINNEDSAH: {
			float splitCost = FindBestSplitPlane(node, axis, splitPos);
			float nosplitCost = CalculateNodeCost(node);
			if (splitCost >= nosplitCost) return;
			break;
		}
		case SplitMethod::LONGESTAXIS: {
			float3 extent = node.aabbMax - node.aabbMin;
			axis = 0;
			if (extent.y > extent.x) axis = 1;
			if (extent.z > extent[axis]) axis = 2;
			splitPos = node.aabbMin[axis] + extent[axis] * 0.5f;
			break;
		}
		case SplitMethod::SAMESIZE: {
			float3 extent = node.aabbMax - node.aabbMin;
			axis = 0;
			if (extent.y > extent.x) axis = 1;
			if (extent.z > extent[axis]) axis = 2;
			//splitPos = node.aabbMin[axis] + extent[axis] * 0.5f;
			splitPos = (node.aabbMin[axis] + node.aabbMax[axis]) * 0.5f;
			break;
		}
		case SplitMethod::SAH: {
			int bestAxis = -1;
			float bestPos = 0, bestCost = 1e30f;
			float candidatePos = 0;
			for (int a = 0; a < 3; a++) for (uint i = 0; i < node.primCount; i++) {
				uint primIdx = primitiveIdx[node.leftFirst + i];
				if (primIdx < NTri) {
					Triangle& triangle = getTriangle(primIdx);
					candidatePos = triangle.centroid[a];
				}
				else if (primIdx >= NTri && primIdx < NTri + NSph) {
					primIdx -= NTri;
					Sphere& sphere = scene->spheres[primIdx];
					candidatePos = sphere.pos[a];
				}
				float splitCost = EvaluateSAH(node, axis, candidatePos);
				if (splitCost < bestCost)
					bestPos = candidatePos, bestAxis = a, bestCost = splitCost;
			}
			axis = bestAxis;
			splitPos = bestPos;
			break;
		}
	}
}

int bvh::Partition(uint nodeIdx, int axis, float splitPos) {
	BVHNode& node = bvhNode[nodeIdx];
	// in-place partition
	int i = node.leftFirst;
	int j = i + node.primCount - 1;
	while (i <= j)
	{
		uint primIdx = primitiveIdx[i];
		if (primIdx < NTri) {
			if (getTriangle(primIdx).centroid[axis] < splitPos)
				i++;
			else
				swap(primitiveIdx[i], primitiveIdx[j--]);
		}
		else if (primIdx >= NTri && primIdx < N) {
			primIdx -= NTri;
			if (scene->spheres[primIdx].pos[axis] < splitPos)
				i++;
			else
				swap(primitiveIdx[i], primitiveIdx[j--]);
		}
	}
	// abort split if one of the sides is empty
	int leftCount = i - node.leftFirst;
	if (leftCount == 0 || leftCount == node.primCount) return -1;
	//cout << " Split[" << node.leftFirst << "," << node.leftFirst + leftCount << "," << node.leftFirst + node.primCount << "]" << "Idx :" << nodeIdx << endl;
	return leftCount;
}

void bvh::QSubdivide(uint nodeIdx) {
	BVHNode& node = bvhNode[nodeIdx];
	int axis = -1; float splitPos = 0;

	//cout << "First Cut : IDX :" << nodeIdx << endl;
	Cut(nodeIdx, axis, splitPos);
	int leftCount = Partition(nodeIdx, axis, splitPos);
	if(leftCount == -1) return;

	// create child nodes
	int leftLeftChildIdx = nodesUsed++;
	int leftChildIdx = nodesUsed++;
	int rightChildIdx = nodesUsed++;
	int rightRightChildIdx = nodesUsed++;
	bvhNode[leftLeftChildIdx].leftFirst = node.leftFirst;
	bvhNode[leftLeftChildIdx].primCount = leftCount;
	bvhNode[rightChildIdx].leftFirst = leftCount + node.leftFirst;
	bvhNode[rightChildIdx].primCount = node.primCount - leftCount;
	node.leftFirst = leftLeftChildIdx;
	node.primCount = 0;

	//cout << "Second cut" << endl;
	axis = -1; splitPos = 0;
	Cut(leftLeftChildIdx, axis, splitPos);
	int leftLeftCount = Partition(leftLeftChildIdx, axis, splitPos);
	axis = -1; splitPos = 0;
	Cut(rightChildIdx, axis, splitPos);
	int rightCount = Partition(rightChildIdx, axis, splitPos);
	
	if (leftLeftCount != -1) {
		bvhNode[leftChildIdx].leftFirst = leftLeftCount + bvhNode[leftLeftChildIdx].leftFirst;
		bvhNode[leftChildIdx].primCount = bvhNode[leftLeftChildIdx].primCount - leftLeftCount;
		bvhNode[leftLeftChildIdx].primCount = leftLeftCount;
		if (rightCount == -1) {
			//three nodes case
			//reorder => Not needed
			//set end
			bvhNode[rightRightChildIdx].leftFirst = 1;
			bvhNode[rightRightChildIdx].primCount = 0;
			return;
		}
		else {
			//four nodes : OK
			bvhNode[rightRightChildIdx].leftFirst = rightCount + bvhNode[rightChildIdx].leftFirst;
			bvhNode[rightRightChildIdx].primCount = bvhNode[rightChildIdx].primCount - rightCount;
			bvhNode[rightChildIdx].primCount = rightCount;
		}
	} else {
		if (rightCount == -1) {
			// two nodes case : OK
			// reorder
			bvhNode[leftChildIdx].leftFirst = bvhNode[rightChildIdx].leftFirst;
			bvhNode[leftChildIdx].primCount = bvhNode[rightChildIdx].primCount;
			// set end
			bvhNode[rightChildIdx].leftFirst = 1;
			bvhNode[rightChildIdx].primCount = 0;
			bvhNode[rightRightChildIdx].leftFirst = 1;
			bvhNode[rightRightChildIdx].primCount = 0;
			return;
		}
		else {
			//three nodes : OK
			bvhNode[rightRightChildIdx].leftFirst = rightCount + bvhNode[rightChildIdx].leftFirst;
			bvhNode[rightRightChildIdx].primCount = bvhNode[rightChildIdx].primCount - rightCount;
			bvhNode[rightChildIdx].primCount = rightCount;
			//reorder 
			bvhNode[leftChildIdx].leftFirst = bvhNode[rightChildIdx].leftFirst;
			bvhNode[leftChildIdx].primCount = bvhNode[rightChildIdx].primCount;
			bvhNode[rightChildIdx].leftFirst = bvhNode[rightRightChildIdx].leftFirst;
			bvhNode[rightChildIdx].primCount = bvhNode[rightRightChildIdx].primCount;
			//set end
			bvhNode[rightRightChildIdx].leftFirst = 1;
			bvhNode[rightRightChildIdx].primCount = 0;
			return;
		}
	}

	dataCollector->UpdateTreeDepth(false);
	// recurse
	if (!bvhNode[leftLeftChildIdx].isEmpty()) {
		UpdateNodeBounds(leftLeftChildIdx);
		QSubdivide(leftLeftChildIdx);
	}
	if (!bvhNode[leftChildIdx].isEmpty()) {
		UpdateNodeBounds(leftChildIdx);
		QSubdivide(leftChildIdx);
	}
	if (!bvhNode[rightChildIdx].isEmpty()) {
		UpdateNodeBounds(rightChildIdx);
		QSubdivide(rightChildIdx);
	}
	if (!bvhNode[rightRightChildIdx].isEmpty()) {
		UpdateNodeBounds(rightRightChildIdx);
		QSubdivide(rightRightChildIdx);
	}
	dataCollector->UpdateTreeDepth(true);
}

float bvh::EvaluateSAH(BVHNode& node, int axis, float pos)
{
	// determine triangle counts and bounds for this split candidate
	aabb leftBox, rightBox;
	int leftCount = 0, rightCount = 0;
	for (uint i = 0; i < node.primCount; i++)
	{
		uint primIdx = primitiveIdx[node.leftFirst + i];
		if (primIdx < NTri) {
			Triangle& triangle = getTriangle(primIdx);
			if (triangle.centroid[axis] < pos) {
				leftCount++;
				leftBox.grow(triangle.v0);
				leftBox.grow(triangle.v1);
				leftBox.grow(triangle.v2);
			}
			else {
				rightCount++;
				rightBox.grow(triangle.v0);
				rightBox.grow(triangle.v1);
				rightBox.grow(triangle.v2);
			}
		} else if (primIdx >= NTri && primIdx < N) {
			primIdx -= NTri;
			Sphere& sphere = scene->spheres[primIdx];
			if (sphere.pos[axis] < pos) {
				leftCount++;
				leftBox.grow(sphere.pos[axis] - float3(sphere.r));
				leftBox.grow(sphere.pos[axis] + float3(sphere.r));
			}
			else {
				rightCount++;
				rightBox.grow(sphere.pos[axis] - float3(sphere.r));
				rightBox.grow(sphere.pos[axis] + float3(sphere.r));
			}
		}
			
	}
	float cost = leftCount * leftBox.area() + rightCount * rightBox.area();
	return cost > 0 ? cost : 1e30f;
}

void bvh::Refit()
{
	for (int i = nodesUsed - 1; i >= 0; i--) if (i != 1)
	{
		BVHNode& node = bvhNode[i];
		if (isQBVH && node.isEmpty()) continue;
		if (node.isLeaf())
		{
			// leaf node: adjust bounds to contained triangles
			UpdateNodeBounds(i);
			continue;
		}
		// interior node: adjust bounds to child node bounds
		BVHNode& leftChild = bvhNode[node.leftFirst];
		BVHNode& rightChild = bvhNode[node.leftFirst + 1];
		node.aabbMin = fminf(leftChild.aabbMin, rightChild.aabbMin);
		node.aabbMax = fmaxf(leftChild.aabbMax, rightChild.aabbMax);
		if (isQBVH) {
			BVHNode& child3 = bvhNode[node.leftFirst + 2];
			BVHNode& child4 = bvhNode[node.leftFirst + 3];
			if (!child3.isEmpty() && !child4.isEmpty()) {
				float3 min34 = fminf(child3.aabbMin, child4.aabbMin);
				node.aabbMin = fminf(node.aabbMin, min34);
				float3 max34 = fmaxf(child3.aabbMax, child4.aabbMax);
				node.aabbMax = fmaxf(node.aabbMax, max34);
			}
			else {
				if (child3.isEmpty() && !child4.isEmpty()) {
					node.aabbMin = fminf(node.aabbMin, child4.aabbMin);
					node.aabbMax = fmaxf(node.aabbMax, child4.aabbMax);
				}
				if (child4.isEmpty() && !child3.isEmpty()) {
					node.aabbMin = fminf(node.aabbMin, child3.aabbMin);
					node.aabbMax = fmaxf(node.aabbMax, child3.aabbMax);
				}
			}
		}
	}
}

void bvh::Intersect(Ray& ray) {
	if(isQBVH) QIntersect(ray);
	else BIntersect(ray);
}

bool bvh::IsOccluded(Ray& ray) {
	if (isQBVH) return QIsOccluded(ray);
	else return BIsOccluded(ray);
}

void bvh::BIntersect(Ray& ray) {
	float t_min = 0.0001f;
	BVHNode* node = &bvhNode[rootNodeIdx], *stack[64];
	uint stackPtr = 0;
	int traversalSteps = 0;

	// trace transformed ray
	while(1){
		traversalSteps++;
		if (node->primCount > 0) {
			for (uint i = 0; i < node->primCount; i++) {
				uint primIdx = primitiveIdx[node->leftFirst + i];
				if (primIdx < NTri) {
					getTriangle(primIdx).Intersect(ray, t_min);
				} else if(primIdx >=NTri && primIdx < NTri + NSph){
					primIdx -= NTri;
					scene->spheres[primIdx].Intersect(ray, t_min);
				}
				else {
					primIdx -= NTri + NSph;
					scene->planes[primIdx].Intersect(ray, t_min);
				}
				dataCollector->UpdateIntersectedPrimitives();
			}
			if (stackPtr == 0) { 
				dataCollector->UpdateAverageTraversalSteps(traversalSteps);
				break; 
			}
			else node = stack[--stackPtr];
			continue;
		}

		BVHNode* c1 = &bvhNode[node->leftFirst];
		BVHNode* c2 = &bvhNode[node->leftFirst + 1];
#ifdef USE_SSE
		float dist1 = IntersectAABB_SSE(ray, c1->aabbMin4, c1->aabbMax4);
		float dist2 = IntersectAABB_SSE(ray, c2->aabbMin4, c2->aabbMax4);
#else
		float dist1 = IntersectAABB(ray, c1->aabbMin, c1->aabbMax);
		float dist2 = IntersectAABB(ray, c2->aabbMin, c2->aabbMax);
#endif
		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
		if (dist1 == 1e30f) {
			if (stackPtr == 0) break; else node = stack[--stackPtr];
		}
		else {
			node = c1;
			if (dist2 != 1e30f) stack[stackPtr++] = c2;
		}
	}
}

void bvh::QIntersect(Ray& ray) {
	float t_min = 0.0001f;
	BVHNode* node = &bvhNode[rootNodeIdx], * stack[64];
	uint stackPtr = 0;
	int traversalSteps = 0;

	// trace transformed ray
	while (1) {
		traversalSteps++;
		if (node->isLeaf()) {
			for (uint i = 0; i < node->primCount; i++) {
				uint primIdx = primitiveIdx[node->leftFirst + i];
				getTriangle(primIdx).Intersect(ray, t_min);
			}
			if (stackPtr == 0) {
				break;
			}
			else node = stack[--stackPtr];
			continue;
		}

		BVHNode* c1 = &bvhNode[node->leftFirst];
		BVHNode* c2 = &bvhNode[node->leftFirst + 1];
		BVHNode* c3 = &bvhNode[node->leftFirst + 2];
		BVHNode* c4 = &bvhNode[node->leftFirst + 3];
#ifdef USE_SSE
		float dist1 = IntersectAABB_SSE(ray, c1->aabbMin4, c1->aabbMax4);
		float dist2 = IntersectAABB_SSE(ray, c2->aabbMin4, c2->aabbMax4);
#else

		float dist1 = IntersectAABB(ray, c1->aabbMin, c1->aabbMax);
		float dist2 = IntersectAABB(ray, c2->aabbMin, c2->aabbMax);
		float dist3 = IntersectAABB(ray, c3->aabbMin, c3->aabbMax);
		float dist4 = IntersectAABB(ray, c4->aabbMin, c4->aabbMax);

		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
		if (dist2 > dist3) { swap(dist2, dist3); swap(c2, c3); }
		if (dist3 > dist4) { swap(dist3, dist4); swap(c3, c4); }
		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
		if (dist2 > dist3) { swap(dist2, dist3); swap(c2, c3); }
		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
#endif

		if (dist4 != 1e30f && !c4->isEmpty()) stack[stackPtr++] = c4;
		if (dist3 != 1e30f && !c3->isEmpty()) stack[stackPtr++] = c3;
		if (dist2 != 1e30f && !c2->isEmpty()) stack[stackPtr++] = c2;
		if(dist1 != 1e30f && !c1->isEmpty()) stack[stackPtr++] = c1;
		if (stackPtr == 0) break; else node = stack[--stackPtr];
	}
}

bool bvh::QIsOccluded(Ray& ray) {
	float t_min = 0.0001f;
	BVHNode* node = &bvhNode[rootNodeIdx], * stack[64];
	uint stackPtr = 0;
	int traversalSteps = 0;

	// trace transformed ray
	while (1) {
		traversalSteps++;
		if (node->isLeaf()) {
			for (uint i = 0; i < node->primCount; i++) {
				uint primIdx = primitiveIdx[node->leftFirst + i];
				if (getTriangle(primIdx).IsOccluding(ray, t_min))
					return true;
			}
			if (stackPtr == 0) {
				break;
			}
			else node = stack[--stackPtr];
			continue;
		}

		BVHNode* c1 = &bvhNode[node->leftFirst];
		BVHNode* c2 = &bvhNode[node->leftFirst + 1];
		BVHNode* c3 = &bvhNode[node->leftFirst + 2];
		BVHNode* c4 = &bvhNode[node->leftFirst + 3];
#ifdef USE_SSE
		float dist1 = IntersectAABB_SSE(ray, c1->aabbMin4, c1->aabbMax4);
		float dist2 = IntersectAABB_SSE(ray, c2->aabbMin4, c2->aabbMax4);
#else

		float dist1 = IntersectAABB(ray, c1->aabbMin, c1->aabbMax);
		float dist2 = IntersectAABB(ray, c2->aabbMin, c2->aabbMax);
		float dist3 = IntersectAABB(ray, c3->aabbMin, c3->aabbMax);
		float dist4 = IntersectAABB(ray, c4->aabbMin, c4->aabbMax);

		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
		if (dist2 > dist3) { swap(dist2, dist3); swap(c2, c3); }
		if (dist3 > dist4) { swap(dist3, dist4); swap(c3, c4); }
		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
		if (dist2 > dist3) { swap(dist2, dist3); swap(c2, c3); }
		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
#endif

		if (dist4 != 1e30f && !c4->isEmpty()) stack[stackPtr++] = c4;
		if (dist3 != 1e30f && !c3->isEmpty()) stack[stackPtr++] = c3;
		if (dist2 != 1e30f && !c2->isEmpty()) stack[stackPtr++] = c2;
		if (dist1 != 1e30f && !c1->isEmpty()) stack[stackPtr++] = c1;
		if (stackPtr == 0) break; else node = stack[--stackPtr];
	}

	return false;
}

bool bvh::BIsOccluded(Ray& ray) {
	float t_min = 0.0001f;
	BVHNode* node = &bvhNode[rootNodeIdx], * stack[64];
	uint stackPtr = 0;
	while (1) {
		//if (!IntersectAABB(ray, node->aabbMin, node->aabbMax)) return;
		if (node->primCount > 0) {
			for (uint i = 0; i < node->primCount; i++) {
				uint primIdx = primitiveIdx[node->leftFirst + i];
				if (primIdx < NTri) {
					if (getTriangle(primIdx).IsOccluding(ray, t_min)) return true;
				}
				else if (primIdx >= NTri && primIdx < NTri + NSph) {
					primIdx -= NTri;
					if (scene->spheres[primIdx].IsOccluding(ray, t_min)) return true;
				}
				else {
					primIdx -= NTri + NSph;
					if (scene->planes[primIdx].IsOccluding(ray, t_min)) return true;
				}
			}
			if (stackPtr == 0) return false; else node = stack[--stackPtr];
			continue;
		}

		BVHNode* c1 = &bvhNode[node->leftFirst];
		BVHNode* c2 = &bvhNode[node->leftFirst + 1];
#ifdef USE_SSE
		float dist1 = IntersectAABB_SSE(ray, c1->aabbMin4, c1->aabbMax4);
		float dist2 = IntersectAABB_SSE(ray, c2->aabbMin4, c2->aabbMax4);
#else
		float dist1 = IntersectAABB(ray, c1->aabbMin, c1->aabbMax);
		float dist2 = IntersectAABB(ray, c2->aabbMin, c2->aabbMax);
#endif
		if (dist1 > dist2) { swap(dist1, dist2); swap(c1, c2); }
		if (dist1 == 1e30f) {
			if (stackPtr == 0) return false; else node = stack[--stackPtr];
		}
		else {
			node = c1;
			if (dist2 != 1e30f) stack[stackPtr++] = c2;
		}
	}
}

float bvh::IntersectAABB_SSE(const Ray& ray, const __m128 bmin4, const __m128 bmax4)
{
	static __m128 mask4 = _mm_cmpeq_ps(_mm_setzero_ps(), _mm_set_ps(1, 0, 0, 0));
	__m128 t1 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmin4, mask4), ray.O4), ray.rD4);
	__m128 t2 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmax4, mask4), ray.O4), ray.rD4);
	__m128 vmax4 = _mm_max_ps(t1, t2), vmin4 = _mm_min_ps(t1, t2);
	float tmax = min(vmax4.m128_f32[0], min(vmax4.m128_f32[1], vmax4.m128_f32[2]));
	float tmin = max(vmin4.m128_f32[0], max(vmin4.m128_f32[1], vmin4.m128_f32[2]));
	if (tmax >= tmin && tmin < ray.t && tmax > 0) return tmin; else return 1e30f;
}

float bvh::IntersectAABB(const Ray& ray, const float3 bmin, const float3 bmax)
{
	float tx1 = (bmin.x - ray.O.x) * ray.rD.x, tx2 = (bmax.x - ray.O.x) * ray.rD.x;
	float tmin = min(tx1, tx2), tmax = max(tx1, tx2);
	float ty1 = (bmin.y - ray.O.y) * ray.rD.y, ty2 = (bmax.y - ray.O.y) * ray.rD.y;
	tmin = max(tmin, min(ty1, ty2)), tmax = min(tmax, max(ty1, ty2));
	float tz1 = (bmin.z - ray.O.z) * ray.rD.z, tz2 = (bmax.z - ray.O.z) * ray.rD.z;
	tmin = max(tmin, min(tz1, tz2)), tmax = min(tmax, max(tz1, tz2));
	if (tmax >= tmin && tmin < ray.t && tmax > 0) return tmin; else return 1e30f;
}
