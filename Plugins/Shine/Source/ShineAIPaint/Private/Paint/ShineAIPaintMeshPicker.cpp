#include "Paint/ShineAIPaintMeshPicker.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Paint/ShineAIPaintSession.h"
#include "RawIndexBuffer.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "StaticMeshResources.h"

namespace
{
    /** 便宜的 AABB slab 测试，用来在遍历三角形前快速剔除。 */
    bool RayHitsBox(const FBox& Box, const FVector& Origin, const FVector& Direction)
    {
        if (!Box.IsValid)
        {
            return true;
        }

        double TMin = 0.0;
        double TMax = TNumericLimits<double>::Max();

        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const double O = Origin[Axis];
            const double D = Direction[Axis];
            const double AxisMin = Box.Min[Axis];
            const double AxisMax = Box.Max[Axis];

            if (FMath::Abs(D) < 1e-9)
            {
                if (O < AxisMin || O > AxisMax)
                {
                    return false;
                }
                continue;
            }

            double T1 = (AxisMin - O) / D;
            double T2 = (AxisMax - O) / D;
            if (T1 > T2)
            {
                Swap(T1, T2);
            }

            TMin = FMath::Max(TMin, T1);
            TMax = FMath::Min(TMax, T2);
            if (TMin > TMax)
            {
                return false;
            }
        }

        return true;
    }

    /** Möller–Trumbore。OutBarycentric 是 (w0, w1, w2)，对应 V0/V1/V2。 */
    bool IntersectTriangle(
        const FVector& Origin,
        const FVector& Direction,
        const FVector& V0,
        const FVector& V1,
        const FVector& V2,
        double& OutDistance,
        FVector& OutBarycentric)
    {
        const FVector Edge1 = V1 - V0;
        const FVector Edge2 = V2 - V0;

        const FVector PVec = FVector::CrossProduct(Direction, Edge2);
        const double Determinant = FVector::DotProduct(Edge1, PVec);
        if (FMath::Abs(Determinant) < 1e-12)
        {
            return false;
        }

        const double InvDeterminant = 1.0 / Determinant;
        const FVector TVec = Origin - V0;

        const double U = FVector::DotProduct(TVec, PVec) * InvDeterminant;
        if (U < -1e-6 || U > 1.0 + 1e-6)
        {
            return false;
        }

        const FVector QVec = FVector::CrossProduct(TVec, Edge1);
        const double V = FVector::DotProduct(Direction, QVec) * InvDeterminant;
        if (V < -1e-6 || U + V > 1.0 + 1e-6)
        {
            return false;
        }

        const double HitDistance = FVector::DotProduct(Edge2, QVec) * InvDeterminant;
        if (HitDistance <= 0.0)
        {
            return false;
        }

        OutDistance = HitDistance;
        OutBarycentric = FVector(1.0 - U - V, U, V);
        return true;
    }
}

void FShineAIPaintMeshPicker::Reset()
{
    Triangles.Reset();
    Bounds = FBox(ForceInit);
}

void FShineAIPaintMeshPicker::Rebuild(const FShineAIPaintSession& Session)
{
    Reset();

    for (const FShineAIPaintTarget& Target : Session.GetTargets())
    {
        UObject* MeshAsset = Target.MeshAsset.Get();
        if (!MeshAsset)
        {
            continue;
        }

        if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset))
        {
            AppendStaticMesh(*StaticMesh);
        }
        else if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset))
        {
            AppendSkeletalMesh(*SkeletalMesh);
        }
    }
}

