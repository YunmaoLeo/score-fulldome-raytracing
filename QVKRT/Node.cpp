#include "Node.hpp"

#include "load_ply.hpp"
#include "halp/geometry.hpp"

#include <Gfx/Graph/GeometryFilterNodeRenderer.hpp>

#include <private/qrhivulkan_p.h>
#include <QVKRT/rt/rt.hpp>
#include "score/gfx/Vulkan.hpp"

#include <Gfx/Graph/NodeRenderer.hpp>

#include <score/tools/Debug.hpp>

namespace QVKRT
{
/** Here we define a mesh fairly manually and in a fairly suboptimal way
 * (based on this: https://pastebin.com/DXKEmvap)
 */
struct TexturedCube final : score::gfx::BasicMesh
{
  static std::vector<float> generateCubeMesh() noexcept
  {
    struct vec3
    {
      float x, y, z;
    };
    struct vec2
    {
      float x, y;
    };

    static constexpr const unsigned int indices[6 * 6] = {
        0, 1, 3, 3, 1, 2, //
        1, 5, 2, 2, 5, 6, //
        5, 4, 6, 6, 4, 7, //
        4, 0, 7, 7, 0, 3, //
        3, 2, 7, 7, 2, 6, //
        4, 5, 0, 0, 5, 1  //
    };

    static constexpr const vec3 vertices[8]
        = {{-1., -1., -1.}, //
           {1., -1., -1.},  //
           {1., 1., -1.},   //
           {-1., 1., -1.},  //
           {-1., -1., 1.},  //
           {1., -1., 1.},   //
           {1., 1., 1.},    //
           {-1., 1., 1.}};

    static constexpr const vec2 texCoords[4]
        = {{0, 0}, //
           {1, 0}, //
           {1, 1}, //
           {0, 1}};

    static constexpr const vec3 normals[6]
        = {{0, 0, 1},  //
           {1, 0, 0},  //
           {0, 0, -1}, //
           {-1, 0, 0}, //
           {0, 1, 0},  //
           {0, -1, 0}};

    static constexpr const int texInds[6] = {0, 1, 3, 3, 1, 2};

    std::vector<float> meshBuf;
    meshBuf.reserve(36 * 3 + 36 * 2 + 36 * 3);

    // The beginning of the buffer is:
    // [ {x y z} {x y z} {x y z} ... ]
    for (int i = 0; i < 36; i++)
    {
      meshBuf.push_back(vertices[indices[i]].x);
      meshBuf.push_back(vertices[indices[i]].y);
      meshBuf.push_back(vertices[indices[i]].z);
    }

    // Then we store the texcoords at the end: [ {u v} {u v} {u v} ... ]
    for (int i = 0; i < 36; i++)
    {
      meshBuf.push_back(texCoords[texInds[i % 6]].x);
      meshBuf.push_back(texCoords[texInds[i % 6]].y);
    }

    // Then the normals (unused in this example)
    for (int i = 0; i < 36; i++)
    {
      meshBuf.push_back(normals[indices[i / 6]].x);
      meshBuf.push_back(normals[indices[i / 6]].y);
      meshBuf.push_back(normals[indices[i / 6]].z);
    }

    return meshBuf;
  }

  [[nodiscard]] Flags flags() const noexcept override
  {
    return HasPosition | HasTexCoord | HasNormals;
  }

  // Generate our mesh data
  const std::vector<float> mesh = generateCubeMesh();

