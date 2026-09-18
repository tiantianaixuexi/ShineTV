#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"

class FShineAIPaintSession;
class UStaticMesh;
class USkeletalMesh;

/** 一次 3D 涂画命中结果。 */
struct FShineAIPaintHit
{
    bool bHit = false;

    /** 命中点的 UV（可能落在 0..1 之外，说明在 UV 边界外，需要靠平铺补笔迹）。 */
    FVector2D UV = FVector2D::ZeroVector;

    /** 沿射线的距离。 */
    double Distance = 0.0;
};

/**
 * 把指认到的网格体烘成 CPU 三角形（LOD0），用射线求交拿回命中点的 UV。
 *
 * 预览场景里的网格体都摆在原点、无旋转缩放，所以"网格局部空间 == 世界空间"，
 * 三角形不用再乘组件变换。
 */
class SHINEAIPAINT_API FShineAIPaintMeshPicker
{
public:
    /** 按当前会话的目标网格重建三角形数据（贴图尺寸/指认网格变化时调用）。 */
    void Rebuild(const FShineAIPaintSession& Session);

    void Reset();

    bool IsEmpty() const { return Triangles.Num() == 0; }
    int32 GetTriangleCount() const { return Triangles.Num(); }

    /** 取最近的命中。 */
    bool Raycast(const FVector& RayOrigin, const FVector& RayDirection, FShineAIPaintHit& OutHit) const;

private:
    struct FTriangle
    {
        FVector V0;
        FVector V1;
        FVector V2;
        FVector2D UV0;
        FVector2D UV1;
        FVector2D UV2;
    };

    void AppendStaticMesh(const UStaticMesh& Mesh);
    void AppendSkeletalMesh(const USkeletalMesh& Mesh);

    TArray<FTriangle> Triangles;
    FBox Bounds = FBox(ForceInit);
};
