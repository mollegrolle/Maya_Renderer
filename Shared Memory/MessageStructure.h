#pragma once

#include "ComLib.h"
// 1 << 10 // 1024 // 1kb
// 1 << 20 // 1048576 // 1048kb // 1mb
// 1 << 30 // 1073741824 // 1073741kb // 1073mb // 1gb

#define BUFFERSIZE 8<<20 // 1048 MB
#define MSGSIZE 5<<20 // 5 MB

ComLib comlib("MayaToRender", BUFFERSIZE);

char* msg = new char[MSGSIZE];
size_t msgSize = 0;

enum ACTIVITY { ADD, UPDATE, REMOVE };
enum NODETYPE { MESH, MATERIAL, CAMERA, TRANSFORM, LIGHT };

struct sHeader {
	ACTIVITY activity;			// Add / Update / Remove
	NODETYPE type;				// Mesh / Camera / Transform etc
	char nodeID[37];			// uuid[36] + '\0'[1]
};

struct sCamera {
	float position[3];			// Postion
	float target[3];			// Forward
	float up[3];				// Up
	float fovy;					// Fovy
	bool projection;			// Projection
};

//TODO: merge mesh structs into one
struct sMeshHeader {
	int vertexCount;			// Number of vertices stored in arrays
	int triangleCount;			// Number of triangles stored (indexed or not)	
	char connectedMatID[37];	// uuid[36] + '\0'[1]
};

struct sMeshData {
	float* posXYZ;				// Vertex position (XYZ - 3 components per vertex) (shader-location = 0)
	float* UV;					// Vertex texture coordinates (UV - 2 components per vertex) (shader-location = 1)
	float* norXYZ;				// Vertex normals (XYZ - 3 components per vertex) (shader-location = 2)
};

struct sTransform {
	float m0, m4, m8, m12;		// Transform 4x4 matrix
	float m1, m5, m9, m13;
	float m2, m6, m10, m14;
	float m3, m7, m11, m15;
};

struct sMaterial {
	float color[3];
	int pathSize;
	char *texturePath;
};

// Not in use
struct sLight {
	float position[3];
	float intensity;
	int color[3];
};