  explicit TexturedCube()
  {
    // Our mesh's attribute data is not interleaved, thus
    // we have multiple bindings stored in the same buffer:
    // [ {x y z} {x y z} {x y z} ... ] [ {u v} {u v} {u v} ... ]

    // Vertex
    // Each `in vec3 position;` in the vertex shader will be an [ x y z ] element
    // The stride is the 3 * sizeof(float) - it means that:
    // * vertex 0's data in this attribute will start at 0 (in bytes)
    // * vertex 1's data in this attribute will start at 3 * sizeof(float)
    // * vertex 2's data in this attribute will start at 6 * sizeof(float)
    // (basically that every vertex position is one after each other).
    vertexBindings.push_back({3 * sizeof(float)});
    vertexAttributes.push_back({0, 0, QRhiVertexInputAttribute::Float3, 0});

    // Texcoord
    // Each `in vec2 texcoord;` in the fragment shader will be an [ u v ] element
    vertexBindings.push_back({2 * sizeof(float)});
    vertexAttributes.push_back({1, 1, QRhiVertexInputAttribute::Float2, 0});

    // These variables are used by score to upload the texture
    // and send the draw call automatically
    vertexArray = mesh;
    vertexCount = 36;

    // Define the topology of our mesh
    topology = pip::Topology::Triangles;
    cullMode = pip::CullMode::Back;
    frontFace = pip::FrontFace::CW;

    // Note: if we had an interleaved mesh, where the data is stored like
    // [ { x y z u v } { x y z u v } { x y z u v } ] ...
    // Then we'd have a single binding of stride 5 * sizeof(float)
    // (3 floats for the position and 2 floats for the texture coordinates):

    // vertexBindings.push_back({5 * sizeof(float)});

    // And two attributes mapped to that binding
    // The first attribute (position) starts at byte 0 in each element:
    //
    //     vertexAttributes.push_back(
    //         {0, 0, QRhiVertexInputAttribute::Float3, 0});
    //
    // The second attribute (texcoord) starts at byte 3 * sizeof(float)
    // (just after the position):
    //
    //     vertexAttributes.push_back(
    //         {0, 1, QRhiVertexInputAttribute::Float2, 3 * sizeof(float)});
  }

  // Utility singleton
  static const TexturedCube& instance() noexcept
  {
    static const TexturedCube cube;
    return cube;
  }

  // Ignore this function
  const char* defaultVertexShader() const noexcept override { return ""; }

  // This function is called when running the draw calls,
  // it tells the pipeline which buffers are going to be bound
  // to each attribute defined above
  void setupBindings(
      const score::gfx::MeshBuffers& bufs,
      QRhiCommandBuffer& cb) const noexcept override
  {
    const QRhiCommandBuffer::VertexInput bindings[] = {
        {bufs.mesh, 0}, // vertex starts at offset zero
        {bufs.mesh,
         36 * 3 * sizeof(float)}, // texcoord starts after all the vertices
    };

    cb.setVertexInput(0, 2, bindings);
  }
};

// Here we define basic shaders to display a textured cube with a camera
static const constexpr auto vertex_shader = R"_(#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;

layout(location = 1) out vec2 v_texcoord;
layout(binding = 3) uniform sampler2D y_tex;

layout(std140, binding = 0) uniform renderer_t {
  mat4 clipSpaceCorrMatrix;
  vec2 texcoordAdjust;
  vec2 renderSize;
};

layout(std140, binding = 2) uniform model_t {
  mat4 matrixModelViewProjection;
  mat4 matrixModelView;
  mat4 matrixModel;
  mat4 matrixView;
  mat4 matrixProjection;
  mat3 matrixNormal;
};

out gl_PerVertex { vec4 gl_Position; };

void main()
{
  v_texcoord = vec2(texcoord.x, texcoordAdjust.y + texcoordAdjust.x * texcoord.y);
  v_texcoord = texcoord;
  gl_Position = clipSpaceCorrMatrix * matrixModelViewProjection * vec4(position, 1.0);
}
)_";

static const constexpr auto fragment_shader = R"_(#version 450
layout(std140, binding = 0) uniform renderer_t {
  mat4 clipSpaceCorrMatrix;
  vec2 texcoordAdjust;
  vec2 renderSize;
};

layout(binding=3) uniform sampler2D y_tex;

layout(location = 1) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main ()
{
  fragColor = texture(y_tex, v_texcoord.xy);
}
)_";

Node::Node()
{
  this->requiresDepth = true;

  input.push_back(new score::gfx::Port{this,
    nullptr, score::gfx::Types::Geometry,{}});

  // This texture is provided by score
  m_image = QImage(":/ossia-score.png");

  // Load ubo address in m_materialData
  m_materialData.reset((char*)&ubo);

  // Create an output port to indicate that this node
  // draws something
  output.push_back(
      new score::gfx::Port{this, {}, score::gfx::Types::Image, {}});
}

Node::~Node()
{
  // We do not want to free m_materialData as it is
  // not allocated dynamically
  m_materialData.release();
}

// This header is used because some function names change between Qt 5 and Qt 6
class Renderer : public score::gfx::GenericNodeRenderer
{
public:
  using GenericNodeRenderer::GenericNodeRenderer;

private:
  ~Renderer() { }

  QSize m_pixelSize;
  QRhi* m_rhi = nullptr;
  QRhiTexture* m_rhiTex = nullptr;
  QRhiRenderPassDescriptor *m_rpDesc = nullptr;
  QRhiTextureRenderTarget *m_rhiRt = nullptr;

