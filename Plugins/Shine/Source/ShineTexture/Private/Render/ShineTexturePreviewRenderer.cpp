#include "Render/ShineTexturePreviewRenderer.h"

#include "Asset/ShineTextureAsset.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "GraphEditAction.h"
#include "Graph/ShineTextureGraph.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "Node/Presets/Math/ShineTextureAbsNode.h"
#include "Node/Presets/Math/ShineTextureAddNode.h"
#include "Node/Presets/Math/ShineTextureAndNode.h"
#include "Node/Presets/Math/ShineTextureAppendVector2Node.h"
#include "Node/Presets/Math/ShineTextureAppendVector3Node.h"
#include "Node/Presets/Math/ShineTextureAppendVector4Node.h"
#include "Node/Presets/ShineTextureBlendNode.h"
#include "Node/Presets/ShineTextureBlurNode.h"
#include "Node/Presets/ShineTextureChannelPackNode.h"
#include "Node/Presets/Math/ShineTextureClampNode.h"
#include "Node/Presets/Math/ShineTextureDivideNode.h"
#include "Node/Presets/Math/ShineTextureEqualNode.h"
#include "Node/Presets/ShineTextureFibers1Node.h"
#include "Node/Presets/Math/ShineTextureBreakOutNode.h"
#include "Node/Presets/Math/ShineTextureFloorNode.h"
#include "Node/Presets/Math/ShineTextureFracNode.h"
#include "Node/Presets/Math/ShineTextureGreaterNode.h"
#include "Node/Presets/Math/ShineTextureLessNode.h"
#include "Node/Presets/Math/ShineTextureLevelsNode.h"
#include "Node/Presets/Math/ShineTextureLerpNode.h"
#include "Node/Presets/Math/ShineTextureMaskNode.h"
#include "Node/Presets/Math/ShineTextureMaxNode.h"
#include "Node/Presets/Math/ShineTextureMinNode.h"
#include "Node/Presets/Math/ShineTextureModuloNode.h"
#include "Node/Presets/Math/ShineTextureMultiplyNode.h"
#include "Node/Presets/ShineTextureNoiseNode.h"
#include "Node/Presets/ShineTexturePerlinNoiseNode.h"
#include "Node/Presets/Math/ShineTextureNotNode.h"
#include "Node/Presets/ShineTextureNormalBlendNode.h"
#include "Node/Presets/Math/ShineTextureOneMinusNode.h"
#include "Node/Presets/ShineTextureOutputNode.h"
#include "Node/Presets/Math/ShineTextureOrNode.h"
#include "Node/Presets/ShineTexturePreviewNode.h"
#include "Node/Presets/Math/ShineTextureScalarConstantNode.h"
#include "Node/Presets/Math/ShineTextureSelectNode.h"
#include "Node/Presets/ShineTextureSolidColorNode.h"
#include "Node/Presets/ShineTextureTextureInputNode.h"
#include "Node/Presets/Math/ShineTextureSubtractNode.h"
#include "Node/Presets/Math/ShineTextureStepNode.h"
#include "Node/Presets/Math/ShineTextureToFloatNode.h"
#include "Node/Presets/Math/ShineTextureToIntNode.h"
#include "Node/Presets/Math/ShineTextureSwizzle2Node.h"
#include "Node/Presets/Math/ShineTextureSwizzle4Node.h"
#include "Node/Presets/Math/ShineTextureSqrtNode.h"
#include "Node/Presets/ShineTextureTransform2DNode.h"
#include "Node/Presets/ShineTextureUvNode.h"
#include "Node/Presets/Math/ShineTextureVector2ConstantNode.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

namespace
{
    static constexpr EPixelFormat ShineTextureIntermediatePreviewFormat = PF_FloatRGBA;
    static constexpr ETextureRenderTargetFormat ShineTextureIntermediatePreviewRTFormat = RTF_RGBA16f;

    enum class EShineTextureGpuNodeType : uint32
    {
        SolidColor = 0,
        TextureInput = 1,
        Noise = 2,
        Fibers1 = 3,
        UV = 4,
        ScalarConstant = 5,
        Vector2Constant = 6,
        Swizzle2 = 7,
        AppendVector2 = 8,
        Transform2D = 9,
        Blend = 10,
        Multiply = 11,
        Add = 12,
        Subtract = 13,
        Divide = 14,
        Min = 15,
        Max = 16,
        Abs = 17,
        Sqrt = 18,
        OneMinus = 19,
        Floor = 20,
        Frac = 21,
        Clamp = 22,
        Levels = 23,
        Blur = 24,
        NormalBlend = 25,
        ChannelPack = 26,
        Greater = 27,
        Less = 28,
        Equal = 29,
        Step = 30,
        And = 31,
        Or = 32,
        Not = 33,
        ToFloat = 34,
        ToInt = 35,
        Swizzle4 = 36,
        AppendVector4 = 37,
        Lerp = 38,
        Modulo = 39,
        Mask = 40,
        BreakOut = 41,
        Select = 42,
        Preview = 43,
        Output = 44,
        PerlinNoise = 45,
        AppendVector3 = 46,
    };

    enum class EShineTextureDefaultInputTexture : uint8
    {
        Black = 0,
        White,
        Normal,
    };

    using FConfigureGpuNodeFn = void(*)(UShineTextureGraphNodeBase* Node, struct FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies);

    struct FShineTextureGpuNodeRegistration
    {
        UClass* NodeClass = nullptr;
        EShineTextureGpuNodeType Type = EShineTextureGpuNodeType::SolidColor;
        const TCHAR* ShaderFile = TEXT("");
        const TCHAR* Param0Semantics = TEXT("unused");
        const TCHAR* Param1Semantics = TEXT("unused");
        EShineTextureDefaultInputTexture DefaultInputs[4] =
        {
            EShineTextureDefaultInputTexture::Black,
            EShineTextureDefaultInputTexture::Black,
            EShineTextureDefaultInputTexture::Black,
            EShineTextureDefaultInputTexture::Black,
        };
        FConfigureGpuNodeFn Configure = nullptr;
    };

    UShineTextureGraphNodeBase* GetLinkedSourceNode(UShineTextureGraphNodeBase* Node, const TCHAR* InputPinName, int32* OutOutputComponent = nullptr);

    struct FShineTextureCompiledNode
    {
        EShineTextureGpuNodeType Type = EShineTextureGpuNodeType::SolidColor;
        FGuid NodeGuid;
        FGuid InputNodeGuids[4];
        int32 InputIndices[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
        int32 InputOutputComponents[4] = { 4, 4, 4, 4 };
        EShineTextureDefaultInputTexture DefaultInputs[4] =
        {
            EShineTextureDefaultInputTexture::Black,
            EShineTextureDefaultInputTexture::Black,
            EShineTextureDefaultInputTexture::Black,
            EShineTextureDefaultInputTexture::Black,
        };
        FVector4f Param0 = FVector4f::Zero();
        FVector4f Param1 = FVector4f::Zero();
        TObjectPtr<UTexture> ExternalInputTexture = nullptr;
    };

    struct FShineTextureCachedNodeEntry
    {
        uint32 LastHash = 0;
        TStrongObjectPtr<UTextureRenderTarget2D> Texture;
    };

    struct FShineTextureTargetCacheEntry
    {
        uint32 PipelineHash = 0;
        FIntPoint Resolution = FIntPoint::ZeroValue;
        TMap<FGuid, FShineTextureCachedNodeEntry> NodeCache;
    };

    struct FShineTextureAssetCacheEntry
    {
        TMap<FGuid, FShineTextureTargetCacheEntry> TargetCaches;
    };

