#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace RHI {

// ============================================================================
// BACKEND SELECTION
// ============================================================================
enum class Backend {
    OpenGL,
    Vulkan
};

// ============================================================================
// RESOURCE FORMATS
// ============================================================================
enum class Format {
    Unknown,

    // 8-bit formats
    R8_UNORM,
    R8_SNORM,
    R8_UINT,
    R8_SINT,

    // 16-bit formats
    R16_FLOAT,
    R16_UINT,
    R16_SINT,
    RG8_UNORM,
    RG8_SNORM,

    // 32-bit formats
    R32_FLOAT,
    R32_UINT,
    R32_SINT,
    RG16_FLOAT,
    RGBA8_UNORM,
    RGBA8_SRGB,
    BGRA8_UNORM,
    BGRA8_SRGB,
    RGB10A2_UNORM,
    RG11B10_FLOAT,

    // 64-bit formats
    RG32_FLOAT,
    RGBA16_FLOAT,

    // 128-bit formats
    RGBA32_FLOAT,

    // Depth/stencil formats
    D16_UNORM,
    D24_UNORM_S8_UINT,
    D32_FLOAT,
    D32_FLOAT_S8_UINT,

    // Compressed formats
    BC1_UNORM,
    BC1_SRGB,
    BC3_UNORM,
    BC3_SRGB,
    BC5_UNORM,
    BC7_UNORM,
    BC7_SRGB
};

// ============================================================================
// BUFFER TYPES
// ============================================================================
enum class BufferUsage : uint32_t {
    None           = 0,
    Vertex         = 1 << 0,
    Index          = 1 << 1,
    Uniform        = 1 << 2,
    Storage        = 1 << 3,
    Indirect       = 1 << 4,
    TransferSrc    = 1 << 5,
    TransferDst    = 1 << 6
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool hasFlag(BufferUsage flags, BufferUsage flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

enum class MemoryUsage {
    GpuOnly,        // Device local only (fastest GPU access)
    CpuToGpu,       // Host visible, for uploads (staging buffers)
    GpuToCpu,       // Host visible, for readbacks
    CpuOnly,        // Host cached, for CPU-side operations
    Persistent      // Persistently mapped (like VertexPool)
};

struct BufferDesc {
    size_t size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryUsage memory = MemoryUsage::GpuOnly;
    bool persistentMap = false;  // Keep buffer mapped for lifetime
    std::string debugName;
};

// ============================================================================
// TEXTURE TYPES
// ============================================================================
enum class TextureType {
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
    Texture2DArray,
    TextureCubeArray
};

enum class TextureUsage : uint32_t {
    None           = 0,
    Sampled        = 1 << 0,
    Storage        = 1 << 1,
    RenderTarget   = 1 << 2,
    DepthStencil   = 1 << 3,
    TransferSrc    = 1 << 4,
    TransferDst    = 1 << 5
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

struct TextureDesc {
    TextureType type = TextureType::Texture2D;
    Format format = Format::RGBA8_UNORM;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    uint32_t samples = 1;  // MSAA samples
    TextureUsage usage = TextureUsage::Sampled;
    std::string debugName;
};

// ============================================================================
// SAMPLER TYPES
// ============================================================================
enum class Filter {
    Nearest,
    Linear
};

enum class MipmapMode {
    Nearest,
    Linear
};

enum class AddressMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge
};

enum class CompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always
};

struct SamplerDesc {
    Filter magFilter = Filter::Linear;
    Filter minFilter = Filter::Linear;
    MipmapMode mipmapMode = MipmapMode::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    AddressMode addressW = AddressMode::Repeat;
    float mipLodBias = 0.0f;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;
    bool compareEnable = false;
    CompareOp compareOp = CompareOp::Less;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    glm::vec4 borderColor = glm::vec4(0.0f);
};

// ============================================================================
// SHADER TYPES
// ============================================================================
enum class ShaderStage : uint32_t {
    None        = 0,
    Vertex      = 1 << 0,
    Fragment    = 1 << 1,
    Geometry    = 1 << 2,
    TessControl = 1 << 3,
    TessEval    = 1 << 4,
    Compute     = 1 << 5,
    Mesh        = 1 << 6,
    Task        = 1 << 7,

