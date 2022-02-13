/*******************************************************************************************
*
*   Raylib external MAYA renderer
*
*   This project has been created using raylib 1.3 (www.raylib.com)
*	This application works like an external render window for Maya 2022.
*	With MayaAPI plugin loaded in Maya, objects, cameras and other activites
*	in the maya viewport are being registered and sent via a shared memory (ComLib)
*	which this Raylib application receives and renders out.
*
*	By Leonard Grolleman
*
********************************************************************************************/

#include <iostream>
#include <string>
#include <vector>
#include <time.h>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "MessageStructure.h"

#define DEBUG 1

// Light data
struct Light {
	int type;
	Vector3 position;
	Vector3 target;
	Color color;
	bool enabled;

	// Shader locations
	int enabledLoc;
	int typeLoc;
	int posLoc;
	int targetLoc;
	int colorLoc;
};

// Light type
enum LightType {
	LIGHT_DIRECTIONAL,
	LIGHT_POINT
};

Light CreateLight(int type, Vector3 position, Vector3 target, Color color, Shader shader);
void UpdateLightValues(Shader shader, Light light);

// Current amount of created lights
int lightsCount = 0;

// Create a light and get shader locations
Light CreateLight(int type, Vector3 position, Vector3 target, Color color, Shader shader)
{
	Light light = { 0 };

	light.enabled = true;
	light.type = type;
	light.position = position;
	light.target = target;
	light.color = color;

	std::string i = std::to_string(lightsCount);

	light.enabledLoc = GetShaderLocation(shader, ("lights[" + i + "].enabled").c_str());
	light.typeLoc = GetShaderLocation(shader, ("lights[" + i + "].type").c_str());
	light.posLoc = GetShaderLocation(shader, ("lights[" + i + "].position").c_str());
	light.targetLoc = GetShaderLocation(shader, ("lights[" + i + "].target").c_str());
	light.colorLoc = GetShaderLocation(shader, ("lights[" + i + "].color").c_str());

	UpdateLightValues(shader, light);

	lightsCount++;

	return light;
}

void UpdateLightValues(Shader shader, Light light)
{

	// Send to shader light enabled state and type
	SetShaderValue(shader, light.enabledLoc, &light.enabled, SHADER_UNIFORM_INT);
	SetShaderValue(shader, light.typeLoc, &light.type, SHADER_UNIFORM_INT);

	// Send to shader light position values
	float position[3] = { light.position.x, light.position.y, light.position.z };
	SetShaderValue(shader, light.posLoc, position, SHADER_UNIFORM_VEC3);

	// Send to shader light target position values
	float target[3] = { light.target.x, light.target.y, light.target.z };
	SetShaderValue(shader, light.targetLoc, target, SHADER_UNIFORM_VEC3);

	// Send to shader light color values
	float color[4] = { (float)light.color.r / (float)255, (float)light.color.g / (float)255,
					   (float)light.color.b / (float)255, (float)light.color.a / (float)255 };
	SetShaderValue(shader, light.colorLoc, color, SHADER_UNIFORM_VEC4);

}

//------------------------------------------------------------------------------------------
// Types and Structures Definition
//------------------------------------------------------------------------------------------