void FShineAIPaintMeshPicker::AppendStaticMesh(const UStaticMesh& Mesh)
{
    const FStaticMeshRenderData* RenderData = Mesh.GetRenderData();
    if (!RenderData || RenderData->LODResources.Num() == 0)
    {
        return;
    }

    const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
    const FStaticMeshVertexBuffer& VertexBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;
    const FPositionVertexBuffer& PositionBuffer = LOD.VertexBuffers.PositionVertexBuffer;

    if (VertexBuffer.GetNumTexCoords() == 0 || PositionBuffer.GetNumVertices() == 0)
    {
        return;
    }

    const FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
    const int32 IndexCount = Indices.Num();
    const uint32 VertexCount = PositionBuffer.GetNumVertices();

    for (int32 Index = 0; Index + 2 < IndexCount; Index += 3)
    {
        const uint32 I0 = Indices[Index];
        const uint32 I1 = Indices[Index + 1];
        const uint32 I2 = Indices[Index + 2];

        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
        {
            continue;
        }

        FTriangle Triangle;
        Triangle.V0 = FVector(PositionBuffer.VertexPosition(I0));
        Triangle.V1 = FVector(PositionBuffer.VertexPosition(I1));
        Triangle.V2 = FVector(PositionBuffer.VertexPosition(I2));
        Triangle.UV0 = FVector2D(VertexBuffer.GetVertexUV(I0, 0));
        Triangle.UV1 = FVector2D(VertexBuffer.GetVertexUV(I1, 0));
        Triangle.UV2 = FVector2D(VertexBuffer.GetVertexUV(I2, 0));

        Bounds += Triangle.V0;
        Bounds += Triangle.V1;
        Bounds += Triangle.V2;
        Triangles.Add(Triangle);
    }
}

void FShineAIPaintMeshPicker::AppendSkeletalMesh(const USkeletalMesh& Mesh)
{
    const FSkeletalMeshRenderData* RenderData = Mesh.GetResourceForRendering();
    if (!RenderData || RenderData->LODRenderData.Num() == 0)
    {
        return;
    }

    const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[0];
    const FStaticMeshVertexBuffer& VertexBuffer = LOD.StaticVertexBuffers.StaticMeshVertexBuffer;
    const FPositionVertexBuffer& PositionBuffer = LOD.StaticVertexBuffers.PositionVertexBuffer;

    if (VertexBuffer.GetNumTexCoords() == 0 || PositionBuffer.GetNumVertices() == 0)
    {
        return;
    }

    TArray<uint32> Indices;
    LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);

    const int32 IndexCount = Indices.Num();
    const uint32 VertexCount = PositionBuffer.GetNumVertices();

    for (int32 Index = 0; Index + 2 < IndexCount; Index += 3)
    {
        const uint32 I0 = Indices[Index];
        const uint32 I1 = Indices[Index + 1];
        const uint32 I2 = Indices[Index + 2];

        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
        {
            continue;
        }

        FTriangle Triangle;
        Triangle.V0 = FVector(PositionBuffer.VertexPosition(I0));
        Triangle.V1 = FVector(PositionBuffer.VertexPosition(I1));
        Triangle.V2 = FVector(PositionBuffer.VertexPosition(I2));
        Triangle.UV0 = FVector2D(VertexBuffer.GetVertexUV(I0, 0));
        Triangle.UV1 = FVector2D(VertexBuffer.GetVertexUV(I1, 0));
        Triangle.UV2 = FVector2D(VertexBuffer.GetVertexUV(I2, 0));

        Bounds += Triangle.V0;
        Bounds += Triangle.V1;
        Bounds += Triangle.V2;
        Triangles.Add(Triangle);
    }
}

bool FShineAIPaintMeshPicker::Raycast(const FVector& RayOrigin, const FVector& RayDirection, FShineAIPaintHit& OutHit) const
{
    OutHit = FShineAIPaintHit();

    if (Triangles.Num() == 0 || !RayHitsBox(Bounds, RayOrigin, RayDirection))
    {
        return false;
    }

    double NearestDistance = TNumericLimits<double>::Max();
    FVector NearestBarycentric = FVector::ZeroVector;
    const FTriangle* NearestTriangle = nullptr;

    for (const FTriangle& Triangle : Triangles)
    {
        double Distance = 0.0;
        FVector Barycentric = FVector::ZeroVector;
        if (!IntersectTriangle(RayOrigin, RayDirection, Triangle.V0, Triangle.V1, Triangle.V2, Distance, Barycentric))
        {
            continue;
        }

        if (Distance < NearestDistance)
        {
            NearestDistance = Distance;
            NearestBarycentric = Barycentric;
            NearestTriangle = &Triangle;
        }
    }

    if (!NearestTriangle)
    {
        return false;
    }

    OutHit.bHit = true;
    OutHit.Distance = NearestDistance;
    OutHit.UV = NearestTriangle->UV0 * NearestBarycentric.X
        + NearestTriangle->UV1 * NearestBarycentric.Y
        + NearestTriangle->UV2 * NearestBarycentric.Z;
    return true;
}