  QVulkanInstance *m_inst = nullptr;
  VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
  VkDevice m_dev = VK_NULL_HANDLE;
  QVulkanDeviceFunctions *m_devFuncs = nullptr;
  QVulkanFunctions *m_funcs = nullptr;

  VkImage m_output = VK_NULL_HANDLE;
  VkImageLayout m_outputLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkDeviceMemory m_outputMemory = VK_NULL_HANDLE;
  VkImageView m_outputView = VK_NULL_HANDLE;

  RayTracer raytracing;

  int frameSlotCount;

  // This function is only useful to reimplement if the node has an
  // input port (e.g. if it's an effect / filter / ...)
  score::gfx::TextureRenderTarget
  renderTargetForInput(const score::gfx::Port& p) override
  {
    return {};
  }

  // The pipeline is the object which contains all the state
  // needed by the graphics card when issuing draw calls
  score::gfx::Pipeline buildPipeline(
      const score::gfx::RenderList& renderer,
      const score::gfx::Mesh& mesh,
      const QShader& vertexS,
      const QShader& fragmentS,
      const score::gfx::TextureRenderTarget& rt,
      QRhiShaderResourceBindings* srb)
  {

    auto& rhi = *renderer.state.rhi;
    auto ps = rhi.newGraphicsPipeline();
    ps->setName("Node::ps");
    SCORE_ASSERT(ps);

    // Set various graphics options
    QRhiGraphicsPipeline::TargetBlend premulAlphaBlend;
    premulAlphaBlend.enable = true;
    ps->setTargetBlends({premulAlphaBlend});

    ps->setSampleCount(1);

    // Set the shaders used
    ps->setShaderStages(
        {{QRhiShaderStage::Vertex, vertexS},
         {QRhiShaderStage::Fragment, fragmentS}});

    // Set the mesh specification
    mesh.preparePipeline(*ps);

    // Set the shader resources (input UBOs, samplers & textures...)
    ps->setShaderResourceBindings(srb);

    // Where we are rendering
    SCORE_ASSERT(rt.renderPass);
    ps->setRenderPassDescriptor(rt.renderPass);

    SCORE_ASSERT(ps->create());
    return {ps, srb};
  }

  void createNativeTexture()
  {
    qDebug() << "new texture of size" << m_pixelSize;

    m_outputLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = uint32_t(m_pixelSize.width());
    imageInfo.extent.height = uint32_t(m_pixelSize.height());
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = m_outputLayout;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

    m_devFuncs->vkCreateImage(m_dev, &imageInfo, nullptr, &m_output);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetImageMemoryRequirements(m_dev, m_output, &memReq);
    quint32 memIndex = 0;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    m_funcs->vkGetPhysicalDeviceMemoryProperties(m_physDev, &physDevMemProps);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
      if (!(memReq.memoryTypeBits & (1 << i)))
        continue;
      if (physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        memIndex = i;
        break;
      }
    }

    VkMemoryAllocateInfo allocInfo = {
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      nullptr,
      memReq.size,
      memIndex
  };

