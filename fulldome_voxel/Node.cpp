#include "Node.hpp"
#include "halp/geometry.hpp"
#include <private/qrhivulkan_p.h>
#include <fulldome_voxel/vk_raytracing/vk_voxel_raytracing.hpp>
#include "score/gfx/Vulkan.hpp"
#include <Gfx/Graph/NodeRenderer.hpp>
#include <score/tools/Debug.hpp>

namespace vkfrt
{
static const constexpr auto images_vertex_shader = R"_(#version 450
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(binding = 3) uniform sampler2D y_tex;
layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 0) uniform renderer_t {
  mat4 clipSpaceCorrMatrix;
  vec2 renderSize;
} renderer;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
  v_texcoord = vec2(texcoord.x, 1. - texcoord.y);
  gl_Position = renderer.clipSpaceCorrMatrix * vec4(position.xy, 0.0, 1.);
#if defined(QSHADER_HLSL) || defined(QSHADER_MSL)
  gl_Position.y = - gl_Position.y;
#endif
}
)_";

static const constexpr auto images_fragment_shader = R"_(#version 450
layout(std140, binding = 0) uniform renderer_t {
  mat4 clipSpaceCorrMatrix;
  vec2 renderSize;
} renderer;

layout(binding=3) uniform sampler2D y_tex;

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main ()
{
  vec2 factor = textureSize(y_tex, 0) / renderer.renderSize;
  vec2 ifactor = renderer.renderSize / textureSize(y_tex, 0);
  fragColor = texture(y_tex, v_texcoord);
}
)_";


Node::Node()
{
  this->requiresDepth = true;

  input.push_back(new score::gfx::Port{this,
    nullptr, score::gfx::Types::Geometry,{}});

  input.push_back(new score::gfx::Port{this, nullptr, score::gfx::Types::Vec3, {}});
  input.push_back(new score::gfx::Port{this, nullptr, score::gfx::Types::Vec3, {}});
  input.push_back(new score::gfx::Port{this, nullptr, score::gfx::Types::Float, {}});
  input.push_back(new score::gfx::Port{this, nullptr, score::gfx::Types::Int, {}});

  output.push_back(
      new score::gfx::Port{this, {}, score::gfx::Types::Image, {}});
}

Node::~Node()
{
}

// This header is used because some function names change between Qt 5 and Qt 6
class Renderer : public score::gfx::GenericNodeRenderer
{
public:
  using GenericNodeRenderer::GenericNodeRenderer;

private:
  ~Renderer() { }
  bool m_isRtReady = false;
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

  VkRayTracer raytracing;

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

    createNativeTexture();

    processUBOInit(renderer);