    struct FShineTextureRenderTargetJob
    {
        TArray<FShineTextureCompiledNode> Nodes;
        TArray<FTextureRenderTargetResource*> NodeResources;
        TArray<FTextureRHIRef> ExternalInputTextures;
        TArray<uint8> DirtyNodeFlags;
        FIntPoint Resolution = FIntPoint(256, 256);
        FTextureRenderTargetResource* Resource = nullptr;
        FTextureRenderTargetResource* DisplayResource = nullptr;
        uint8 PreviewDisplayMode = 0;
        FString DebugName;
    };

    TMap<TWeakObjectPtr<UShineTextureAsset>, FShineTextureAssetCacheEntry> GShineTexturePreviewCaches;

    class FShineTextureNodePreviewCS : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FShineTextureNodePreviewCS);
        SHADER_USE_PARAMETER_STRUCT(FShineTextureNodePreviewCS, FGlobalShader);

    public:
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(uint32, NodeType)
            SHADER_PARAMETER(FIntVector4, InputValid)
            SHADER_PARAMETER(FVector4f, Param0)
            SHADER_PARAMETER(FVector4f, Param1)
            SHADER_PARAMETER(FUintVector2, Resolution)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTextureA)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTextureB)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTextureC)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTextureD)
            SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
        END_SHADER_PARAMETER_STRUCT()
    };

    IMPLEMENT_GLOBAL_SHADER(FShineTextureNodePreviewCS, "/Plugin/Shine/Source/ShineTexture/ShineTexturePreviewCS.usf", "MainCS", SF_Compute);

    class FShineTexturePreviewDisplayCS : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FShineTexturePreviewDisplayCS);
        SHADER_USE_PARAMETER_STRUCT(FShineTexturePreviewDisplayCS, FGlobalShader);

    public:
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FUintVector2, Resolution)
            SHADER_PARAMETER(uint32, DisplayMode)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
        END_SHADER_PARAMETER_STRUCT()
    };

    IMPLEMENT_GLOBAL_SHADER(FShineTexturePreviewDisplayCS, "/Plugin/Shine/Source/ShineTexture/ShineTexturePreviewDisplayCS.usf", "MainCS", SF_Compute);

    void ConfigureSolidColorNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureSolidColorNode* SolidColorNode = CastChecked<UShineTextureSolidColorNode>(Node);
        const FLinearColor Color = SolidColorNode->GetColorValue();
        OutCompiledNode.Param0 = FVector4f(Color.R, Color.G, Color.B, Color.A);
    }

    void ConfigureTextureInputNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureTextureInputNode* TextureInputNode = CastChecked<UShineTextureTextureInputNode>(Node);
        OutCompiledNode.ExternalInputTexture = TextureInputNode->GetSourceTexture();
    }

    void ConfigureNoiseNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureNoiseNode* NoiseNode = CastChecked<UShineTextureNoiseNode>(Node);
        OutCompiledNode.Param0 = FVector4f(NoiseNode->GetScale(), static_cast<float>(NoiseNode->GetOctaves()), 0.0f, 0.0f);
    }

    void ConfigureFibers1Node(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureFibers1Node* Fibers1Node = CastChecked<UShineTextureFibers1Node>(Node);
        OutCompiledNode.Param0 = FVector4f(
            static_cast<float>(Fibers1Node->GetTiling()),
            Fibers1Node->GetNonSquareExpansion() ? 1.0f : 0.0f,
            0.0f,
            0.0f);
    }

    void ConfigurePerlinNoiseNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTexturePerlinNoiseNode* PerlinNoiseNode = CastChecked<UShineTexturePerlinNoiseNode>(Node);
        const FVector2D TileOffset = PerlinNoiseNode->GetTileOffset();
        OutCompiledNode.Param0 = FVector4f(
            static_cast<float>(PerlinNoiseNode->GetScale()),
            PerlinNoiseNode->GetDisorder(),
            PerlinNoiseNode->GetDisorderSpeed(),
            PerlinNoiseNode->GetNonSquareExpansion() ? 1.0f : 0.0f);
        OutCompiledNode.Param1 = FVector4f(TileOffset.X, TileOffset.Y, 0.0f, 0.0f);
    }

    void ConfigureUvNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
    }

    void ConfigureScalarConstantNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureScalarConstantNode* ScalarConstantNode = CastChecked<UShineTextureScalarConstantNode>(Node);
        OutCompiledNode.Param0 = FVector4f(ScalarConstantNode->GetValue(), 0.0f, 0.0f, 0.0f);
    }

    void ConfigureVector2ConstantNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureVector2ConstantNode* Vector2ConstantNode = CastChecked<UShineTextureVector2ConstantNode>(Node);
        const FVector2D Value = Vector2ConstantNode->GetValue();
        OutCompiledNode.Param0 = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
    }

    void ConfigureSwizzle2Node(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureSwizzle2Node* Swizzle2Node = CastChecked<UShineTextureSwizzle2Node>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        OutCompiledNode.Param0 = FVector4f(static_cast<float>(Swizzle2Node->GetFirstComponent()), static_cast<float>(Swizzle2Node->GetSecondComponent()), 0.0f, 0.0f);
    }

    void ConfigureAppendVector2Node(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("X"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("Y"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureAppendVector3Node(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("X"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("Y"), &OutCompiledNode.InputOutputComponents[1]);
        OutDependencies[2] = GetLinkedSourceNode(Node, TEXT("Z"), &OutCompiledNode.InputOutputComponents[2]);
    }

    void ConfigureAppendVector4Node(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("X"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("Y"), &OutCompiledNode.InputOutputComponents[1]);
        OutDependencies[2] = GetLinkedSourceNode(Node, TEXT("Z"), &OutCompiledNode.InputOutputComponents[2]);
        OutDependencies[3] = GetLinkedSourceNode(Node, TEXT("W"), &OutCompiledNode.InputOutputComponents[3]);
    }

    void ConfigureTransform2DNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureTransform2DNode* TransformNode = CastChecked<UShineTextureTransform2DNode>(Node);
        const FVector2D Offset = TransformNode->GetOffset();
        const FVector2D Scale = TransformNode->GetScale();
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        OutCompiledNode.Param0 = FVector4f(Offset.X, Offset.Y, TransformNode->GetRotationDegrees(), TransformNode->GetTilingEnabled() ? 1.0f : 0.0f);
        OutCompiledNode.Param1 = FVector4f(Scale.X, Scale.Y, 0.0f, 0.0f);
    }

    void ConfigureBlendNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureBlendNode* BlendNode = CastChecked<UShineTextureBlendNode>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
        OutCompiledNode.Param0 = FVector4f(BlendNode->GetBlendFactor(), 0.0f, 0.0f, 0.0f);
    }

    void ConfigureMultiplyNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureAddNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureSubtractNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureDivideNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureMinNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureMaxNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureAbsNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureSqrtNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureOneMinusNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureFloorNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureFracNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureClampNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureClampNode* ClampNode = CastChecked<UShineTextureClampNode>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        OutCompiledNode.Param0 = FVector4f(ClampNode->GetMinValue(), ClampNode->GetMaxValue(), 0.0f, 0.0f);
    }

    void ConfigureLevelsNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureLevelsNode* LevelsNode = CastChecked<UShineTextureLevelsNode>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        OutCompiledNode.Param0 = FVector4f(LevelsNode->GetInputLow(), LevelsNode->GetInputHigh(), LevelsNode->GetGamma(), LevelsNode->GetOutputLow());
        OutCompiledNode.Param1 = FVector4f(LevelsNode->GetOutputHigh(), 0.0f, 0.0f, 0.0f);
    }

    void ConfigureBlurNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureBlurNode* BlurNode = CastChecked<UShineTextureBlurNode>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        OutCompiledNode.Param0 = FVector4f(BlurNode->GetRadius(), 0.0f, 0.0f, 0.0f);
    }

    void ConfigureNormalBlendNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureNormalBlendNode* NormalBlendNode = CastChecked<UShineTextureNormalBlendNode>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Base"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("Detail"), &OutCompiledNode.InputOutputComponents[1]);
        OutCompiledNode.Param0 = FVector4f(NormalBlendNode->GetDetailStrength(), 0.0f, 0.0f, 0.0f);
    }

    void ConfigureChannelPackNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureChannelPackNode* ChannelPackNode = CastChecked<UShineTextureChannelPackNode>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("R"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("G"), &OutCompiledNode.InputOutputComponents[1]);
        OutDependencies[2] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[2]);
        OutDependencies[3] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[3]);
        OutCompiledNode.Param0 = FVector4f(ChannelPackNode->GetDefaultAlpha(), 0.0f, 0.0f, 0.0f);
    }

    void ConfigureGreaterNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureLessNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureEqualNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
        OutCompiledNode.Param0 = FVector4f(0.0001f, 0.0f, 0.0f, 0.0f);
    }

    void ConfigureStepNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Edge"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("Value"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureAndNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureOrNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureNotNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureToFloatNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureToIntNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureSwizzle4Node(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureSwizzle4Node* Swizzle4Node = CastChecked<UShineTextureSwizzle4Node>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        OutCompiledNode.Param0 = FVector4f(
            static_cast<float>(Swizzle4Node->GetXComponent()),
            static_cast<float>(Swizzle4Node->GetYComponent()),
            static_cast<float>(Swizzle4Node->GetZComponent()),
            static_cast<float>(Swizzle4Node->GetWComponent()));
    }

    void ConfigureLerpNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
        OutDependencies[2] = GetLinkedSourceNode(Node, TEXT("Alpha"), &OutCompiledNode.InputOutputComponents[2]);
    }

    void ConfigureModuloNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("A"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("B"), &OutCompiledNode.InputOutputComponents[1]);
    }

    void ConfigureMaskNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        const UShineTextureMaskNode* MaskNode = CastChecked<UShineTextureMaskNode>(Node);
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        OutCompiledNode.Param0 = FVector4f(
            MaskNode->GetMaskX() ? 1.0f : 0.0f,
            MaskNode->GetMaskY() ? 1.0f : 0.0f,
            MaskNode->GetMaskZ() ? 1.0f : 0.0f,
            MaskNode->GetMaskW() ? 1.0f : 0.0f);
    }

    void ConfigureBreakOutNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
        // Negative component index tells the shader to preserve the full input vector.
        OutCompiledNode.Param1 = FVector4f(-1.0f, 0.0f, 0.0f, 0.0f);
    }

    void ConfigureSelectNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Condition"), &OutCompiledNode.InputOutputComponents[0]);
        OutDependencies[1] = GetLinkedSourceNode(Node, TEXT("True"), &OutCompiledNode.InputOutputComponents[1]);
        OutDependencies[2] = GetLinkedSourceNode(Node, TEXT("False"), &OutCompiledNode.InputOutputComponents[2]);
    }

    void ConfigurePreviewNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Input"), &OutCompiledNode.InputOutputComponents[0]);
    }

    void ConfigureOutputNode(UShineTextureGraphNodeBase* Node, FShineTextureCompiledNode& OutCompiledNode, TArray<UShineTextureGraphNodeBase*>& OutDependencies)
    {
        OutDependencies[0] = GetLinkedSourceNode(Node, TEXT("Surface"), &OutCompiledNode.InputOutputComponents[0]);
    }

    const TArray<FShineTextureGpuNodeRegistration>& GetGpuNodeRegistrations()
    {
        // Central GPU node registry for UObject -> GPU enum -> shader file mapping.
        // Keep this table as the source of truth when adding nodes.
        // Every row must stay in sync with:
        // - the node parameter packing done by Configure*
        // - the include + branch in Plugins/ShineTexture/Shaders/ShineTexturePreviewCS.usf
        // - the dedicated shader file listed in ShaderFile
        static const TArray<FShineTextureGpuNodeRegistration> Registrations =
        {
            { UShineTextureSolidColorNode::StaticClass(), EShineTextureGpuNodeType::SolidColor, TEXT("ShineTextureNodeSolidColor.ush"), TEXT("x=R, y=G, z=B, w=A"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureSolidColorNode },
            { UShineTextureTextureInputNode::StaticClass(), EShineTextureGpuNodeType::TextureInput, TEXT("ShineTextureNodeTextureInput.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureTextureInputNode },
            { UShineTextureNoiseNode::StaticClass(), EShineTextureGpuNodeType::Noise, TEXT("ShineTextureNodeNoise.ush"), TEXT("x=Scale, y=Octaves"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureNoiseNode },
            { UShineTextureFibers1Node::StaticClass(), EShineTextureGpuNodeType::Fibers1, TEXT("ShineTextureNodeFibers1.ush"), TEXT("x=Tiling, y=NonSquareExpansion"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureFibers1Node },
            { UShineTexturePerlinNoiseNode::StaticClass(), EShineTextureGpuNodeType::PerlinNoise, TEXT("ShineTextureNodePerlinNoise.ush"), TEXT("x=Scale, y=Disorder, z=DisorderSpeed, w=NonSquareExpansion"), TEXT("x=TileOffsetX, y=TileOffsetY"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigurePerlinNoiseNode },
            { UShineTextureUvNode::StaticClass(), EShineTextureGpuNodeType::UV, TEXT("ShineTextureNodeUv.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureUvNode },
            { UShineTextureScalarConstantNode::StaticClass(), EShineTextureGpuNodeType::ScalarConstant, TEXT("ShineTextureNodeScalarConstant.ush"), TEXT("x=Value"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureScalarConstantNode },
            { UShineTextureVector2ConstantNode::StaticClass(), EShineTextureGpuNodeType::Vector2Constant, TEXT("ShineTextureNodeVector2Constant.ush"), TEXT("x=ValueX, y=ValueY"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureVector2ConstantNode },
            { UShineTextureSwizzle2Node::StaticClass(), EShineTextureGpuNodeType::Swizzle2, TEXT("ShineTextureNodeSwizzle2.ush"), TEXT("x=FirstComponent, y=SecondComponent"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureSwizzle2Node },
            { UShineTextureAppendVector2Node::StaticClass(), EShineTextureGpuNodeType::AppendVector2, TEXT("ShineTextureNodeAppendVector2.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureAppendVector2Node },
            { UShineTextureAppendVector3Node::StaticClass(), EShineTextureGpuNodeType::AppendVector3, TEXT("ShineTextureNodeAppendVector3.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureAppendVector3Node },
            { UShineTextureAppendVector4Node::StaticClass(), EShineTextureGpuNodeType::AppendVector4, TEXT("ShineTextureNodeAppendVector4.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureAppendVector4Node },
            { UShineTextureTransform2DNode::StaticClass(), EShineTextureGpuNodeType::Transform2D, TEXT("ShineTextureNodeTransform2D.ush"), TEXT("x=OffsetX, y=OffsetY, z=RotationDegrees, w=TilingEnabled"), TEXT("x=ScaleX, y=ScaleY"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureTransform2DNode },
            { UShineTextureBlendNode::StaticClass(), EShineTextureGpuNodeType::Blend, TEXT("ShineTextureNodeBlend.ush"), TEXT("x=BlendFactor"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureBlendNode },
            { UShineTextureMultiplyNode::StaticClass(), EShineTextureGpuNodeType::Multiply, TEXT("ShineTextureNodeMultiply.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::White, EShineTextureDefaultInputTexture::White, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureMultiplyNode },
            { UShineTextureAddNode::StaticClass(), EShineTextureGpuNodeType::Add, TEXT("ShineTextureNodeAdd.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureAddNode },
            { UShineTextureSubtractNode::StaticClass(), EShineTextureGpuNodeType::Subtract, TEXT("ShineTextureNodeSubtract.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureSubtractNode },
            { UShineTextureDivideNode::StaticClass(), EShineTextureGpuNodeType::Divide, TEXT("ShineTextureNodeDivide.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::White, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureDivideNode },
            { UShineTextureMinNode::StaticClass(), EShineTextureGpuNodeType::Min, TEXT("ShineTextureNodeMin.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureMinNode },
            { UShineTextureMaxNode::StaticClass(), EShineTextureGpuNodeType::Max, TEXT("ShineTextureNodeMax.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureMaxNode },
            { UShineTextureAbsNode::StaticClass(), EShineTextureGpuNodeType::Abs, TEXT("ShineTextureNodeAbs.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureAbsNode },
            { UShineTextureSqrtNode::StaticClass(), EShineTextureGpuNodeType::Sqrt, TEXT("ShineTextureNodeSqrt.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureSqrtNode },
            { UShineTextureOneMinusNode::StaticClass(), EShineTextureGpuNodeType::OneMinus, TEXT("ShineTextureNodeOneMinus.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::White, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureOneMinusNode },
            { UShineTextureFloorNode::StaticClass(), EShineTextureGpuNodeType::Floor, TEXT("ShineTextureNodeFloor.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureFloorNode },
            { UShineTextureFracNode::StaticClass(), EShineTextureGpuNodeType::Frac, TEXT("ShineTextureNodeFrac.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureFracNode },
            { UShineTextureClampNode::StaticClass(), EShineTextureGpuNodeType::Clamp, TEXT("ShineTextureNodeClamp.ush"), TEXT("x=MinValue, y=MaxValue"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureClampNode },
            { UShineTextureLevelsNode::StaticClass(), EShineTextureGpuNodeType::Levels, TEXT("ShineTextureNodeLevels.ush"), TEXT("x=InputLow, y=InputHigh, z=Gamma, w=OutputLow"), TEXT("x=OutputHigh"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureLevelsNode },
            { UShineTextureBlurNode::StaticClass(), EShineTextureGpuNodeType::Blur, TEXT("ShineTextureNodeBlur.ush"), TEXT("x=Radius"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureBlurNode },
            { UShineTextureNormalBlendNode::StaticClass(), EShineTextureGpuNodeType::NormalBlend, TEXT("ShineTextureNodeNormalBlend.ush"), TEXT("x=DetailStrength"), TEXT("unused"), { EShineTextureDefaultInputTexture::Normal, EShineTextureDefaultInputTexture::Normal, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureNormalBlendNode },
            { UShineTextureChannelPackNode::StaticClass(), EShineTextureGpuNodeType::ChannelPack, TEXT("ShineTextureNodeChannelPack.ush"), TEXT("x=DefaultAlpha"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureChannelPackNode },
            { UShineTextureGreaterNode::StaticClass(), EShineTextureGpuNodeType::Greater, TEXT("ShineTextureNodeGreater.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureGreaterNode },
            { UShineTextureLessNode::StaticClass(), EShineTextureGpuNodeType::Less, TEXT("ShineTextureNodeLess.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureLessNode },
            { UShineTextureEqualNode::StaticClass(), EShineTextureGpuNodeType::Equal, TEXT("ShineTextureNodeEqual.ush"), TEXT("x=Epsilon"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureEqualNode },
            { UShineTextureStepNode::StaticClass(), EShineTextureGpuNodeType::Step, TEXT("ShineTextureNodeStep.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureStepNode },
            { UShineTextureAndNode::StaticClass(), EShineTextureGpuNodeType::And, TEXT("ShineTextureNodeAnd.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureAndNode },
            { UShineTextureOrNode::StaticClass(), EShineTextureGpuNodeType::Or, TEXT("ShineTextureNodeOr.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureOrNode },
            { UShineTextureNotNode::StaticClass(), EShineTextureGpuNodeType::Not, TEXT("ShineTextureNodeNot.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureNotNode },
            { UShineTextureToFloatNode::StaticClass(), EShineTextureGpuNodeType::ToFloat, TEXT("ShineTextureNodeToFloat.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureToFloatNode },
            { UShineTextureToIntNode::StaticClass(), EShineTextureGpuNodeType::ToInt, TEXT("ShineTextureNodeToInt.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureToIntNode },
            { UShineTextureSwizzle4Node::StaticClass(), EShineTextureGpuNodeType::Swizzle4, TEXT("ShineTextureNodeSwizzle4.ush"), TEXT("x=XComponent, y=YComponent, z=ZComponent, w=WComponent"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureSwizzle4Node },
            { UShineTextureLerpNode::StaticClass(), EShineTextureGpuNodeType::Lerp, TEXT("ShineTextureNodeLerp.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureLerpNode },
            { UShineTextureModuloNode::StaticClass(), EShineTextureGpuNodeType::Modulo, TEXT("ShineTextureNodeModulo.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::White, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureModuloNode },
            { UShineTextureMaskNode::StaticClass(), EShineTextureGpuNodeType::Mask, TEXT("ShineTextureNodeMask.ush"), TEXT("x=MaskX, y=MaskY, z=MaskZ, w=MaskW"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureMaskNode },
            { UShineTextureBreakOutNode::StaticClass(), EShineTextureGpuNodeType::BreakOut, TEXT("ShineTextureNodeBreakOut.ush"), TEXT("unused"), TEXT("x=OutputComponentIndex"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureBreakOutNode },
            { UShineTextureSelectNode::StaticClass(), EShineTextureGpuNodeType::Select, TEXT("ShineTextureNodeSelect.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureSelectNode },
            { UShineTexturePreviewNode::StaticClass(), EShineTextureGpuNodeType::Preview, TEXT("ShineTextureNodePreview.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigurePreviewNode },
            { UShineTextureOutputNode::StaticClass(), EShineTextureGpuNodeType::Output, TEXT("ShineTextureNodeOutput.ush"), TEXT("unused"), TEXT("unused"), { EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black, EShineTextureDefaultInputTexture::Black }, &ConfigureOutputNode },
        };

        return Registrations;
    }

    uint32 HashPreviewShaderSources()
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ShineTexture"));
        if (!Plugin.IsValid())
        {
            return 0;
        }

        const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
        if (!IFileManager::Get().DirectoryExists(*ShaderDir))
        {
            return 0;
        }

        uint32 Hash = 0;
        TArray<FString> ShaderFiles;
        IFileManager::Get().FindFilesRecursive(ShaderFiles, *ShaderDir, TEXT("*"), true, false);
        for (const FString& ShaderFile : ShaderFiles)
        {
            const FString Extension = FPaths::GetExtension(ShaderFile);
            if (Extension != TEXT("ush") && Extension != TEXT("usf"))
            {
                continue;
            }

            const FString FullPath = FPaths::Combine(ShaderDir, ShaderFile);
            const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*FullPath);
            const int64 FileSize = IFileManager::Get().FileSize(*FullPath);
            Hash = HashCombine(Hash, GetTypeHash(Timestamp));
            Hash = HashCombine(Hash, GetTypeHash(FileSize));
            Hash = HashCombine(Hash, FCrc::Strihash_DEPRECATED(*ShaderFile));
        }

        return Hash;
    }

    uint32 HashGpuNodeRegistry()
    {
        uint32 Hash = 0;
        for (const FShineTextureGpuNodeRegistration& Registration : GetGpuNodeRegistrations())
        {
            Hash = HashCombine(Hash, GetTypeHash(static_cast<uint32>(Registration.Type)));
            Hash = HashCombine(Hash, FCrc::Strihash_DEPRECATED(Registration.ShaderFile));
            Hash = HashCombine(Hash, FCrc::Strihash_DEPRECATED(Registration.Param0Semantics));
            Hash = HashCombine(Hash, FCrc::Strihash_DEPRECATED(Registration.Param1Semantics));

            for (const EShineTextureDefaultInputTexture DefaultInput : Registration.DefaultInputs)
            {
                Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(DefaultInput)));
            }
        }

        return Hash;
    }

    uint32 GetPreviewPipelineHash()
    {
        uint32 Hash = HashCombine(HashPreviewShaderSources(), HashGpuNodeRegistry());
        Hash = HashCombine(Hash, GetTypeHash(ShineTextureIntermediatePreviewRTFormat));
        Hash = HashCombine(Hash, GetTypeHash(ShineTextureIntermediatePreviewFormat));
        return Hash;
    }

    const FShineTextureGpuNodeRegistration* FindGpuNodeRegistration(const UShineTextureGraphNodeBase* Node)
    {
        if (!Node)
        {
            return nullptr;
        }

        for (const FShineTextureGpuNodeRegistration& Registration : GetGpuNodeRegistrations())
        {
            if (Registration.NodeClass && Node->IsA(Registration.NodeClass))
            {
                return &Registration;
            }
        }

        return nullptr;
    }

    FGuid EnsureNodeGuid(UShineTextureGraphNodeBase* Node)
    {
        if (!Node)
        {
            return FGuid();
        }

        if (!Node->NodeGuid.IsValid())
        {
            Node->CreateNewGuid();
        }

        return Node->NodeGuid;
    }

    uint32 HashCompiledNode(const FShineTextureCompiledNode& Node)
    {
        uint32 Hash = GetTypeHash(static_cast<uint32>(Node.Type));
        Hash = HashCombine(Hash, GetTypeHash(Node.NodeGuid));
        Hash = HashCombine(Hash, GetTypeHash(Node.Param0));
        Hash = HashCombine(Hash, GetTypeHash(Node.Param1));
        Hash = HashCombine(Hash, GetTypeHash(Node.ExternalInputTexture));

        for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(Node.InputNodeGuids); ++InputIndex)
        {
            Hash = HashCombine(Hash, GetTypeHash(Node.InputNodeGuids[InputIndex]));
            Hash = HashCombine(Hash, GetTypeHash(Node.InputOutputComponents[InputIndex]));
        }

        return Hash;
    }

    UTextureRenderTarget2D* CreateOrUpdateCachedRenderTarget(const UObject* Owner, TStrongObjectPtr<UTextureRenderTarget2D>& InOutTexture, FIntPoint Resolution)
    {
        const FIntPoint TextureSize(FMath::Max(16, Resolution.X), FMath::Max(16, Resolution.Y));
        UTextureRenderTarget2D* Texture = InOutTexture.Get();
        if (!Texture
            || Texture->SizeX != TextureSize.X
            || Texture->SizeY != TextureSize.Y
            || Texture->RenderTargetFormat != ShineTextureIntermediatePreviewRTFormat)
        {
            Texture = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
            Texture->NeverStream = true;
            Texture->Filter = TF_Bilinear;
            Texture->ClearColor = FLinearColor::Black;
            Texture->bAutoGenerateMips = false;
            Texture->bCanCreateUAV = true;
            Texture->RenderTargetFormat = ShineTextureIntermediatePreviewRTFormat;
            Texture->InitCustomFormat(TextureSize.X, TextureSize.Y, ShineTextureIntermediatePreviewFormat, false);
            Texture->UpdateResourceImmediate(true);
            InOutTexture.Reset(Texture);
        }

        return Texture;
    }

    void PurgeDeadPreviewCaches()
    {
        for (auto It = GShineTexturePreviewCaches.CreateIterator(); It; ++It)
        {
            if (!It.Key().IsValid())
            {
                It.RemoveCurrent();
            }
        }
    }

    void CollectChangedNodeGuids(const FEdGraphEditAction* InAction, TSet<FGuid>& OutNodeGuids)
    {
        if (!InAction)
        {
            return;
        }

        for (const UEdGraphNode* ChangedNode : InAction->Nodes)
        {
            if (const UShineTextureGraphNodeBase* TextureNode = Cast<UShineTextureGraphNodeBase>(ChangedNode))
            {
                if (TextureNode->NodeGuid.IsValid())
                {
                    OutNodeGuids.Add(TextureNode->NodeGuid);
                }
            }
        }
    }

    UShineTextureGraphNodeBase* GetLinkedSourceNode(UShineTextureGraphNodeBase* Node, const TCHAR* InputPinName, int32* OutOutputComponent)
    {
        if (!Node)
        {
            return nullptr;
        }

        if (UEdGraphPin* InputPin = Node->FindPin(InputPinName))
        {
            if (InputPin->LinkedTo.Num() > 0 && InputPin->LinkedTo[0])
            {
                if (OutOutputComponent)
                {
                    const FName LinkedPinName = InputPin->LinkedTo[0]->PinName;
                    if (LinkedPinName == TEXT("X"))
                    {
                        *OutOutputComponent = 0;
                    }
                    else if (LinkedPinName == TEXT("Y"))
                    {
                        *OutOutputComponent = 1;
                    }
                    else if (LinkedPinName == TEXT("Z"))
                    {
                        *OutOutputComponent = 2;
                    }
                    else if (LinkedPinName == TEXT("W"))
                    {
                        *OutOutputComponent = 3;
                    }
                    else
                    {
                        *OutOutputComponent = 4;
                    }
                }

                return Cast<UShineTextureGraphNodeBase>(InputPin->LinkedTo[0]->GetOwningNode());
            }
        }

        return nullptr;
    }

    bool CompileNodeRecursive(
        UShineTextureGraphNodeBase* Node,
        TArray<FShineTextureCompiledNode>& OutNodes,
        TMap<UShineTextureGraphNodeBase*, int32>& CompiledIndices,
        TSet<UShineTextureGraphNodeBase*>& Visiting)
    {
        if (!Node)
        {
            return false;
        }

        if (const int32* ExistingIndex = CompiledIndices.Find(Node))
        {
            return true;
        }

        if (Visiting.Contains(Node))
        {
            return false;
        }

        Visiting.Add(Node);

        FShineTextureCompiledNode CompiledNode;
        CompiledNode.NodeGuid = EnsureNodeGuid(Node);
        TArray<UShineTextureGraphNodeBase*> Dependencies;
        Dependencies.SetNum(4);

        const FShineTextureGpuNodeRegistration* Registration = FindGpuNodeRegistration(Node);
        if (!Registration || !Registration->Configure)
        {
            Visiting.Remove(Node);
            return false;
        }

        CompiledNode.Type = Registration->Type;
        for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(CompiledNode.DefaultInputs); ++InputIndex)
        {
            CompiledNode.DefaultInputs[InputIndex] = Registration->DefaultInputs[InputIndex];
        }
        Registration->Configure(Node, CompiledNode, Dependencies);

        for (int32 DependencyIndex = 0; DependencyIndex < Dependencies.Num(); ++DependencyIndex)
        {
            if (Dependencies[DependencyIndex])
            {
                if (!CompileNodeRecursive(Dependencies[DependencyIndex], OutNodes, CompiledIndices, Visiting))
                {
                    Visiting.Remove(Node);
                    return false;
                }

                if (const int32* FoundIndex = CompiledIndices.Find(Dependencies[DependencyIndex]))
                {
                    CompiledNode.InputIndices[DependencyIndex] = *FoundIndex;
                    CompiledNode.InputNodeGuids[DependencyIndex] = EnsureNodeGuid(Dependencies[DependencyIndex]);
                }
            }
        }

        const int32 NewIndex = OutNodes.Add(CompiledNode);
        CompiledIndices.Add(Node, NewIndex);
        Visiting.Remove(Node);
        return true;
    }

    UShineTextureOutputNode* FindOutputNode(UShineTextureGraph* Graph)
    {
        if (!Graph)
        {
            return nullptr;
        }

        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            if (UShineTextureOutputNode* OutputNode = Cast<UShineTextureOutputNode>(GraphNode))
            {
                return OutputNode;
            }
        }

        return nullptr;
    }

    bool BuildRenderJob(
        UShineTextureAsset* Asset,
        UShineTextureGraphNodeBase* TargetNode,
        FIntPoint Resolution,
        FTextureRenderTargetResource* Resource,
        const FString& DebugName,
        const TSet<FGuid>& ChangedNodeGuids,
        const bool bForceFullRefresh,
        FShineTextureRenderTargetJob& OutJob)
    {
        TMap<UShineTextureGraphNodeBase*, int32> CompiledIndices;
        TSet<UShineTextureGraphNodeBase*> Visiting;
        if (!CompileNodeRecursive(TargetNode, OutJob.Nodes, CompiledIndices, Visiting))
        {
            return false;
        }

        OutJob.Resolution = Resolution;
        OutJob.Resource = Resource;
        OutJob.DebugName = DebugName;
        OutJob.NodeResources.SetNumZeroed(OutJob.Nodes.Num());
        OutJob.ExternalInputTextures.SetNum(OutJob.Nodes.Num());
        OutJob.DirtyNodeFlags.Init(0, OutJob.Nodes.Num());

        if (OutJob.Nodes.IsEmpty() || Resource == nullptr || !Asset)
        {
            return false;
        }

        FShineTextureAssetCacheEntry& AssetCache = GShineTexturePreviewCaches.FindOrAdd(Asset);
        FShineTextureTargetCacheEntry& TargetCache = AssetCache.TargetCaches.FindOrAdd(EnsureNodeGuid(TargetNode));

        bool bRefreshAllNodes = bForceFullRefresh || DebugName.StartsWith(TEXT("ShineTexture.Preview."));
        const uint32 CurrentPipelineHash = GetPreviewPipelineHash();
        if (TargetCache.PipelineHash != CurrentPipelineHash)
        {
            TargetCache.NodeCache.Empty();
            TargetCache.PipelineHash = CurrentPipelineHash;
            bRefreshAllNodes = true;
        }

        if (TargetCache.Resolution != Resolution)
        {
            TargetCache.Resolution = Resolution;
            TargetCache.NodeCache.Empty();
            bRefreshAllNodes = true;
        }

        TSet<FGuid> ActiveNodeGuids;
        TSet<FGuid> DirtyNodeGuids;

        for (int32 NodeIndex = 0; NodeIndex < OutJob.Nodes.Num(); ++NodeIndex)
        {
            const FShineTextureCompiledNode& CompiledNode = OutJob.Nodes[NodeIndex];
            ActiveNodeGuids.Add(CompiledNode.NodeGuid);

            FShineTextureCachedNodeEntry& CachedNode = TargetCache.NodeCache.FindOrAdd(CompiledNode.NodeGuid);
            UTextureRenderTarget2D* const PreviousTexture = CachedNode.Texture.Get();
            UTextureRenderTarget2D* CachedTexture = CreateOrUpdateCachedRenderTarget(Asset, CachedNode.Texture, Resolution);
            const bool bTextureRecreated = PreviousTexture != CachedTexture;
            OutJob.NodeResources[NodeIndex] = CachedTexture ? CachedTexture->GameThread_GetRenderTargetResource() : nullptr;
            if (UTexture* ExternalInputTexture = CompiledNode.ExternalInputTexture)
            {
                if (FTextureResource* ExternalTextureResource = ExternalInputTexture->GetResource())
                {
                    OutJob.ExternalInputTextures[NodeIndex] = ExternalTextureResource->TextureRHI;
                }
            }

            const uint32 CurrentHash = HashCompiledNode(CompiledNode);
            bool bDirty = bRefreshAllNodes
                || bTextureRecreated
                || CachedNode.LastHash == 0
                || CachedNode.LastHash != CurrentHash
                || ChangedNodeGuids.Contains(CompiledNode.NodeGuid)
                || OutJob.NodeResources[NodeIndex] == nullptr;

            for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(CompiledNode.InputNodeGuids); ++InputIndex)
            {
                if (DirtyNodeGuids.Contains(CompiledNode.InputNodeGuids[InputIndex]))
                {
                    bDirty = true;
                    break;
                }
            }

            CachedNode.LastHash = CurrentHash;
            OutJob.DirtyNodeFlags[NodeIndex] = bDirty ? 1 : 0;
            if (bDirty)
            {
                DirtyNodeGuids.Add(CompiledNode.NodeGuid);
            }
        }

        for (auto It = TargetCache.NodeCache.CreateIterator(); It; ++It)
        {
            if (!ActiveNodeGuids.Contains(It.Key()))
            {
                It.RemoveCurrent();
            }
        }

        for (const FTextureRenderTargetResource* NodeResource : OutJob.NodeResources)
        {
            if (NodeResource != nullptr)
            {
                return true;
            }
        }

        return false;
    }

    FRDGTextureRef CreateSolidColorTexture(FRDGBuilder& GraphBuilder, const FLinearColor& Color, const TCHAR* Name)
    {
        FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
            FIntPoint(1, 1),
            ShineTextureIntermediatePreviewFormat,
            FClearValueBinding::None,
            TexCreate_ShaderResource | TexCreate_UAV);

        FRDGTextureRef Texture = GraphBuilder.CreateTexture(Desc, Name);
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Texture), FVector4f(Color.R, Color.G, Color.B, Color.A));
        return Texture;
    }

    void RenderPreviewDisplayPass(
        FRHICommandListImmediate& RHICmdList,
        FTextureRenderTargetResource* SourceResource,
        FTextureRenderTargetResource* DisplayResource,
        FIntPoint Resolution,
        uint8 DisplayMode)
    {
        if (!SourceResource || !DisplayResource)
        {
            return;
        }

        FRHITexture* SourceTextureRHI = SourceResource->GetRenderTargetTexture();
        FRHITexture* DisplayTextureRHI = DisplayResource->GetRenderTargetTexture();
        if (!SourceTextureRHI || !DisplayTextureRHI)
        {
            return;
        }

        FRDGBuilder GraphBuilder(RHICmdList);
        FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(
            CreateRenderTarget(SourceTextureRHI, TEXT("ShineTexture.DisplaySource")));
        FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(
            CreateRenderTarget(DisplayTextureRHI, TEXT("ShineTexture.DisplayOutput")));

        FShineTexturePreviewDisplayCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FShineTexturePreviewDisplayCS::FParameters>();
        PassParameters->Resolution = FUintVector2(Resolution.X, Resolution.Y);
        PassParameters->DisplayMode = static_cast<uint32>(DisplayMode);
        PassParameters->InputTexture = SourceTexture;
        PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

        const TShaderMapRef<FShineTexturePreviewDisplayCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("ShineTexturePreviewDisplay_%d", DisplayMode),
            ERDGPassFlags::Compute,
            ComputeShader,
            PassParameters,
            FIntVector(FMath::DivideAndRoundUp(Resolution.X, 8), FMath::DivideAndRoundUp(Resolution.Y, 8), 1));

        GraphBuilder.Execute();
    }

    void ResolvePreviewToDisplayResource(
        FRHICommandListImmediate& RHICmdList,
        const FShineTextureRenderTargetJob& Job)
    {
        if (Job.NodeResources.IsEmpty() || !Job.NodeResources.Last())
        {
            return;
        }

        FTextureRenderTargetResource* const SourceResource = Job.NodeResources.Last();
        if (Job.PreviewDisplayMode != 0 && Job.DisplayResource)
        {
            RenderPreviewDisplayPass(RHICmdList, SourceResource, Job.DisplayResource, Job.Resolution, Job.PreviewDisplayMode);
        }
        else if (Job.Resource)
        {
            RenderPreviewDisplayPass(RHICmdList, SourceResource, Job.Resource, Job.Resolution, 0);
        }
    }

    void RenderJob_RenderThread(FRHICommandListImmediate& RHICmdList, const FShineTextureRenderTargetJob& Job)
    {
        if (!Job.Resource)
        {
            return;
        }

        const bool bDisplayOnlyJob = Job.Nodes.IsEmpty();
        if (bDisplayOnlyJob)
        {
            if (Job.DisplayResource && Job.PreviewDisplayMode != 0)
            {
                RenderPreviewDisplayPass(RHICmdList, Job.Resource, Job.DisplayResource, Job.Resolution, Job.PreviewDisplayMode);
            }
            return;
        }

        if (Job.NodeResources.Num() != Job.Nodes.Num() || Job.DirtyNodeFlags.Num() != Job.Nodes.Num())
        {
            return;
        }

        bool bAnyDirtyNodes = false;
        if (Job.Resource->GetRenderTargetTexture() == nullptr)
        {
            return;
        }

        for (int32 NodeIndex = 0; NodeIndex < Job.Nodes.Num(); ++NodeIndex)
        {
            if (!Job.NodeResources[NodeIndex] || Job.NodeResources[NodeIndex]->GetRenderTargetTexture() == nullptr)
            {
                return;
            }

            bAnyDirtyNodes |= Job.DirtyNodeFlags[NodeIndex] != 0;
        }

        if (!bAnyDirtyNodes)
        {
            ResolvePreviewToDisplayResource(RHICmdList, Job);
            return;
        }

        FRDGBuilder GraphBuilder(RHICmdList);

        FRDGTextureRef BlackTexture = CreateSolidColorTexture(GraphBuilder, FLinearColor::Black, TEXT("ShineTexture.Black"));
        FRDGTextureRef WhiteTexture = CreateSolidColorTexture(GraphBuilder, FLinearColor::White, TEXT("ShineTexture.White"));
        FRDGTextureRef NormalTexture = CreateSolidColorTexture(GraphBuilder, FLinearColor(0.5f, 0.5f, 1.0f, 1.0f), TEXT("ShineTexture.Normal"));

        TArray<FRDGTextureRef> NodeTextures;
        NodeTextures.SetNum(Job.Nodes.Num());

        for (int32 NodeIndex = 0; NodeIndex < Job.Nodes.Num(); ++NodeIndex)
        {
            NodeTextures[NodeIndex] = GraphBuilder.RegisterExternalTexture(
                CreateRenderTarget(Job.NodeResources[NodeIndex]->GetRenderTargetTexture(), *FString::Printf(TEXT("%s.Node.%d"), *Job.DebugName, NodeIndex)));
        }

        const FIntPoint Resolution = Job.Resolution;
        const TShaderMapRef<FShineTextureNodePreviewCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

        auto AddBreakOutPass = [&](FRDGTextureRef SourceTexture, int32 ComponentIndex, const FString& DebugName) -> FRDGTextureRef
        {
            if (SourceTexture == nullptr || ComponentIndex < 0 || ComponentIndex > 3)
            {
                return SourceTexture;
            }

            const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
                Resolution,
                ShineTextureIntermediatePreviewFormat,
                FClearValueBinding::None,
                TexCreate_ShaderResource | TexCreate_UAV);
            FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, *DebugName);

            FShineTextureNodePreviewCS::FParameters* BreakOutParameters = GraphBuilder.AllocParameters<FShineTextureNodePreviewCS::FParameters>();
            BreakOutParameters->NodeType = static_cast<uint32>(EShineTextureGpuNodeType::BreakOut);
            BreakOutParameters->InputValid = FIntVector4(1, 0, 0, 0);
            BreakOutParameters->Param0 = FVector4f::Zero();
            BreakOutParameters->Param1 = FVector4f(static_cast<float>(ComponentIndex), 0.0f, 0.0f, 0.0f);
            BreakOutParameters->Resolution = FUintVector2(Resolution.X, Resolution.Y);
            BreakOutParameters->InputTextureA = SourceTexture;
            BreakOutParameters->InputTextureB = BlackTexture;
            BreakOutParameters->InputTextureC = BlackTexture;
            BreakOutParameters->InputTextureD = BlackTexture;
            BreakOutParameters->InputSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
            BreakOutParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("ShineTextureBreakOut_%s", *DebugName),
                ERDGPassFlags::Compute,
                ComputeShader,
                BreakOutParameters,
                FIntVector(FMath::DivideAndRoundUp(Resolution.X, 8), FMath::DivideAndRoundUp(Resolution.Y, 8), 1));

            return OutputTexture;
        };

        for (int32 NodeIndex = 0; NodeIndex < Job.Nodes.Num(); ++NodeIndex)
        {
            if (Job.DirtyNodeFlags[NodeIndex] == 0)
            {
                continue;
            }

            const FShineTextureCompiledNode& Node = Job.Nodes[NodeIndex];
            FRDGTextureRef ExternalInputTexture = nullptr;
            if (Job.ExternalInputTextures.IsValidIndex(NodeIndex) && Job.ExternalInputTextures[NodeIndex].IsValid())
            {
                ExternalInputTexture = GraphBuilder.RegisterExternalTexture(
                    CreateRenderTarget(Job.ExternalInputTextures[NodeIndex], *FString::Printf(TEXT("%s.Node.%d.External"), *Job.DebugName, NodeIndex)));
            }

            auto ResolveInputTexture = [&](int32 InputIndex, FRDGTextureRef DefaultTexture) -> FRDGTextureRef
            {
                return InputIndex != INDEX_NONE ? NodeTextures[InputIndex] : DefaultTexture;
            };

            auto ResolveDefaultTexture = [&](EShineTextureDefaultInputTexture DefaultInput) -> FRDGTextureRef
            {
                switch (DefaultInput)
                {
                case EShineTextureDefaultInputTexture::White:
                    return WhiteTexture;
                case EShineTextureDefaultInputTexture::Normal:
                    return NormalTexture;
                case EShineTextureDefaultInputTexture::Black:
                default:
                    return BlackTexture;
                }
            };

            FShineTextureNodePreviewCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FShineTextureNodePreviewCS::FParameters>();
            PassParameters->NodeType = static_cast<uint32>(Node.Type);
            PassParameters->InputValid = FIntVector4(
                (Node.InputIndices[0] != INDEX_NONE || ExternalInputTexture != nullptr) ? 1 : 0,
                Node.InputIndices[1] != INDEX_NONE ? 1 : 0,
                Node.InputIndices[2] != INDEX_NONE ? 1 : 0,
                Node.InputIndices[3] != INDEX_NONE ? 1 : 0);
            PassParameters->Param0 = Node.Param0;
            PassParameters->Param1 = Node.Param1;
            PassParameters->Resolution = FUintVector2(Job.Resolution.X, Job.Resolution.Y);
            PassParameters->InputTextureA = AddBreakOutPass(
                Node.InputIndices[0] != INDEX_NONE
                    ? ResolveInputTexture(Node.InputIndices[0], ResolveDefaultTexture(Node.DefaultInputs[0]))
                    : (ExternalInputTexture != nullptr ? ExternalInputTexture : ResolveDefaultTexture(Node.DefaultInputs[0])),
                Node.InputOutputComponents[0],
                FString::Printf(TEXT("%s.Node.%d.Input.A"), *Job.DebugName, NodeIndex));
            PassParameters->InputTextureB = AddBreakOutPass(
                ResolveInputTexture(Node.InputIndices[1], ResolveDefaultTexture(Node.DefaultInputs[1])),
                Node.InputOutputComponents[1],
                FString::Printf(TEXT("%s.Node.%d.Input.B"), *Job.DebugName, NodeIndex));
            PassParameters->InputTextureC = AddBreakOutPass(
                ResolveInputTexture(Node.InputIndices[2], ResolveDefaultTexture(Node.DefaultInputs[2])),
                Node.InputOutputComponents[2],
                FString::Printf(TEXT("%s.Node.%d.Input.C"), *Job.DebugName, NodeIndex));
            PassParameters->InputTextureD = AddBreakOutPass(
                ResolveInputTexture(Node.InputIndices[3], ResolveDefaultTexture(Node.DefaultInputs[3])),
                Node.InputOutputComponents[3],
                FString::Printf(TEXT("%s.Node.%d.Input.D"), *Job.DebugName, NodeIndex));
            PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
            PassParameters->OutputTexture = GraphBuilder.CreateUAV(NodeTextures[NodeIndex]);

            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("ShineTextureNode_%s_%d", *Job.DebugName, NodeIndex),
                ERDGPassFlags::Compute,
                ComputeShader,
                PassParameters,
                FIntVector(FMath::DivideAndRoundUp(Job.Resolution.X, 8), FMath::DivideAndRoundUp(Job.Resolution.Y, 8), 1));
        }

        GraphBuilder.Execute();
        ResolvePreviewToDisplayResource(RHICmdList, Job);
    }

    void PopulatePreviewDisplayJobFields(UShineTextureGraphNodeBase* TargetNode, FIntPoint Resolution, FShineTextureRenderTargetJob& Job)
    {
        if (!TargetNode)
        {
            return;
        }

        Job.PreviewDisplayMode = static_cast<uint8>(TargetNode->GetPreviewDisplayMode());
        if (Job.PreviewDisplayMode == 0)
        {
            Job.DisplayResource = nullptr;
            return;
        }

        if (UTextureRenderTarget2D* DisplayTexture = TargetNode->GetOrCreateInlinePreviewDisplayRenderTarget(Resolution))
        {
            Job.DisplayResource = DisplayTexture->GameThread_GetRenderTargetResource();
        }
    }
}

void ShineTexturePreviewRenderer::RefreshAssetPreview(UShineTextureAsset* Asset, const FEdGraphEditAction* InAction)
{
    if (!Asset)
    {
        return;
    }

    PurgeDeadPreviewCaches();

    UShineTextureGraph* Graph = Asset->GetOrCreateGraph();
    UShineTextureOutputNode* OutputNode = FindOutputNode(Graph);
    if (!OutputNode)
    {
        return;
    }

    TArray<FShineTextureRenderTargetJob> Jobs;
    TSet<FGuid> ChangedNodeGuids;
    CollectChangedNodeGuids(InAction, ChangedNodeGuids);
    const bool bForceFullRefresh = InAction == nullptr || InAction->Action == GRAPHACTION_Default || InAction->Nodes.IsEmpty();

    if (UTextureRenderTarget2D* PreviewTexture = Asset->GetOrCreatePreviewRenderTarget(FIntPoint(OutputNode->GetPreviewResolution(), OutputNode->GetPreviewResolution())))
    {
        FShineTextureRenderTargetJob Job;
        const FIntPoint OutputResolution(OutputNode->GetPreviewResolution(), OutputNode->GetPreviewResolution());
        if (BuildRenderJob(Asset, OutputNode, OutputResolution, PreviewTexture->GameThread_GetRenderTargetResource(), TEXT("ShineTexture.Output"), ChangedNodeGuids, bForceFullRefresh, Job))
        {
            PopulatePreviewDisplayJobFields(OutputNode, OutputResolution, Job);
            Jobs.Add(MoveTemp(Job));
        }
        else if (OutputNode->GetPreviewDisplayMode() != EShineTexturePreviewDisplayMode::Color)
        {
            FShineTextureRenderTargetJob DisplayJob;
            DisplayJob.Resolution = OutputResolution;
            DisplayJob.Resource = PreviewTexture->GameThread_GetRenderTargetResource();
            DisplayJob.DebugName = TEXT("ShineTexture.Output.Display");
            PopulatePreviewDisplayJobFields(OutputNode, OutputResolution, DisplayJob);
            if (DisplayJob.DisplayResource)
            {
                Jobs.Add(MoveTemp(DisplayJob));
            }
        }
    }

    for (UEdGraphNode* GraphNode : Graph->Nodes)
    {
        UShineTextureGraphNodeBase* TextureNode = Cast<UShineTextureGraphNodeBase>(GraphNode);
        if (!TextureNode || TextureNode == OutputNode || !TextureNode->IsInlinePreviewExpanded())
        {
            continue;
        }

        const FIntPoint PreviewSize = TextureNode->GetInlinePreviewSize();
        if (UTextureRenderTarget2D* PreviewTexture = TextureNode->GetOrCreateInlinePreviewRenderTarget(PreviewSize))
        {
            FShineTextureRenderTargetJob Job;
            if (BuildRenderJob(Asset, TextureNode, PreviewSize, PreviewTexture->GameThread_GetRenderTargetResource(), FString::Printf(TEXT("ShineTexture.Preview.%s"), *TextureNode->GetName()), ChangedNodeGuids, bForceFullRefresh, Job))
            {
                PopulatePreviewDisplayJobFields(TextureNode, PreviewSize, Job);
                Jobs.Add(MoveTemp(Job));
            }
            else if (TextureNode->GetPreviewDisplayMode() != EShineTexturePreviewDisplayMode::Color)
            {
                FShineTextureRenderTargetJob DisplayJob;
                DisplayJob.Resolution = PreviewSize;
                DisplayJob.Resource = PreviewTexture->GameThread_GetRenderTargetResource();
                DisplayJob.DebugName = FString::Printf(TEXT("ShineTexture.Preview.%s.Display"), *TextureNode->GetName());
                PopulatePreviewDisplayJobFields(TextureNode, PreviewSize, DisplayJob);
                if (DisplayJob.DisplayResource)
                {
                    Jobs.Add(MoveTemp(DisplayJob));
                }
            }
        }
    }

    Jobs.RemoveAll([](const FShineTextureRenderTargetJob& Job)
    {
        if (Job.Resource == nullptr)
        {
            return true;
        }

        if (Job.Nodes.IsEmpty())
        {
            return Job.DisplayResource == nullptr || Job.PreviewDisplayMode == 0;
        }

        return false;
    });

    if (Jobs.IsEmpty())
    {
        return;
    }

    ENQUEUE_RENDER_COMMAND(ShineTextureRenderPreviewJobs)(
        [Jobs = MoveTemp(Jobs)](FRHICommandListImmediate& RHICmdList)
        {
            for (const FShineTextureRenderTargetJob& Job : Jobs)
            {
                RenderJob_RenderThread(RHICmdList, Job);
            }
        });
}
