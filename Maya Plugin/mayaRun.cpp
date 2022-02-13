#include "maya_includes.h"
#include "MessageStructure.h"
#include <maya/MTimer.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <queue>

#define PLUGINNAME "[MayaApi] - "

MCallbackId callbackId;
MCallbackIdArray callbackIdArray;
MStatus status = MS::kSuccess;

MTimer gTimer;

// keep track of created meshes to maintain them
std::queue<MObject> addedNodeList;

// Maya command once
// commandPort -n ":1234"

/*
 * how Maya calls this method when a node is added.
 * new POLY mesh: kPolyXXX, kTransform, kMesh
 * new MATERIAL : kBlinn, kShadingEngine, kMaterialInfo
 * new LIGHT    : kTransform, [kPointLight, kDirLight, kAmbientLight]
 * new JOINT    : kJoint
 */
void addCallbacks();

void nodeAdded(MObject& node, void* clientData);
void nodeRemoved(MObject& node, void* clientData);
void cameraUpdate(const MString& modelPanel, void* clientData);

// TODO: merge Add/Update/Remove. They dont differ much and can be the same function
// 
// exp: 'void getMesh(MObject& node, ACTIVITY activity);'
//		'void attributeChangedMesh(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
//

void meshAdd(MObject& node);
void meshUpdate(MObject& node);
void meshRemove(MObject& node);

void materialAdd(MObject& node);
void materialUpdate(MObject& node);
void materialRemove(MObject& node);

void transformAdd(MObject& node);
void transformUpdate(MObject& node);
void transformRemove(MObject& node);

void lightAdd(MObject& node);
void lightUpdate(MObject& node);
void lightRemove(MObject& node);

MString getName(MObject& node);