    const auto& mesh = renderer.defaultQuad();
    defaultMeshInit(renderer, mesh, res);

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
          QRhiSampler::ClampToEdge,
          QRhiSampler::ClampToEdge);

      sampler->setName("Node::sampler");
      sampler->create();
      m_samplers.push_back({sampler, m_rhiTex});
    }

    std::tie(m_vertexS, m_fragmentS) = score::gfx::makeShaders(
      renderer.state,images_vertex_shader, images_fragment_shader);
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
            renderer, rt, m_processUBO, nullptr, m_samplers);
        auto pipeline = buildPipeline(
            renderer, mesh, m_vertexS, m_fragmentS, rt, bindings);
        m_p.emplace_back(edge, pipeline);
      }
    }
  }

  int m_rotationCount = 0;
  void update(score::gfx::RenderList& renderer, QRhiResourceUpdateBatch& res, score::gfx::Edge* edge)
      override
  {
    auto& n = static_cast<const Node&>(this->node);
    // Update the process UBO (indicates timing)
    res.updateDynamicBuffer(
        m_processUBO,
        0,
        sizeof(score::gfx::ProcessUBO),
        &n.standardUBO);


    if (n.cameraChanged)
    {
      QVector3D pos(n.position[0], n.position[1], n.position[2]);
      QVector3D center(n.look_point[0], n.look_point[1], n.look_point[2]);
      raytracing.setCamera(pos, center, n.fov, n.projectionMode);
      n.cameraChanged = false; // Reset the flag
    }

    if (this->geometryChanged)  // n = static_cast<const Node&>(this->node);
    {
      if (!n.m_positions.empty())
      {
        raytracing.setPointCloud(n.m_positions, n.m_colors);
        m_isRtReady = true;
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
    if (m_isRtReady)
    {
      m_outputLayout = raytracing.render(m_inst, m_physDev, m_dev, m_devFuncs, m_funcs,
                              vkCmdBuf, m_output, m_outputLayout, m_outputView,
                              currentFrameSlot, m_pixelSize);
    }

    m_rhiTex->setNativeLayout(int(m_outputLayout));

    m_rhiTex->setNativeLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_outputLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    m_samplers[0].texture = m_rhiTex;

    // const auto& mesh = TexturedCube::instance();
    const auto& mesh = renderer.defaultQuad();
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
      ProcessNode::process(p, *val);
      if (lastIndex == val->meshes->dirty_index)
      {
        continue;
      }

      geometryChanged = true;
      lastIndex = val->meshes->dirty_index;

      qDebug() << "Received a new Mesh with size: " << val->meshes->dirty_index;
      m_positions.clear();
      m_colors.clear();

      for (const auto& geom : val->meshes->meshes)
      {
        if (geom.vertices <= 0 || geom.buffers.empty())
          continue;

        m_positions.clear();
        m_colors.clear();

        const int vertexCount = geom.vertices;

        // Iterate over attributes and extract positions/colors
        for (size_t i = 0; i < geom.attributes.size(); ++i)
        {
          const auto& attr = geom.attributes[i];
          const auto& in   = geom.input[i];

          // Find the buffer containing the data
          if (in.buffer < 0 || in.buffer >= static_cast<int>(geom.buffers.size()))
            continue;

          const auto& buf = geom.buffers[in.buffer];
          if (!buf.data || buf.size <= 0)
            continue;

          const char* base = static_cast<const char*>(buf.data.get()) + in.offset + attr.offset;

          switch (attr.location)
          {
            case halp::dynamic_geometry::attribute::position:
              if (static_cast<int>(attr.format) == static_cast<int>(halp::dynamic_geometry::attribute::float3))
              {
                for (int v = 0; v < vertexCount; ++v)
                {
                  const float* p = reinterpret_cast<const float*>(base + v * geom.bindings[attr.binding].stride);
                  m_positions.emplace_back(p[0], p[1], p[2], 1.0f);
                }
                qDebug() << "get point data with size: " << m_positions.size();
              }
              break;

            case halp::dynamic_geometry::attribute::color:
              if (static_cast<int>(attr.format) == static_cast<int>(halp::dynamic_geometry::attribute::float3))
              {
                for (int v = 0; v < vertexCount; ++v)
                {
                  const float* c = reinterpret_cast<const float*>(base + v * geom.bindings[attr.binding].stride);
                  m_colors.emplace_back(c[0], c[1], c[2], 1.0f);
                }
                qDebug() << "get color data with size: " << m_colors.size();
              }
              break;
            default:
              break;
          }
        }
      }
      p++;
    }
    else if (auto val = ossia::get_if<ossia::value>(&m))
    {
      switch(p)
      {
        case 1: // Position
          this->position = ossia::convert<ossia::vec3f>(*val);
          this->cameraChanged = true;
          break;
        case 2: // Look at Point
          this->look_point = ossia::convert<ossia::vec3f>(*val);
          this->cameraChanged = true;
          break;
        case 3: // FOV
          this->fov = ossia::convert<float>(*val);
          this->cameraChanged = true;
          break;
        case 4: // Projection Mode
          this->projectionMode = ossia::convert<int>(*val);
          this->cameraChanged = true;
          break;
      }
      p++;
    }
    else
    {
      p++;
    }
    }
  }
}

