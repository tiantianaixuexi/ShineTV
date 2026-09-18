// Fill out your copyright notice in the Description page of Project Settings.


#include "Tools/FunctionLibrary/ShineToolsBpFunction.h"

#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMeshEditor.h"
#include "Generators/DiscMeshGenerator.h"

using namespace UE::Geometry;

bool UShineToolsBpFunction::ShineBitMaskAnd(int A, int B)
{
	return A & B;
}