void eventCallback(void* clientData);
void attributeChangedMesh(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
void attributeChangedShadingEngine(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
void attributeChangedMaterial(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
void attributeChangedTextureFile(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
void attributeChangedTransform(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
void attributeCallbackInfo(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug);

void updateCamera();
void appendCallback(MString name, MCallbackId* id, MStatus* status);

void nodeAdded(MObject& node, void* clientData)
{
	MString str = PLUGINNAME;
	str += "Node Added (";
	str += node.apiTypeStr();
	str += "): '";
	str += getName(node);
	str += "' ";
	str += status.errorString();
	MGlobal::displayInfo(str);

	// As newly added nodes are not yet fully completed/connected in the dependency graph, some functionalities will be unavailable
	// Therfore, the added nodes will be stored for a later point when functionalities are available
	addedNodeList.push(node);

	// When no more connections are being made, MEventMessage will trigger an 'idle' callback
	// At this point, functionalities are avaiable and the stored nodes can be utilzed
	MEventMessage::addEventCallback("idle", eventCallback, NULL, &status);

}

void nodeRemoved(MObject& node, void* clientData) {

	MFnDependencyNode nodeFn(node);

	MString str = PLUGINNAME;
	str += "Node Removed (";
	str += node.apiTypeStr();
	str += "): '";
	str += nodeFn.name();
	str += "'";
	MGlobal::displayInfo(str);

	if (node.hasFn(MFn::kTransform))
	{
		transformRemove(node);
	}

	if (node.hasFn(MFn::kMesh))
	{
		meshRemove(node);
	}

	if (node.hasFn(MFn::kMaterial))
	{
		materialRemove(node);
	}
}

void cameraUpdate(const MString& modelPanel, void* clientData) {

	// Set a timer to update to reduce delay in 3D Renderer
	gTimer.endTimer();

	if (gTimer.elapsedTime() > 0.015)
	{
		updateCamera();

		gTimer.clear();
		gTimer.beginTimer();
	}
}

void meshAdd(MObject& node)
{
	MFnMesh mesh(node, &status);

	if (status == MStatus::kSuccess)
	{
		// Fetch data from mesh	
		int numPoints = 0;
		int numTriangles = 0;
		char materialID[37]{};

		MFloatArray posArr; // Vertex positions
		MFloatArray uvArr;	// Vertex uvs
		MFloatArray norArr; // Vertex normals

		MItMeshPolygon polyIter(node);

		// Iterate each face
		for (; !polyIter.isDone(); polyIter.next())
		{

			// Get the number of tris per face
			int currentNumTris = 0;
			polyIter.numTriangles(currentNumTris);
			numTriangles += currentNumTris;

			// Get vertex position and vertex id from all trinagles per face 
			MPointArray points;
			MIntArray vertexList;
			polyIter.getTriangles(points, vertexList);
			numPoints += points.length();

			// Iterate through each point of all trinagles per face (3 * 2)
			for (size_t i = 0; i < points.length(); i++)
			{
				posArr.append(points[i].x);
				posArr.append(points[i].y);
				posArr.append(points[i].z);

				// Get vertex UVs
				float2 uvPoint;
				polyIter.getUVAtPoint(points[i], uvPoint);
				uvArr.append(uvPoint[0]);
				uvArr.append(1.0 - uvPoint[1]);

				// Get vertex normals
				MVector normalPoint;
				polyIter.getNormal(normalPoint);
				norArr.append(normalPoint.x);
				norArr.append(normalPoint.y);
				norArr.append(normalPoint.z);

			}
		}

		// Get connected mat uuid
		MItDependencyGraph itSE(node, MFn::kShadingEngine, MItDependencyGraph::kDownstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel);
		for (; !itSE.isDone(); itSE.next())
		{
			materialAdd(itSE.currentItem()); // Send material used by mesh

			MFnDependencyNode shadingEngine(itSE.currentItem());
			MPlug surfaceShader = shadingEngine.findPlug("surfaceShader", &status);
			if (status == MS::kSuccess)
			{
				MPlugArray plugArr;
				surfaceShader.connectedTo(plugArr, true, false);
				if (plugArr.length())
				{
					MFnDependencyNode mat(plugArr[0].node());
					std::memcpy(materialID, mat.uuid().asString().asChar(), 37);
				}
			}
		}

		// Message data
		sHeader mainHeader;
		mainHeader.activity = ADD;
		mainHeader.type = MESH;
		std::memcpy(mainHeader.nodeID, mesh.uuid().asString().asChar(), 37);

		sMeshHeader meshHeader;
		meshHeader.vertexCount = numPoints;
		meshHeader.triangleCount = numTriangles;
		std::memcpy(meshHeader.connectedMatID, materialID, 37);

		sMeshData meshData;
		meshData.posXYZ = new float[posArr.length()];
		posArr.get(meshData.posXYZ);
		meshData.UV = new float[uvArr.length()];
		uvArr.get(meshData.UV);
		meshData.norXYZ = new float[norArr.length()];
		norArr.get(meshData.norXYZ);

		// Message send
		msgSize = 0;
		msgSize += sizeof(sHeader);
		msgSize += sizeof(sMeshHeader);

		msgSize += sizeof(float) * posArr.length();
		msgSize += sizeof(float) * uvArr.length();
		msgSize += sizeof(float) * norArr.length();

		int offset = 0;

		std::memcpy((char*)msg, &mainHeader, sizeof(sHeader));
		offset += sizeof(sHeader);

		std::memcpy((char*)msg + offset, &meshHeader, sizeof(sMeshHeader));
		offset += sizeof(sMeshHeader);

		std::memcpy((char*)msg + offset, meshData.posXYZ, sizeof(float) * posArr.length());
		offset += sizeof(float) * posArr.length();

		std::memcpy((char*)msg + offset, meshData.UV, sizeof(float) * uvArr.length());
		offset += sizeof(float) * uvArr.length();

		std::memcpy((char*)msg + offset, meshData.norXYZ, sizeof(float) * norArr.length());

		comlib.send(msg, msgSize);

		delete[] meshData.posXYZ;
		delete[] meshData.UV;
		delete[] meshData.norXYZ;
	}
}

void meshUpdate(MObject& node)
{
	MFnMesh mesh(node, &status);

	if (status == MStatus::kSuccess)
	{
		// Fetch data from mesh	
		int numPoints = 0;
		int numTriangles = 0;
		char materialID[37]{};

		MFloatArray posArr; // Vertex positions
		MFloatArray uvArr;	// Vertex uvs
		MFloatArray norArr; // Vertex normals

		MItMeshPolygon polyIter(node, &status);

		// Iterate each face
		for (; !polyIter.isDone(); polyIter.next())
		{

			// Get the number of tris per face
			int currentNumTris = 0;
			polyIter.numTriangles(currentNumTris);
			numTriangles += currentNumTris;

			// Get vertex position and vertex id from all trinagles per face 
			MPointArray points;
			MIntArray vertexList;
			polyIter.getTriangles(points, vertexList);
			numPoints += points.length();

			// Iterate through each point of all trinagles per face (3 * 2)
			for (size_t i = 0; i < points.length(); i++)
			{

				posArr.append(points[i].x);
				posArr.append(points[i].y);
				posArr.append(points[i].z);

				// Get vertex UVs
				float2 uvPoint;
				polyIter.getUVAtPoint(points[i], uvPoint);
				uvArr.append(uvPoint[0]);
				uvArr.append(1.0 - uvPoint[1]);

				// Get vertex normals
				MVector normalPoint;
				polyIter.getNormal(normalPoint);
				norArr.append(normalPoint.x);
				norArr.append(normalPoint.y);
				norArr.append(normalPoint.z);

			}
		}


		// Get connected mat uuid
		MItDependencyGraph itSE(node, MFn::kShadingEngine, MItDependencyGraph::kDownstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel);
		for (; !itSE.isDone(); itSE.next())
		{
			MFnDependencyNode shadingEngine(itSE.currentItem());
			MPlug surfaceShader = shadingEngine.findPlug("surfaceShader", &status);
			if (status == MS::kSuccess)
			{
				MPlugArray plugArr;
				surfaceShader.connectedTo(plugArr, true, false);
				if (plugArr.length())
				{
					MFnDependencyNode mat(plugArr[0].node());
					std::memcpy(materialID, mat.uuid().asString().asChar(), 37);
				}
			}
		}


		// Message data
		sHeader mainHeader;
		mainHeader.activity = UPDATE;
		mainHeader.type = MESH;
		std::memcpy(mainHeader.nodeID, mesh.uuid().asString().asChar(), 37);

		sMeshHeader meshHeader;
		meshHeader.vertexCount = numPoints;
		meshHeader.triangleCount = numTriangles;
		std::memcpy(meshHeader.connectedMatID, materialID, 37);

		sMeshData meshData;
		meshData.posXYZ = new float[posArr.length()];
		posArr.get(meshData.posXYZ);
		meshData.UV = new float[uvArr.length()];
		uvArr.get(meshData.UV);
		meshData.norXYZ = new float[norArr.length()];
		norArr.get(meshData.norXYZ);

		// Message send
		msgSize = 0;
		msgSize += sizeof(sHeader);
		msgSize += sizeof(sMeshHeader);

		msgSize += sizeof(float) * posArr.length();
		msgSize += sizeof(float) * uvArr.length();
		msgSize += sizeof(float) * norArr.length();

		int offset = 0;

		std::memcpy((char*)msg, &mainHeader, sizeof(sHeader));
		offset += sizeof(sHeader);

		std::memcpy((char*)msg + offset, &meshHeader, sizeof(sMeshHeader));
		offset += sizeof(sMeshHeader);

		std::memcpy((char*)msg + offset, meshData.posXYZ, sizeof(float) * posArr.length());
		offset += sizeof(float) * posArr.length();

		std::memcpy((char*)msg + offset, meshData.UV, sizeof(float) * uvArr.length());
		offset += sizeof(float) * uvArr.length();

		std::memcpy((char*)msg + offset, meshData.norXYZ, sizeof(float) * norArr.length());

		comlib.send(msg, msgSize);

		delete[] meshData.posXYZ;
		delete[] meshData.UV;
		delete[] meshData.norXYZ;

		//MItDependencyGraph itSE(node, MFn::kShadingEngine, MItDependencyGraph::kDownstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel);
		//for (; !itSE.isDone(); itSE.next())
		//{
		//	materialUpdate(itSE.currentItem());
		//}
	}
}

void meshRemove(MObject& node)
{
	MFnMesh mesh(node, &status);
	if (status == MStatus::kSuccess)
	{
		sHeader mainHeader;
		mainHeader.activity = REMOVE;
		mainHeader.type = MESH;
		std::memcpy(&mainHeader.nodeID, mesh.uuid().asString().asChar(), 37);

		msgSize = sizeof(sHeader);

		std::memcpy((char*)msg, &mainHeader, sizeof(sHeader));

		comlib.send(msg, msgSize);

		MItDependencyGraph itSE(node, MFn::kShadingEngine, MItDependencyGraph::kDownstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel, &status);
		for (; !itSE.isDone(); itSE.next())
		{
			materialRemove(itSE.currentItem());
		}
	}
}

void materialAdd(MObject& node)
{
	MMaterial shadingEngine(node, &status);
	if (status == MStatus::kSuccess)
	{
		// Fetch data from material
		float color[3]{ 0,1,0 };
		int pathSize{ 0 };
		char* texturePath{};

		MFnDependencyNode dependencyNode(node);

		MPlug surfaceShader = dependencyNode.findPlug("surfaceShader", &status);
		MPlugArray materialArr; // Material Out Color plug array

		// Get the materialPlug connected to the surface shader
		surfaceShader.connectedTo(materialArr, true, false, &status);

		if (status == MS::kSuccess)
		{
			// The material node e.g. lambert1, phong1 etc
			MFnDependencyNode material(materialArr[0].node());

			sMaterial materialData;

			MPlug colorPlug = material.findPlug("color", &status);

			if (status == MS::kSuccess)
			{
				MObject data;
				colorPlug.getValue(data);

				MFnNumericData colorData(data);
				colorData.getData(color[0], color[1], color[2]);

				// Is texture connected				
				MItDependencyGraph colorTexIt(colorPlug, MFn::kFileTexture, MItDependencyGraph::kUpstream);

				for (; !colorTexIt.isDone(); colorTexIt.next())
				{
					MFnDependencyNode texture(colorTexIt.currentItem());
					MPlug colorTexturePlug = texture.findPlug("fileTextureName", &status);
					if (status == MStatus::kSuccess)
					{
						MString filePathName;
						status = colorTexturePlug.getValue(filePathName);

						if (filePathName.numChars() > 0 && status == MS::kSuccess)
						{
							pathSize = filePathName.length() + 1;

							texturePath = new char[pathSize];

							memcpy(texturePath, filePathName.asChar(), pathSize);

						}
					}
				}
			}

			MPlug diffusePlug = material.findPlug("diffuse", &status);

			if (status == MS::kSuccess)
			{
				float diffuse;

				diffusePlug.getValue(diffuse);

				color[0] *= (float)diffuse;
				color[1] *= (float)diffuse;
				color[2] *= (float)diffuse;
			}

			// Message data
			sHeader sheader;
			sheader.activity = ADD;
			sheader.type = MATERIAL;
			std::memcpy(sheader.nodeID, material.uuid().asString().asChar(), 37);

			sMaterial smaterial;

			smaterial.color[0] = color[0];
			smaterial.color[1] = color[1];
			smaterial.color[2] = color[2];

			smaterial.pathSize = pathSize;
			smaterial.texturePath = texturePath;

			// Message send
			msgSize = 0;
			msgSize += sizeof(sHeader);
			msgSize += sizeof(sMaterial);
			msgSize += sizeof(char) * pathSize;

			int offset = 0;

			std::memcpy((char*)msg, &sheader, sizeof(sHeader));
			offset += sizeof(sHeader);

			std::memcpy((char*)msg + offset, &smaterial, sizeof(sMaterial));
			offset += sizeof(sMaterial);

			std::memcpy((char*)msg + offset, &smaterial.texturePath[0], smaterial.pathSize);

			comlib.send(msg, msgSize);

			delete[] texturePath;
		}
	}
}

void materialUpdate(MObject& node)
{
	MMaterial shadingEngine(node, &status);
	if (status == MStatus::kSuccess)
	{
		// Fetch data from material
		float color[3]{ 0,1,0 };
		int pathSize{ 0 };
		char* texturePath{};

		MFnDependencyNode dependencyNode(node);

		MPlug surfaceShader = dependencyNode.findPlug("surfaceShader", &status);
		MPlugArray materialArr; // Material Out Color plug array

		// Get the materialPlug connected to the surface shader
		surfaceShader.connectedTo(materialArr, true, false, &status);

		if (status == MS::kSuccess)
		{
			// The material node e.g. lambert1, phong1 etc
			MFnDependencyNode material(materialArr[0].node());

			sMaterial materialData;

			MPlug colorPlug = material.findPlug("color", &status);

			if (status == MS::kSuccess)
			{
				MObject data;
				colorPlug.getValue(data);

				MFnNumericData colorData(data);
				colorData.getData(color[0], color[1], color[2]);

				// Is texture connected				
				MItDependencyGraph colorTexIt(colorPlug, MFn::kFileTexture, MItDependencyGraph::kUpstream);

				for (; !colorTexIt.isDone(); colorTexIt.next())
				{
					MFnDependencyNode texture(colorTexIt.currentItem());
					MPlug colorTexturePlug = texture.findPlug("fileTextureName", &status);
					if (status == MStatus::kSuccess)
					{
						MString filePathName;
						status = colorTexturePlug.getValue(filePathName);

						if (filePathName.numChars() > 0 && status == MS::kSuccess)
						{
							pathSize = filePathName.length() + 1;

							texturePath = new char[pathSize];

							memcpy(texturePath, filePathName.asChar(), pathSize);


						}
					}
				}
			}

			MPlug diffusePlug = material.findPlug("diffuse", &status);

			if (status == MS::kSuccess)
			{
				float diffuse;

				diffusePlug.getValue(diffuse);

				color[0] *= (float)diffuse;
				color[1] *= (float)diffuse;
				color[2] *= (float)diffuse;
			}

			// Message data
			sHeader sheader;
			sheader.activity = UPDATE;
			sheader.type = MATERIAL;
			std::memcpy(sheader.nodeID, material.uuid().asString().asChar(), 37);

			sMaterial smaterial;
			smaterial.color[0] = color[0];
			smaterial.color[1] = color[1];
			smaterial.color[2] = color[2];
			smaterial.pathSize = pathSize;
			smaterial.texturePath = texturePath;

			// Message send
			msgSize = 0;
			msgSize += sizeof(sHeader);
			msgSize += sizeof(sMaterial);
			msgSize += pathSize;  //char arr

			int offset = 0;

			std::memcpy((char*)msg, &sheader, sizeof(sHeader));
			offset += sizeof(sHeader);

			std::memcpy((char*)msg + offset, &smaterial, sizeof(sMaterial));
			offset += sizeof(sMaterial);

			std::memcpy((char*)msg + offset, &smaterial.texturePath[0], smaterial.pathSize);

			comlib.send(msg, msgSize);

			delete[] texturePath;
		}
	}
}

void materialRemove(MObject& node)
{
	MMaterial material(node, &status);
	if (status == MStatus::kSuccess)
	{
		sHeader sheader;
		sheader.activity = REMOVE;
		sheader.type = MATERIAL;
		std::memcpy(&sheader.nodeID, MFnDependencyNode(node).uuid().asString().asChar(), 37);

		msgSize = sizeof(sHeader);

		std::memcpy((char*)msg, &sheader, sizeof(sHeader));

		comlib.send(msg, msgSize);
	}
}

void transformAdd(MObject& node)
{
	MFnTransform transform(node, &status);
	if (status == MStatus::kSuccess)
	{
		MFnDagNode dag(node, &status);

		MDagPath path;
		dag.getPath(path);

		float matrix[4][4];

		path.inclusiveMatrix().get(matrix);

		sHeader mainHeader;
		mainHeader.activity = ADD;
		mainHeader.type = TRANSFORM;
		std::memcpy(mainHeader.nodeID, dag.uuid().asString().asChar(), 37);

		sTransform transformData;
		transformData = {
			matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0],
			matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1],
			matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2],
			matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]
		};

		// Message send
		msgSize = 0;
		msgSize += sizeof(sHeader);
		msgSize += sizeof(sTransform);

		int offset = 0;

		std::memcpy((char*)msg, &mainHeader, sizeof(sHeader));
		offset += sizeof(sHeader);

		std::memcpy((char*)msg + offset, &transformData, sizeof(sTransform));

		comlib.send(msg, msgSize);

	}
}

void transformUpdate(MObject& node)
{
	// When calling this func it will also be called for every child
	// And as child can be either Transform or Mesh, a check is required
	MFnTransform transform(node, &status);
	if (status == MStatus::kSuccess)
	{
		MFnDagNode dag(node, &status);

		MDagPath path;
		dag.getPath(path);

		float matrix[4][4];

		path.inclusiveMatrix().get(matrix);

		sHeader mainHeader;
		mainHeader.activity = UPDATE;
		mainHeader.type = TRANSFORM;
		std::memcpy(mainHeader.nodeID, dag.uuid().asString().asChar(), 37);

		sTransform transformData;
		transformData = {
			matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0],
			matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1],
			matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2],
			matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]
		};

		// Message send
		msgSize = 0;
		msgSize += sizeof(sHeader);
		msgSize += sizeof(sTransform);

		int offset = 0;

		std::memcpy((char*)msg, &mainHeader, sizeof(sHeader));
		offset += sizeof(sHeader);

		std::memcpy((char*)msg + offset, &transformData, sizeof(sTransform));

		comlib.send(msg, msgSize);

		MString str = PLUGINNAME;
		str += "AttributeChange (";
		str += node.apiTypeStr();
		str += "): ";
		str += dag.name();
		str += " (";
		str += matrix[3][0];
		str += ",";
		str += matrix[3][1];
		str += ",";
		str += matrix[3][2];
		str += ")";

		//MGlobal::displayInfo(str);

		for (int i = 0; i < path.childCount(); i++)
		{
			transformUpdate(path.child(i));
		}

	}
}

