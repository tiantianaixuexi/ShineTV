#include "Node/ShineTextureNodeRegistry.h"

#include "EdGraph/EdGraphNode.h"
#include "Node/Presets/Math/ShineTextureAbsNode.h"
#include "Node/Presets/Math/ShineTextureAddNode.h"
#include "Node/Presets/Math/ShineTextureAndNode.h"
#include "Node/Presets/Math/ShineTextureAppendVector2Node.h"
#include "Node/Presets/Math/ShineTextureAppendVector3Node.h"
#include "Node/Presets/Math/ShineTextureAppendVector4Node.h"
#include "Node/Presets/ShineTextureBlurNode.h"
#include "Node/Presets/ShineTextureBlendNode.h"
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
#include "Node/ShineTextureGraphNodeBase.h"

namespace ShineTextureNodeRegistry
{
    namespace
    {
        enum class EShineTextureNodeMenuCategory : uint8
        {
            Source,
            Arithmetic,
            Vector,
            Logic,
            Blend,
            Adjust,
            Filter,
            Transform,
            Preview,
        };

        const FText& MenuCategory(EShineTextureNodeMenuCategory Category)
        {
            switch (Category)
            {
            case EShineTextureNodeMenuCategory::Source:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "SourceCategory", "Texture|Source");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Arithmetic:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "ArithmeticCategory", "Texture|Math|Arithmetic");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Vector:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "VectorCategory", "Texture|Math|Vector");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Logic:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "LogicCategory", "Texture|Math|Logic");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Blend:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "BlendCategory", "Texture|Blend");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Adjust:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "AdjustCategory", "Texture|Adjust");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Filter:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "FilterCategory", "Texture|Filter");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Transform:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "TransformCategory", "Texture|Transform");
                    return Value;
                }
            case EShineTextureNodeMenuCategory::Preview:
            default:
                {
                    static const FText Value = NSLOCTEXT("ShineTextureNodeRegistry", "PreviewCategory", "Texture|Preview");
                    return Value;
                }
            }
        }

#define SHINE_TEXTURE_NODE_ENTRY(NodeClass, LabelKey, LabelText, TooltipKey, TooltipText, CategoryEnum) \
            { \
                NodeClass::StaticClass(), \
                NSLOCTEXT("ShineTextureNodeRegistry", LabelKey, LabelText), \
                NSLOCTEXT("ShineTextureNodeRegistry", TooltipKey, TooltipText), \
                MenuCategory(EShineTextureNodeMenuCategory::CategoryEnum) \
            },
    } // namespace

    const TArray<FShineTextureNodeRegistration>& GetNodeRegistrations()
    {
        static const TArray<FShineTextureNodeRegistration> Registrations =
        {
#include "Node/ShineTextureNodeRegistryEntries.inl"
#undef SHINE_TEXTURE_NODE_ENTRY
        };

        return Registrations;
    }
}