    AllGraphics = Vertex | Fragment | Geometry | TessControl | TessEval,
    All         = AllGraphics | Compute | Mesh | Task
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

struct ShaderModuleDesc {
    ShaderStage stage = ShaderStage::None;
    std::vector<uint8_t> code;  // GLSL source or SPIR-V bytecode
    std::string entryPoint = "main";
    std::string debugName;
};

// ============================================================================
// PIPELINE TYPES
// ============================================================================
enum class PrimitiveTopology {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan,
    PatchList
};

enum class PolygonMode {
    Fill,
    Line,
    Point
};

enum class CullMode {
    None,
    Front,
    Back,
    FrontAndBack
};

enum class FrontFace {
    CounterClockwise,
    Clockwise
};

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
    SrcAlphaSaturate
};

enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

struct BlendState {
    bool enable = false;
    BlendFactor srcColorFactor = BlendFactor::One;
    BlendFactor dstColorFactor = BlendFactor::Zero;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlphaFactor = BlendFactor::One;
    BlendFactor dstAlphaFactor = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;
    uint8_t colorWriteMask = 0xF;  // RGBA
};

struct DepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    CompareOp depthCompareOp = CompareOp::Less;
    bool stencilTestEnable = false;
    // Stencil ops can be added if needed
};

struct RasterizerState {
    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    bool depthClampEnable = false;
    bool depthBiasEnable = false;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    float lineWidth = 1.0f;
};

// Vertex input layout
enum class VertexInputRate {
    Vertex,
    Instance
};

struct VertexBinding {
    uint32_t binding = 0;
    uint32_t stride = 0;
    VertexInputRate inputRate = VertexInputRate::Vertex;
};

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding = 0;
    Format format = Format::Unknown;
    uint32_t offset = 0;
};

struct VertexInputState {
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;
};

// ============================================================================
// RENDER PASS TYPES
// ============================================================================
enum class LoadOp {
    Load,
    Clear,
    DontCare
};

enum class StoreOp {
    Store,
    DontCare
};

struct AttachmentDesc {
    Format format = Format::Unknown;
    uint32_t samples = 1;
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
    LoadOp stencilLoadOp = LoadOp::DontCare;
    StoreOp stencilStoreOp = StoreOp::DontCare;
};

struct ClearValue {
    union {
        struct { float r, g, b, a; } color;
        struct { float depth; uint32_t stencil; } depthStencil;
    };

    ClearValue() : color{0.0f, 0.0f, 0.0f, 1.0f} {}
    static ClearValue Color(float r, float g, float b, float a = 1.0f) {
        ClearValue v;
        v.color = {r, g, b, a};
        return v;
    }
    static ClearValue DepthStencil(float depth = 1.0f, uint32_t stencil = 0) {
        ClearValue v;
        v.depthStencil = {depth, stencil};
        return v;
    }
};

// ============================================================================
// DESCRIPTOR TYPES (Resource Binding)
// ============================================================================
enum class DescriptorType {
    Sampler,
    SampledTexture,
    StorageTexture,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment
};

struct DescriptorBinding {
    uint32_t binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;
    uint32_t count = 1;
    ShaderStage stageFlags = ShaderStage::All;
};

struct DescriptorSetLayoutDesc {
    std::vector<DescriptorBinding> bindings;
};

// ============================================================================
// UTILITY
// ============================================================================
inline uint32_t getFormatSize(Format format) {
    switch (format) {
        case Format::R8_UNORM:
        case Format::R8_SNORM:
        case Format::R8_UINT:
        case Format::R8_SINT:
            return 1;
        case Format::R16_FLOAT:
        case Format::R16_UINT:
        case Format::R16_SINT:
        case Format::RG8_UNORM:
        case Format::RG8_SNORM:
            return 2;
        case Format::R32_FLOAT:
        case Format::R32_UINT:
        case Format::R32_SINT:
        case Format::RG16_FLOAT:
        case Format::RGBA8_UNORM:
        case Format::RGBA8_SRGB:
        case Format::BGRA8_UNORM:
        case Format::BGRA8_SRGB:
        case Format::RGB10A2_UNORM:
        case Format::RG11B10_FLOAT:
            return 4;
        case Format::RG32_FLOAT:
        case Format::RGBA16_FLOAT:
            return 8;
        case Format::RGBA32_FLOAT:
            return 16;
        case Format::D16_UNORM:
            return 2;
        case Format::D24_UNORM_S8_UINT:
        case Format::D32_FLOAT:
            return 4;
        case Format::D32_FLOAT_S8_UINT:
            return 5;
        default:
            return 0;
    }
}

} // namespace RHI