void transformRemove(MObject& node)
{
	MFnTransform transform(node, &status);
	if (status == MStatus::kSuccess)
	{
		sHeader mainHeader;
		mainHeader.activity = REMOVE;
		mainHeader.type = TRANSFORM;
		std::memcpy(&mainHeader.nodeID, transform.uuid().asString().asChar(), 37);

		msgSize = sizeof(sHeader);

		std::memcpy((char*)msg, &mainHeader, sizeof(sHeader));

		comlib.send(msg, msgSize);
	}
}

void lightAdd(MObject& node)
{
	MFnLight light(node, &status);
	if (status == MStatus::kSuccess)
	{

	}
}

void lightUpdate(MObject& node)
{
	MFnLight light(node, &status);
	if (status == MStatus::kSuccess)
	{

	}
}

void lightRemove(MObject& node)
{
	MFnLight light(node, &status);
	if (status == MStatus::kSuccess)
	{

	}
}

MString getName(MObject& node)
{
	// Get full path name from MObject 
	if (node.hasFn(MFn::kDagNode))
	{
		return MFnDagNode(node).fullPathName();
	}
	else
	{
		return MFnDependencyNode(node).name();
	}
}

void eventCallback(void* clientData)
{
	// Node added deferred to a point in time where node is fully completed/connected in the dependency graph

	// Remove the 'idle' callback event as it will continue to be called
	MEventMessage::removeCallback(MEventMessage::currentCallbackId());

	// Fetch the front node and remove it from the list
	MObject node(addedNodeList.front());
	if (addedNodeList.size() > 0)
	{
		addedNodeList.pop();
	}

	// Transform: The relevant transform, when fully completed/connected in the dependency graph, always has a mesh child
	//		e.g. SVG adds transform nodes without any mesh children, these are irrelevant for render application and will not be shared

	MFnTransform transform(node, &status);
	if (status == MS::kSuccess)
	{
		MDagPath dagPath;
		transform.getPath(dagPath);

		if (dagPath.hasFn(MFn::kMesh))
		{
			transformAdd(node);

			callbackId = MNodeMessage::addAttributeChangedCallback(node, attributeChangedTransform, kDefaultNodeType, &status);
			appendCallback("AddAttributeChangedCallback(transform)", &callbackId, &status);
		}
	}

	// Mesh: The relevant mesh, when fully completed/connected in the dependency graph, always has vertices and is not an IntermediateObject
	//		e.g. Polygon 3D Text adds an IntermediateObject mesh node, these are irrelevant for render application and will not be shared

	MFnMesh mesh(node, &status);
	if (status == MS::kSuccess)
	{
		if (!mesh.isIntermediateObject())
		{
			meshAdd(node);

			callbackId = MNodeMessage::addAttributeChangedCallback(node, attributeChangedMesh, kDefaultNodeType, &status);
			appendCallback("AddAttributeChangedCallback(mesh)", &callbackId, &status);
		}
	}

	// Material: The relevant material, when fully completed/connected in the dependency graph, is always connected to their respective shading engine in the "surfaceShader" plug
	//		e.g. Assigning a new material like Phong creates a new shading engine node PhongSG which can be attached to MMaterial function set. However, the relevant data is in the Phong node which do not attach to MMaterial
	//		also the initial material 'lambert1' is connected to two shading engines, initialShadingGroup and initialParticleSE. The second shading engine is irrelevant and can be excluded to avoid double callbacks from 'lambert1'

	MMaterial shadingEngine(node, &status);
	if (status == MS::kSuccess)
	{
		materialAdd(node);

		callbackId = MNodeMessage::addAttributeChangedCallback(node, attributeChangedShadingEngine, kDefaultNodeType, &status);
		appendCallback("AddAttributeChangedCallback(shadingEngine)", &callbackId, &status);

	}
	
	MFnDependencyNode material(node, &status);
	if (status == MS::kSuccess)
	{
		MPlug outColor = material.findPlug("outColor", &status);
		if (status == MS::kSuccess)
		{
			callbackId = MNodeMessage::addAttributeChangedCallback(node, attributeChangedMaterial, kDefaultNodeType, &status);
			appendCallback("AddAttributeChangedCallback(material)", &callbackId, &status);
		}
	}

	if (node.apiType() == MFn::kFileTexture)
	{
		MFnDependencyNode texture(node, &status);
		if (status == MS::kSuccess)
		{
			callbackId = MNodeMessage::addAttributeChangedCallback(node, attributeChangedTextureFile, kDefaultNodeType, &status);
			appendCallback("AddAttributeChangedCallback(texture)", &callbackId, &status);
		}
	}
}