int main(void)
{
	// Initialization
	//--------------------------------------------------------------------------------------
	const int screenWidth = 800;
	const int screenHeight = 450;

	InitWindow(screenWidth, screenHeight, "Maya Raylib Renderer");

	// Define the camera to look into our 3d world
	Camera3D camera = { 0 };
	camera.position = Vector3{ 10.0f, 10.0f, 10.0f }; // Camera position
	camera.target = Vector3{ 0.0f, 0.0f, 0.0f };      // Camera looking at point
	camera.up = Vector3{ 0.0f, 1.0f, 0.0f };          // Camera up vector (rotation towards target)
	camera.fovy = 45.0f;                                // Camera field-of-view Y
	camera.projection = CAMERA_PERSPECTIVE;                   // Camera mode type

	Shader shader = LoadShader("../raylib/examples/shaders/resources/shaders/glsl330/custom/vertexShader.vs", "../raylib/examples/shaders/resources/shaders/glsl330/custom/fragmentShader.fs");

	// Get some required shader loactions
	shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");
	// NOTE: "matModel" location name is automatically assigned on shader loading, 
	// no need to get the location again if using that uniform name
	//shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");

	// Ambient light level (some basic lighting)
	int ambientLoc = GetShaderLocation(shader, "ambient");
	float value[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
	SetShaderValue(shader, ambientLoc, value, SHADER_UNIFORM_VEC4);

	// Identify/find each node
	std::vector<std::string> modelID;
	std::vector<std::string> transformID;
	std::vector<std::string> materialID;

	// Store every node
	std::vector<Model> modelArr;		// cube1, sphere1, cube2, donut1
	std::vector<Matrix> transformArr;	// cube1T, sphere1T, cube2T, donut1T
	std::vector<Material> materialArr;	// lambert1, phong2
	std::vector<int> materialIndexArr;	// 1, 0, 0, 1
	//std::vector<Camera> cameraArr;	// No need to store/idetify camera as only one needed

	Vector3 modelPosition = { 0.0f, 0.0f, 0.0f };

	// Using 4 point lights: gold, red, green and blue
	Light lights[4] = { 0 };
	lights[0] = CreateLight(LIGHT_POINT, Vector3{ -2, 1, -2 }, Vector3Zero(), YELLOW, shader);
	lights[1] = CreateLight(LIGHT_POINT, Vector3{ 2, 1, 2 }, Vector3Zero(), RED, shader);
	lights[2] = CreateLight(LIGHT_POINT, Vector3{ -2, 1, 2 }, Vector3Zero(), GREEN, shader);
	lights[3] = CreateLight(LIGHT_POINT, Vector3{ 2, 1, -2 }, Vector3Zero(), BLUE, shader);

	SetTargetFPS(60); // Set our game to run at 60 frames-per-second

	// Main game loop

	while (!WindowShouldClose()) // Detect window close button or ESC key
	{
		// Shared Memory recv messages
		//----------------------------------------------------------------------------------

		if (comlib.recv(msg, msgSize)) {

			sHeader msgHead{};

			memcpy(&msgHead, (char*)msg, sizeof(sHeader));

			if (msgHead.type == CAMERA)
			{
				//if (DEBUG) std::cout << "UPDATE Camera" << std::endl;

				sCamera msgCam{};

				memcpy(&msgCam, (char*)msg + sizeof(sHeader), sizeof(sCamera));

				camera.position.x = msgCam.position[0];
				camera.position.y = msgCam.position[1];
				camera.position.z = msgCam.position[2];
				camera.target.x = msgCam.target[0];
				camera.target.y = msgCam.target[1];
				camera.target.z = msgCam.target[2];
				camera.up.x = msgCam.up[0];
				camera.up.y = msgCam.up[1];
				camera.up.z = msgCam.up[2];
				camera.fovy = msgCam.fovy;
				camera.projection = msgCam.projection;

			}

			if (msgHead.type == MESH)
			{
				// mesh added
				if (msgHead.activity == ADD)
				{
					if (DEBUG) std::cout << "ADD Mesh [" << msgHead.nodeID << "]" << std::endl;

					sMeshHeader meshHeader{};
					sMeshData meshData{};

					int offset = sizeof(sHeader);

					memcpy(&meshHeader, (char*)msg + offset, sizeof(sMeshHeader));
					offset += sizeof(sMeshHeader);

					meshData.posXYZ = (float*)MemAlloc(meshHeader.vertexCount * 3 * sizeof(float));
					memcpy(meshData.posXYZ, (char*)msg + offset, sizeof(float) * meshHeader.vertexCount * 3);
					offset += sizeof(float) * meshHeader.vertexCount * 3;

					meshData.UV = (float*)MemAlloc(meshHeader.vertexCount * 2 * sizeof(float));
					memcpy(meshData.UV, (char*)msg + offset, sizeof(float) * meshHeader.vertexCount * 2);
					offset += sizeof(float) * meshHeader.vertexCount * 2;

					meshData.norXYZ = (float*)MemAlloc(meshHeader.vertexCount * 3 * sizeof(float));
					memcpy(meshData.norXYZ, (char*)msg + offset, sizeof(float) * meshHeader.vertexCount * 3);

					Mesh tempMesh{};

					tempMesh.vertexCount = meshHeader.vertexCount;
					tempMesh.triangleCount = meshHeader.triangleCount;

					tempMesh.vertices = meshData.posXYZ;
					tempMesh.texcoords = meshData.UV;
					tempMesh.normals = meshData.norXYZ;

					UploadMesh(&tempMesh, false);

					Model tempModel = LoadModelFromMesh(tempMesh);

					tempModel.materials[0] = LoadMaterialDefault();

					tempModel.materials[0].shader = shader;

					modelID.push_back(msgHead.nodeID);
					modelArr.push_back(tempModel);
					materialIndexArr.push_back(0);

					for (int i = 0; i < materialArr.size(); i++)
					{
						if (materialID[i] == meshHeader.connectedMatID)
						{
							for (int j = 0; j < modelID.size(); j++)
							{
								if (msgHead.nodeID == modelID[j])
								{
									materialIndexArr[j] = i;
								}
							}
						}
					}

				}

				// vtx moved / vertex divide
				if (msgHead.activity == UPDATE)
				{
					for (int i = 0; i < modelID.size(); i++)
					{
						if (modelID[i] == msgHead.nodeID)
						{
							if (DEBUG) std::cout << "UPDATE Mesh [" << msgHead.nodeID << "]" << std::endl;

							sMeshHeader meshHeader{};
							sMeshData meshData{};

							int offset = sizeof(sHeader);

							memcpy(&meshHeader, (char*)msg + offset, sizeof(sMeshHeader));
							offset += sizeof(sMeshHeader);

							meshData.posXYZ = (float*)MemAlloc(meshHeader.vertexCount * 3 * sizeof(float));
							memcpy(meshData.posXYZ, (char*)msg + offset, sizeof(float) * meshHeader.vertexCount * 3);
							offset += sizeof(float) * meshHeader.vertexCount * 3;

							meshData.UV = (float*)MemAlloc(meshHeader.vertexCount * 2 * sizeof(float));
							memcpy(meshData.UV, (char*)msg + offset, sizeof(float) * meshHeader.vertexCount * 2);
							offset += sizeof(float) * meshHeader.vertexCount * 2;

							meshData.norXYZ = (float*)MemAlloc(meshHeader.vertexCount * 3 * sizeof(float));
							memcpy(meshData.norXYZ, (char*)msg + offset, sizeof(float) * meshHeader.vertexCount * 3);

							delete[] modelArr.at(i).meshes[0].vertices;
							delete[] modelArr.at(i).meshes[0].texcoords;
							delete[] modelArr.at(i).meshes[0].normals;

							Mesh tempMesh = {};

							tempMesh.vertexCount = meshHeader.vertexCount;
							tempMesh.triangleCount = meshHeader.triangleCount;

							tempMesh.vertices = meshData.posXYZ;
							tempMesh.texcoords = meshData.UV;
							tempMesh.normals = meshData.norXYZ;

							UploadMesh(&tempMesh, false);

							modelArr.at(i) = LoadModelFromMesh(tempMesh);
							modelArr.at(i).materials[0].shader = shader;

							bool foundMat = false;
							for (int j = 0; j < materialArr.size(); j++)
							{
								if (materialID[j] == meshHeader.connectedMatID)
								{
									materialIndexArr.at(i) = j;
									foundMat = true;
								}
							}
							// If connectedMatID can't be found, then it will be pushed-back later
							if (!foundMat)
							{
								materialIndexArr.at(i) = materialArr.size();
							}

						}
					}
				}

				// mesh removed
				if (msgHead.activity == REMOVE)
				{
					for (int i = 0; i < modelID.size(); i++)
					{
						if (modelID[i] == msgHead.nodeID)
						{
							if (DEBUG) std::cout << "REMOVE Mesh [" << msgHead.nodeID << "]" << std::endl;
							modelArr.erase(modelArr.begin() + i);
							modelID.erase(modelID.begin() + i);
							materialIndexArr.erase(materialIndexArr.begin() + i);
						}
					}
				}

			}

			if (msgHead.type == TRANSFORM)
			{
				// transform added
				if (msgHead.activity == ADD)
				{
					if (DEBUG) std::cout << "ADD Transform [" << msgHead.nodeID << "]" << std::endl;


					transformID.push_back(msgHead.nodeID);

					sTransform transform{};

					int offset = sizeof(sHeader);

					memcpy(&transform, (char*)msg + offset, sizeof(sTransform));

					Matrix tempMatrix;

					tempMatrix.m0 = transform.m0;
					tempMatrix.m1 = transform.m1;
					tempMatrix.m2 = transform.m2;
					tempMatrix.m3 = transform.m3;
					tempMatrix.m4 = transform.m4;
					tempMatrix.m5 = transform.m5;
					tempMatrix.m6 = transform.m6;
					tempMatrix.m7 = transform.m7;
					tempMatrix.m8 = transform.m8;
					tempMatrix.m9 = transform.m9;
					tempMatrix.m10 = transform.m10;
					tempMatrix.m11 = transform.m11;
					tempMatrix.m12 = transform.m12;
					tempMatrix.m13 = transform.m13;
					tempMatrix.m14 = transform.m14;
					tempMatrix.m15 = transform.m15;

					transformArr.push_back(tempMatrix);

				}

				// transform moved
				if (msgHead.activity == UPDATE)
				{
					// Maybe can just memcpy Matrix directly
					for (int i = 0; i < transformID.size(); i++)
					{
						if (transformID[i] == msgHead.nodeID)
						{
							if (DEBUG) std::cout << "UPDATE Transform [" << msgHead.nodeID << "]" << std::endl;

							sTransform transform{};

							int offset = sizeof(sHeader);

							memcpy(&transform, (char*)msg + offset, sizeof(sTransform));

							transformArr[i].m0 = transform.m0;
							transformArr[i].m1 = transform.m1;
							transformArr[i].m2 = transform.m2;
							transformArr[i].m3 = transform.m3;
							transformArr[i].m4 = transform.m4;
							transformArr[i].m5 = transform.m5;
							transformArr[i].m6 = transform.m6;
							transformArr[i].m7 = transform.m7;
							transformArr[i].m8 = transform.m8;
							transformArr[i].m9 = transform.m9;
							transformArr[i].m10 = transform.m10;
							transformArr[i].m11 = transform.m11;
							transformArr[i].m12 = transform.m12;
							transformArr[i].m13 = transform.m13;
							transformArr[i].m14 = transform.m14;
							transformArr[i].m15 = transform.m15;
						}
					}
				}

				// transform removed
				if (msgHead.activity == REMOVE)
				{
					for (int i = 0; i < transformID.size(); i++)
					{
						if (transformID[i] == msgHead.nodeID)
						{
							if (DEBUG) std::cout << "REMOVE Transform [" << msgHead.nodeID << "]" << std::endl;
							transformArr.erase(transformArr.begin() + i);
							transformID.erase(transformID.begin() + i);
						}
					}
				}
			}

			if (msgHead.type == MATERIAL)
			{
				// material added
				if (msgHead.activity == ADD)
				{
					//Material material;

					sMaterial smaterial{};

					int offset = sizeof(sHeader);

					memcpy(&smaterial, (char*)msg + offset, sizeof(sMaterial));
					offset += sizeof(sMaterial);

					smaterial.texturePath = new char[smaterial.pathSize];
					memcpy(smaterial.texturePath, (char*)msg + offset, sizeof(char) * smaterial.pathSize);

					Material tempMaterial = LoadMaterialDefault();
					tempMaterial.shader = shader;

					bool duplicate = false;

					for (int i = 0; i < materialID.size(); i++)
					{
						if (materialID[i] == msgHead.nodeID)
						{

							// Pushback the existing material index
							duplicate = true;
						}
					}

					if (!duplicate)
					{
						if (DEBUG) std::cout << "NEW Material [" << msgHead.nodeID << "]" << std::endl;

						// Color
						tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.r = smaterial.color[0] * 255;
						tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.g = smaterial.color[1] * 255;
						tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.b = smaterial.color[2] * 255;

						// Texture
						if (smaterial.pathSize > 0)
						{
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.r = 255;
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.g = 255;
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.b = 255;

							std::cout << smaterial.texturePath << std::endl;
							Texture2D texture = LoadTexture(smaterial.texturePath);
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = texture;
						}

						// Pushback into vector array
						materialID.push_back(msgHead.nodeID);
						materialArr.push_back(tempMaterial);
					}

				}

				// material changed color / texture
				if (msgHead.activity == UPDATE)
				{

					sMaterial smaterial{};

					int offset = sizeof(sHeader);

					memcpy(&smaterial, (char*)msg + offset, sizeof(sMaterial));
					offset += sizeof(sMaterial);

					smaterial.texturePath = new char[smaterial.pathSize];
					memcpy(&smaterial.texturePath[0], (char*)msg + offset, smaterial.pathSize);


					Material tempMaterial = LoadMaterialDefault();
					tempMaterial.shader = shader;

					bool duplicate = false;

					for (int i = 0; i < materialID.size(); i++)
					{
						if (materialID[i] == msgHead.nodeID)
						{
							if (DEBUG) std::cout << "UPDATE Material [" << msgHead.nodeID << "]" << std::endl;
							// Color
							materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].color.r = smaterial.color[0] * 255;
							materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].color.g = smaterial.color[1] * 255;
							materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].color.b = smaterial.color[2] * 255;

							//UnloadTexture(materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].texture);
							// Texture
							if (smaterial.pathSize > 0)
							{
								materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].color.r = 255;
								materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].color.g = 255;
								materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].color.b = 255;

								std::cout << smaterial.texturePath << std::endl;
								Texture2D texture = LoadTexture(smaterial.texturePath);
								materialArr.at(i).maps[MATERIAL_MAP_DIFFUSE].texture = texture;
							}

							duplicate = true;
						}
					}

					if (!duplicate)
					{
						if (DEBUG) std::cout << "NEW Material [" << msgHead.nodeID << "]" << std::endl;

						// Color
						tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.r = smaterial.color[0] * 255;
						tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.g = smaterial.color[1] * 255;
						tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.b = smaterial.color[2] * 255;

						// Texture
						if (smaterial.pathSize > 0)
						{
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.r = 255;
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.g = 255;
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].color.b = 255;

							std::cout << smaterial.texturePath << std::endl;
							Texture2D texture = LoadTexture(smaterial.texturePath);
							tempMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = texture;
						}

						// Pushback into vector array
						materialID.push_back(msgHead.nodeID);
						materialArr.push_back(tempMaterial);
					}

					delete[] smaterial.texturePath;

				}

				// material removed
				if (msgHead.activity == REMOVE)
				{
					for (int i = 0; i < materialID.size(); i++)
					{
						if (materialID[i] == msgHead.nodeID)
						{
							if (DEBUG) std::cout << "REMOVE Material [" << msgHead.nodeID << "]" << std::endl;
							materialArr.erase(materialArr.begin() + i);
							materialID.erase(materialID.begin() + i);
						}
					}
				}
			}
		}

		UpdateCamera(&camera); // Update camera

		// Update light values (actually, only enable/disable them)
		UpdateLightValues(shader, lights[0]);
		UpdateLightValues(shader, lights[1]);
		UpdateLightValues(shader, lights[2]);
		UpdateLightValues(shader, lights[3]);

		// Update the shader with the camera view vector (points towards { 0.0f, 0.0f, 0.0f })
		float cameraPos[3] = { camera.position.x, camera.position.y, camera.position.z };
		SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);

		// Draw
		BeginDrawing();

		ClearBackground(RAYWHITE);

		BeginMode3D(camera);

		for (int i = 0; i < modelArr.size(); i++) {

			Color color{ 255, 255, 255, 255 };

			// Let std::vector::at() catch/throw std::out_of_range exception.
			try
			{
				modelArr.at(i).transform = transformArr.at(i);
			}
			catch (const std::exception&)
			{
				// Material out of range
			}

			try
			{
				modelArr.at(i).materials[0] = materialArr.at(materialIndexArr.at(i));
			}
			catch (const std::exception&)
			{
				// Material out of range
			}

			DrawModel(modelArr[i], {}, 1.0f, color);

		}

		// Draw markers to show where the lights are
		DrawSphereEx(lights[0].position, 0.2f, 8, 8, YELLOW);
		DrawSphereEx(lights[1].position, 0.2f, 8, 8, RED);
		DrawSphereEx(lights[2].position, 0.2f, 8, 8, GREEN);
		DrawSphereEx(lights[3].position, 0.2f, 8, 8, BLUE);


		DrawGrid(20, 1.0f);

		EndMode3D();

		// write text 
		DrawText("Maya API level editor", screenWidth - 120, screenHeight - 20, 10, GRAY);
		DrawText(TextFormat("Camera position: (%.2f, %.2f, %.2f)", camera.position.x, camera.position.y, camera.position.z), 10, 10, 10, GRAY);
		DrawText(TextFormat("Camera target: (%.2f, %.2f, %.2f)", camera.target.x, camera.target.y, camera.target.z), 10, 30, 10, GRAY);
		DrawText(TextFormat("Camera up: (%.2f, %.2f, %.2f)", camera.up.x, camera.up.y, camera.up.z), 10, 50, 10, GRAY);
		DrawText(TextFormat("Camera fovy: (%.2f)", camera.fovy), 10, 70, 10, GRAY);
		DrawText(TextFormat("Camera projection: (%d)", camera.projection), 10, 90, 10, GRAY);
		DrawFPS(10, screenHeight - 20);

		EndDrawing();
	}

	// De-Initialization
	for (int i = 0; i < modelArr.size(); i++) {
		// Unload models
		UnloadModel(modelArr[i]);
	}

	for (int i = 0; i < modelArr.size(); i++)
	{
		// Unload all texture from models
		UnloadTexture(modelArr.at(i).materials[0].maps[MATERIAL_MAP_DIFFUSE].texture);
	}

	UnloadShader(shader);   // Unload shader

	CloseWindow();        // Close window and OpenGL context

	return 0;
}