    m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_outputMemory);
    m_devFuncs->vkBindImageMemory(m_dev, m_output, m_outputMemory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_output;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    m_devFuncs->vkCreateImageView(m_dev, &viewInfo, nullptr, &m_outputView);

    m_rhiTex = m_rhi->newTexture(QRhiTexture::RGBA8, m_pixelSize, 1,
                               QRhiTexture::RenderTarget
                             | QRhiTexture::UsedWithLoadStore);

    QRhiTexture::NativeTexture nt;
    nt.object = quint64(m_output);
    nt.layout = int(m_outputLayout);

    bool ok = m_rhiTex->createFrom(nt);
    Q_ASSERT(ok);
  }

  void init(score::gfx::RenderList& renderer, QRhiResourceUpdateBatch& res) override
  {
    // Start initialize raytracing
    // get rhi
    m_rhi = renderer.state.rhi;
    Q_ASSERT(m_rhi && m_rhi->backend()== QRhi::Vulkan);

    frameSlotCount = m_rhi->resourceLimit(QRhi::FramesInFlight);
    qDebug() << "FrameSlotCount" << frameSlotCount;

    m_inst = score::gfx::staticVulkanInstance();
    Q_ASSERT(m_inst && m_inst->isValid());

    const QRhiVulkanNativeHandles* handles = static_cast<const QRhiVulkanNativeHandles*>(m_rhi->nativeHandles());
    Q_ASSERT(handles);

    m_physDev = handles->physDev;
    m_dev = handles->dev;
    Q_ASSERT(m_physDev && m_dev);

    m_devFuncs = m_inst->deviceFunctions(m_dev);
    m_funcs = m_inst->functions();
    Q_ASSERT(m_devFuncs && m_funcs);

    raytracing.init(m_physDev, m_dev, m_funcs, m_devFuncs);

    m_pixelSize = renderer.state.renderSize;

    qDebug() << "Pixel Size" << m_pixelSize;


    // initialize texture for ray tracing rendering
    createNativeTexture();

    //original cube render pipeline
    const auto& mesh = TexturedCube::instance();

    // Load the mesh data into the GPU
    {
      m_meshbufs = renderer.initMeshBuffer(mesh, res);
    }

    // Initialize the Process UBO (provides timing information, etc.)
    {
      processUBOInit(renderer);
    }

    // Initialize our camera
    {
      m_material.size = sizeof(score::gfx::ModelCameraUBO);
      m_material.buffer = renderer.state.rhi->newBuffer(
          QRhiBuffer::Dynamic,
          QRhiBuffer::UniformBuffer,
          sizeof(score::gfx::ModelCameraUBO));
      SCORE_ASSERT(m_material.buffer->create());
    }

    auto& n = static_cast<const Node&>(this->node);
    auto& rhi = *renderer.state.rhi;

    // Create GPU textures for the image
    const QSize sz = n.m_image.size();
    m_texture = rhi.newTexture(
        QRhiTexture::BGRA8,
        QSize{sz.width(), sz.height()},
        1,
        QRhiTexture::Flag{});

    m_texture->setName("Node::tex");
    m_texture->create();

    // Create the sampler in which we are going to put the texture
    {
      auto sampler = rhi.newSampler(
          QRhiSampler::Linear,
          QRhiSampler::Linear,
          QRhiSampler::None,
          QRhiSampler::Repeat,
          QRhiSampler::Repeat);

      sampler->setName("Node::sampler");
      sampler->create();
      // m_samplers.push_back({sampler, m_texture});
      m_samplers.push_back({sampler, m_rhiTex});
    }
    // Generate the shaders
    std::tie(m_vertexS, m_fragmentS) = score::gfx::makeShaders(
        renderer.state, vertex_shader, fragment_shader);
    SCORE_ASSERT(m_vertexS.isValid());
    SCORE_ASSERT(m_fragmentS.isValid());

    // defaultPassesInit(renderer,mesh);
    // Create the rendering pipelines for each output of this node.
    for (score::gfx::Edge* edge : this->node.output[0]->edges)
    {
      auto rt = renderer.renderTargetForOutput(*edge);
      if (rt.renderTarget)
      {
        auto bindings = createDefaultBindings(
            renderer, rt, m_processUBO, m_material.buffer, m_samplers);
        auto pipeline = buildPipeline(
            renderer, mesh, m_vertexS, m_fragmentS, rt, bindings);
        m_p.emplace_back(edge, pipeline);
      }
    }

    // std::vector<QVector4D> positions;
    // std::vector<QVector4D> colors;
    // bool ok = QVKRT::loadPly("/home/lejie.liu/Documents/rt/score/src/addons/score-vfx-template/QVKRT/rt/bunny/claw.ply", positions, colors);
    // if (ok)
    // {
    //   raytracing.setPointCloud(positions, colors);
    //   qDebug() << "Successfully read the data and upload to GPU";
    // }
    // else
    // {
    //   qWarning() << "Failed to read the point cloud data";
    // }

  }

  int m_rotationCount = 0;
  void update(score::gfx::RenderList& renderer, QRhiResourceUpdateBatch& res, score::gfx::Edge* edge)
      override
  {
    auto& n = static_cast<const Node&>(this->node);


    bool mustRecreatePasses = false;
    {
      // Set up a basic camera
      auto& ubo = (score::gfx::ModelCameraUBO&)n.ubo;

      // We use the Qt class QMatrix4x4 as an utility
      // as it provides everything needed for projection transformations

      // Our object rotates in a very crude way
      QMatrix4x4 model;
      model.scale(0.75);
      // model.rotate(m_rotationCount++, QVector3D(1, 1, 1));

      // The camera and viewports are fixed
      QMatrix4x4 view;
      view.lookAt(QVector3D{0, 0, 1}, QVector3D{0, 0, 0}, QVector3D{0, 1, 0});

      QMatrix4x4 projection;
      projection.perspective(90, 16. / 9., 0.001, 1000.);

      QMatrix4x4 mv = view * model;
      QMatrix4x4 mvp = projection * mv;
      QMatrix3x3 norm = model.normalMatrix();

      score::gfx::copyMatrix(mvp, ubo.mvp);
      score::gfx::copyMatrix(mv, ubo.mv);
      score::gfx::copyMatrix(model, ubo.model);
      score::gfx::copyMatrix(view, ubo.view);
      score::gfx::copyMatrix(projection, ubo.projection);
      score::gfx::copyMatrix(norm, ubo.modelNormal);

      // Send the camera UBO to the graphics card
      res.updateDynamicBuffer(m_material.buffer, 0, m_material.size, &ubo);
    }


    // Update the process UBO (indicates timing)
    res.updateDynamicBuffer(
        m_processUBO,
        0,
        sizeof(score::gfx::ProcessUBO),
        &n.standardUBO);


    if (this->geometryChanged)  // n = static_cast<const Node&>(this->node);
    {
      if (!n.m_positions.empty())
      {
        raytracing.setPointCloud(n.m_positions, n.m_colors);
        qDebug() << "Geometry input updated, uploaded to GPU!";
      }
    }

    // If images haven't been uploaded yet, upload them.
    if (!m_uploaded)
    {
      res.uploadTexture(m_texture, n.m_image);
      m_uploaded = true;
    }


  }

  // Everything is set up, we can render our mesh
  void runRenderPass(
      score::gfx::RenderList& renderer,
      QRhiCommandBuffer& cb,
      score::gfx::Edge& edge) override
  {
    uint currentFrameSlot = renderer.frame % frameSlotCount;

    auto *cbHandles = static_cast<const QRhiVulkanCommandBufferNativeHandles *>(cb.nativeHandles());

    VkCommandBuffer vkCmdBuf = cbHandles->commandBuffer;
    m_outputLayout = raytracing.doIt(m_inst, m_physDev, m_dev, m_devFuncs, m_funcs,
                                 vkCmdBuf, m_output, m_outputLayout, m_outputView,
                                 currentFrameSlot, m_pixelSize);

    m_rhiTex->setNativeLayout(int(m_outputLayout));   // 告诉 QRhi 新布局

    m_rhiTex->setNativeLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_outputLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    m_samplers[0].texture = m_rhiTex;

    const auto& mesh = TexturedCube::instance();
    defaultRenderPass(renderer, mesh, cb, edge);
  }

  // Free resources allocated in this class
  void release(score::gfx::RenderList& r) override
  {
    m_texture->deleteLater();
    m_texture = nullptr;

    m_rhiTex->deleteLater();
    m_rhiTex = nullptr;

    // This will free all the other resources - material & process UBO, etc
    defaultRelease(r);
  }

  QRhiTexture* m_texture{};
  bool m_uploaded = false;
};