void attributeChangedTextureFile(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData)
{
	if (plug.info().indexW("outColor") > -1) {

		MItDependencyGraph itSE(plug.node(), MFn::kShadingEngine, MItDependencyGraph::kDownstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel);
		for (; !itSE.isDone(); itSE.next())
		{
			MMaterial shadingEngine(itSE.currentItem(), &status);
			if (status == MS::kSuccess)
			{
				materialUpdate(itSE.currentItem());
			}
		}
	}
	attributeCallbackInfo(msg, plug, otherPlug);
}

void attributeChangedMaterial(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData)
{
	MItDependencyGraph itSE(plug.node(), MFn::kShadingEngine, MItDependencyGraph::kDownstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel);
	for (; !itSE.isDone(); itSE.next())
	{
		MItDependencyGraph itMesh(itSE.currentItem(), MFn::kMesh, MItDependencyGraph::kUpstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel);

		for (; !itMesh.isDone(); itMesh.next())
		{
			materialUpdate(itSE.currentItem());
		}
	}
	attributeCallbackInfo(msg, plug, otherPlug);
}

void attributeChangedMesh(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData)
{

	if (msg & (MNodeMessage::kAttributeEval + MNodeMessage::kIncomingDirection)) {

		if (plug.info().indexW("outMesh") > -1)
		{
			MItMeshVertex vertIter(plug.node(), &status);

			if (status == MS::kSuccess)
			{
				meshUpdate(plug.node());
			}
		}
	}

	if (msg & (MNodeMessage::kConnectionMade)) {
		meshUpdate(plug.node());
	}

	attributeCallbackInfo(msg, plug, otherPlug);

}

void attributeChangedShadingEngine(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData)
{
	MMaterial shadingEngine(plug.node(), &status);
	if (status == MS::kSuccess)
	{
		materialUpdate(plug.node());

		MItDependencyGraph itMesh(plug.node(), MFn::kMesh, MItDependencyGraph::kUpstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kNodeLevel);

		for (; !itMesh.isDone(); itMesh.next())
		{
			meshUpdate(itMesh.currentItem());
		}
	}
	attributeCallbackInfo(msg, plug, otherPlug);
}

void attributeChangedTransform(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData)
{

	if (msg & (MNodeMessage::kAttributeSet + MNodeMessage::kIncomingDirection))
	{
		// Transform callback
		if (plug.node().apiType() == MFn::kTransform)
		{
			transformUpdate(plug.node());
		}
	}
	attributeCallbackInfo(msg, plug, otherPlug);

}

void attributeCallbackInfo(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug)
{
	MString nodeMsgStr = PLUGINNAME;
	nodeMsgStr += "AttributeChange (";
	nodeMsgStr += plug.node().apiTypeStr();
	nodeMsgStr += "): ";
	nodeMsgStr += plug.name();
	nodeMsgStr += " ";
	nodeMsgStr += plug.info().indexW("outColor");
	nodeMsgStr += " ";
	nodeMsgStr += otherPlug.name();
	nodeMsgStr += " - ";

	if (msg & MNodeMessage::kConnectionMade)
	{
		nodeMsgStr += " kConnectionMade";
	}

	if (msg & MNodeMessage::kConnectionBroken)
	{
		nodeMsgStr += " kConnectionBroken";
	}

	if (msg & MNodeMessage::kAttributeEval)
	{
		nodeMsgStr += " kAttributeEval";
	}

	if (msg & MNodeMessage::kAttributeSet)
	{
		nodeMsgStr += " kAttributeSet";
	}

	if (msg & MNodeMessage::kAttributeLocked)
	{
		nodeMsgStr += " kAttributeLocked";
	}

	if (msg & MNodeMessage::kAttributeUnlocked)
	{
		nodeMsgStr += " kAttributeUnlocked";
	}

	if (msg & MNodeMessage::kAttributeAdded)
	{
		nodeMsgStr += " kAttributeAdded";
	}

	if (msg & MNodeMessage::kAttributeRemoved)
	{
		nodeMsgStr += " kAttributeRemoved";
	}

	if (msg & MNodeMessage::kAttributeRenamed)
	{
		nodeMsgStr += " kAttributeRenamed";
	}

	if (msg & MNodeMessage::kAttributeKeyable)
	{
		nodeMsgStr += " kAttributeKeyable";
	}

	if (msg & MNodeMessage::kAttributeUnkeyable)
	{
		nodeMsgStr += " kAttributeUnkeyable";
	}

	if (msg & MNodeMessage::kIncomingDirection)
	{
		nodeMsgStr += " kIncomingDirection";
	}

	if (msg & MNodeMessage::kAttributeArrayAdded)
	{
		nodeMsgStr += " kAttributeArrayAdded";
	}

	if (msg & MNodeMessage::kAttributeArrayRemoved)
	{
		nodeMsgStr += " kAttributeArrayRemoved";
	}

	if (msg & MNodeMessage::kOtherPlugSet)
	{
		nodeMsgStr += " kOtherPlugSet";
		nodeMsgStr += " ";
		nodeMsgStr += otherPlug.info();
	}

	if (msg & MNodeMessage::kLast)
	{
		nodeMsgStr += " kLast";
	}

	MGlobal::displayInfo(nodeMsgStr);
}