score::gfx::NodeRenderer*
Node::createRenderer(score::gfx::RenderList& r) const noexcept
{
  return new Renderer{*this};
}

void Node::process(score::gfx::Message&& msg)
{
  ProcessNode::process(msg.token);

  int32_t p = 0;
  for (const score::gfx::gfx_input& m : msg.input)
  {
    if (auto val = ossia::get_if<ossia::geometry_spec>(&m))
    {
      // We received a new mesh
      m_positions.clear();
      m_colors.clear();

      for (const auto& mesh : val->meshes->meshes)  // halp::dynamic_geometry
      {
        // Extract positions
        const float* buf = reinterpret_cast<const float*>(mesh.buffers[0].data.get());
        int vertexCount = mesh.vertices;

        for (int i = 0; i < vertexCount; i++)
        {
          QVector4D pos(buf[i * 3 + 0], buf[i * 3 + 1], buf[i * 3 + 2], 1.0f);
          m_positions.push_back(pos);
        }

        // If color buffer exists
        if (mesh.bindings.size() >= 2)
        {
          const float* colBuf =
              reinterpret_cast<const float*>(mesh.buffers[1].data.get());

          for (int i = 0; i < vertexCount; i++)
          {
            QVector4D col(colBuf[i * 3 + 0], colBuf[i * 3 + 1], colBuf[i * 3 + 2], 1.0f);
            m_colors.push_back(col);
          }
        }
        else
        {
          for (int i = 0; i < vertexCount; i++)
            m_colors.push_back(QVector4D(1.f, 1.f, 1.f, 1.f));
        }
      }

      m_geometryDirty = true; // Mark for update
    }

    ++p;
  }
}

}