void updateCamera()
{
	float position[4];
	double target[4];
	double up[3];
	double fovy;
	bool projection;

	// Get active camera
	MDagPath cameraPath;
	M3dView::active3dView().getCamera(cameraPath);
	MFnCamera camera(cameraPath);

	// Get data from active camera
	camera.eyePoint(MSpace::kWorld).get(position);
	camera.centerOfInterestPoint(MSpace::kWorld).get(target);
	camera.upDirection(MSpace::kWorld).get(up);
	fovy = camera.verticalFieldOfView() * (180.0 / 3.141592653589793238463);
	projection = camera.isOrtho();

	sHeader mainHeader;

	mainHeader.activity = UPDATE;
	mainHeader.type = CAMERA;
	std::memcpy(&mainHeader.nodeID, camera.uuid().asString().asChar(), 37);

	sCamera cam{
		{position[0], position[1], position[2]},						// position
		{(float)target[0], (float)target[1], (float)target[2]},			// forward
		{(float)up[0], (float)up[1], (float)up[2]},						// up
		(float)fovy,													// fovy
		projection														// projection
	};

	// Send data

	msgSize = sizeof(sHeader) + sizeof(sCamera);

	std::memcpy((char*)msg, &mainHeader, sizeof(sHeader));
	std::memcpy((char*)msg + sizeof(sHeader), &cam, sizeof(sCamera));

	comlib.send(msg, msgSize);
}

void appendCallback(MString name, MCallbackId* id, MStatus* status) {

	MString str = PLUGINNAME;
	str += name;
	str += ":";

	if (*status == MStatus::kSuccess)
	{
		str += " Success ";
		callbackIdArray.append(*id);
	}
	else
	{
		str += " Failed ";
	}

	MGlobal::displayInfo(str);
}

void addCallbacks()
{
	// Add callback to already existing nodes

	MItDependencyNodes nodes; // dependency node iterator

	for (; !nodes.isDone(); nodes.next())
	{
		MFnDependencyNode node(nodes.item(), &status); // single dependency node

		if (status == MS::kSuccess)
		{
			nodeAdded(node.object(), NULL);
		}
	}

	// Add callbacks to future nodes

	callbackId = MDGMessage::addNodeAddedCallback(nodeAdded, kDefaultNodeType, NULL, &status);
	appendCallback("NodeAddedCallback", &callbackId, &status);

	callbackId = MDGMessage::addNodeRemovedCallback(nodeRemoved, kDefaultNodeType, NULL, &status);
	appendCallback("NodeRemovedCallback", &callbackId, &status);

	// Add callback to camera

	// 1 top	= "modelPanel1"
	// 2 front	= "modelPanel2"
	// 3 left	= "modelPanel3"
	// 4 persp	= "modelPanel4"

	MString activeCameraPanelName;
	activeCameraPanelName = MGlobal::executeCommandStringResult("getPanel -wf");

	callbackId = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel1"), cameraUpdate, NULL, &status);
	appendCallback("ViewPreRenderMsgCallback(modelPanel1)", &callbackId, &status);

	callbackId = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel2"), cameraUpdate, NULL, &status);
	appendCallback("ViewPreRenderMsgCallback(modelPanel2)", &callbackId, &status);

	callbackId = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel3"), cameraUpdate, NULL, &status);
	appendCallback("ViewPreRenderMsgCallback(modelPanel3)", &callbackId, &status);

	callbackId = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel4"), cameraUpdate, NULL, &status);
	appendCallback("ViewPreRenderMsgCallback(modelPanel4)", &callbackId, &status);
}

EXPORT MStatus initializePlugin(MObject obj) {

	MFnPlugin myPlugin(obj, "level editor", "1.0", "Any", &status);

	if (MFAIL(status)) {
		CHECK_MSTATUS(status);
		return status;
	}

	MGlobal::displayInfo(PLUGINNAME + MString("Plugin initialize"));

	// redirect cout to cerr, so that when we do cout goes to cerr
	// in the maya output window (not the scripting output!)
	std::cout.set_rdbuf(MStreamUtils::stdOutStream().rdbuf());
	std::cerr.set_rdbuf(MStreamUtils::stdErrorStream().rdbuf());

	//update camera on init
	updateCamera();

	// register callbacks here
	addCallbacks();

	// a handy timer, courtesy of Maya
	gTimer.clear();
	gTimer.beginTimer();

	return status;
}

EXPORT MStatus uninitializePlugin(MObject obj) {
	MFnPlugin plugin(obj);

	MGlobal::displayInfo(PLUGINNAME + MString("Plugin uninitialize"));

	MMessage::removeCallbacks(callbackIdArray);

	return MS::kSuccess;
}