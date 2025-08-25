// reference: https://github.com/alpqr/qvkrt

#include "vk_voxel_raytracing.hpp"

#include <QElapsedTimer>
#include <QDateTime>
#include "Gfx/GfxContext.hpp"
#include "score/gfx/Vulkan.hpp"

#include <QFile>
#include <QDebug>

#include <rhi/qrhi_platform.h>

struct VoxelUBO {
  QMatrix4x4 projInv;
  QMatrix4x4 viewInv;
  int projMode;
};


// ------------------------------------------------------------
// static cube template (used as single BLAS geometry)
// ------------------------------------------------------------
const float r = 0.01f; // a small half-extent for cube voxel
const float cube_verts_template[8 * 3] = {
  -r, -r, -r,   r, -r, -r,   r,  r, -r,  -r,  r, -r,
  -r, -r,  r,   r, -r,  r,   r,  r,  r,  -r,  r,  r
};
const uint32_t cube_indices_template[36] = {
  0, 1, 2, 2, 3, 0, // front
  1, 5, 6, 6, 2, 1, // right
  5, 4, 7, 7, 6, 5, // back
  4, 0, 3, 3, 7, 4, // left
  3, 2, 6, 6, 7, 3, // top
  4, 5, 1, 1, 0, 4  // bottom
};

template <class Int>
inline Int aligned(Int v, Int byteAlign)
{
    return (v + byteAlign - 1) & ~(byteAlign - 1);
}

// ------------------------------------------------------------
// recreate storage image used as raytracing output target
// ------------------------------------------------------------
void VkRayTracer::recreateTexture(int w, int h){
  // destroy previous rhi + vk image objects if any
  if (m_tex) {
    m_tex->destroy();
    m_tex = nullptr;
  }

  if (m_vkImgView) {
    m_df->vkDestroyImageView(m_device, m_vkImgView, nullptr);
    m_vkImgView = VK_NULL_HANDLE;
  }

  if (m_vkImg) {
    m_df->vkDestroyImage(m_device, m_vkImg, nullptr);
    m_vkImg = VK_NULL_HANDLE;
  }

  if (m_vkImgMem) {
    m_df->vkFreeMemory(m_device, m_vkImgMem, nullptr);
    m_vkImgMem = VK_NULL_HANDLE;
  }

  // create a storage-capable rgba8 image
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent.width = w;
  imageInfo.extent.height = h;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkResult res = m_df->vkCreateImage(m_device, &imageInfo, nullptr, &m_vkImg);
  Q_ASSERT(res == VK_SUCCESS);

  // allocate device-local memory for image
  VkMemoryRequirements memReq;
  m_df->vkGetImageMemoryRequirements(m_device, m_vkImg, &memReq);

  VkMemoryAllocateInfo memAlloc = {};
  memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memAlloc.allocationSize = memReq.size;

  VkPhysicalDeviceMemoryProperties memProps;
  m_f->vkGetPhysicalDeviceMemoryProperties(m_physDev, &memProps);

  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((memReq.memoryTypeBits & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      memAlloc.memoryTypeIndex = i;
      break;
    }
  }

  res = m_df->vkAllocateMemory(m_device, &memAlloc, nullptr, &m_vkImgMem);
  Q_ASSERT(res == VK_SUCCESS);
  res = m_df->vkBindImageMemory(m_device, m_vkImg, m_vkImgMem, 0);
  Q_ASSERT(res == VK_SUCCESS);

  // create view for shader access
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_vkImg;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.layerCount = 1;

  res = m_df->vkCreateImageView(m_device, &viewInfo, nullptr, &m_vkImgView);
  Q_ASSERT(res == VK_SUCCESS);

  // wrap the native image in qrhi texture for easy sampling later
  QRhiTexture::NativeTexture nativeTex = {};
  nativeTex.object = quint64(m_vkImg);
  nativeTex.layout = QRhiTexture::UsedWithLoadStore;

  m_tex = m_rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1, QRhiTexture::UsedWithLoadStore);
  m_tex->createFrom(nativeTex);

  m_size = QSize(w, h);
}

// ------------------------------------------------------------
// one-time device function pointers + pools and ubos
// ------------------------------------------------------------
void VkRayTracer::init(VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df)
{
    // query ray tracing pipeline properties (sbt stride, handle size, etc.)
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps = {};
    rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 deviceProperties2 = {};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &rtProps;
    f->vkGetPhysicalDeviceProperties2(physDev, &deviceProperties2);

    qDebug() << "start initialize RayTracer";
    qDebug() << "shaderGroupHandleSize" << rtProps.shaderGroupHandleSize
             << "maxRayRecursionDepth" << rtProps.maxRayRecursionDepth
             << "maxShaderGroupStride" << rtProps.maxShaderGroupStride
             << "shaderGroupBaseAlignment" << rtProps.shaderGroupBaseAlignment
             << "maxRayDispatchInvocationCount" << rtProps.maxRayDispatchInvocationCount
             << "shaderGroupHandleAlignment" << rtProps.shaderGroupHandleAlignment
             << "maxRayHitAttributeSize" << rtProps.maxRayHitAttributeSize;

    m_rtProps = rtProps;

    // query acceleration structure feature flags
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = {};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &asFeatures;
    f->vkGetPhysicalDeviceFeatures2(physDev, &deviceFeatures2);

    qDebug() << "features: accelerationStructure" << asFeatures.accelerationStructure
             << "accelerationStructureIndirectBuild" << asFeatures.accelerationStructureIndirectBuild
             << "accelerationStructureHostCommands" << asFeatures.accelerationStructureHostCommands
             << "descriptorBindingAccelerationStructureUpdateAfterBind" << asFeatures.descriptorBindingAccelerationStructureUpdateAfterBind;

    m_asFeatures = asFeatures;

    // load khr rt device functions
    vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(f->vkGetDeviceProcAddr(dev, "vkGetBufferDeviceAddressKHR"));
    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(f->vkGetDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR"));
    vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(f->vkGetDeviceProcAddr(dev, "vkBuildAccelerationStructuresKHR"));
    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(f->vkGetDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR"));
    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(f->vkGetDeviceProcAddr(dev, "vkDestroyAccelerationStructureKHR"));
    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(f->vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR"));
    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(f->vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureDeviceAddressKHR"));
    vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(f->vkGetDeviceProcAddr(dev, "vkCmdTraceRaysKHR"));
    vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(f->vkGetDeviceProcAddr(dev, "vkGetRayTracingShaderGroupHandlesKHR"));
    vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(f->vkGetDeviceProcAddr(dev, "vkCreateRayTracingPipelinesKHR"));

    // descriptor pool for as/image/ubo/ssbo
    static const VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, FRAMES_IN_FLIGHT },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FRAMES_IN_FLIGHT },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FRAMES_IN_FLIGHT },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FRAMES_IN_FLIGHT }
    };
    VkDescriptorPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = FRAMES_IN_FLIGHT * 1024; // keep a lot of headroom
    poolCreateInfo.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);
    poolCreateInfo.pPoolSizes = poolSizes;
    df->vkCreateDescriptorPool(dev, &poolCreateInfo, nullptr, &m_descPool);

    // per-frame uniform buffers (projInv + viewInv)
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
        m_uniformBuffers[i] = createHostVisibleBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, physDev, dev, f, df, 2 * 64 + 4);

    m_lastOutputImageView = VK_NULL_HANDLE;

    m_device  = dev;
    m_f       = f;
    m_df      = df;
    m_physDev = physDev;
}

static const char entryPoint[] = "main";

// ------------------------------------------------------------
// helper to load spir-v and create shader stage info
// ------------------------------------------------------------
static VkPipelineShaderStageCreateInfo getShader(const QString &name, VkShaderStageFlagBits stage, VkDevice dev, QVulkanDeviceFunctions *df)
{
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("Failed to open %s", qPrintable(name));
    const QByteArray data = f.readAll();

    VkShaderModuleCreateInfo shaderInfo = {};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = data.size();
    shaderInfo.pCode = reinterpret_cast<const quint32 *>(data.constData());
    VkShaderModule module;
    df->vkCreateShaderModule(dev, &shaderInfo, nullptr, &module);

    VkPipelineShaderStageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module;
    info.pName = entryPoint;

    return info;
}

// ------------------------------------------------------------
// main render entry: performs one-time setup and per-frame dispatch
// ------------------------------------------------------------
VkImageLayout VkRayTracer::render(QVulkanInstance *inst,
                               VkPhysicalDevice physDev,
                               VkDevice dev,
                               QVulkanDeviceFunctions *df,
                               QVulkanFunctions *f,
                               VkCommandBuffer cb,
                               VkImage outputImage,
                               VkImageLayout currentOutputImageLayout,
                               VkImageView outputImageView,
                               uint currentFrameSlot,
                               const QSize &pixelSize)
{
  // setup path: first frame or render target view changed
  if (!m_vertexBuffer.buf || (m_lastOutputImageView && m_lastOutputImageView != outputImageView)) {
      qDebug("ray tracing setup");
      m_lastOutputImageView = outputImageView;

      QElapsedTimer timer;
      timer.start();
      qDebug() << "[TIMESTAMP] setup started at: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

      // use a single cube mesh for the BLAS
      std::vector<float>    all_vertices(cube_verts_template,   cube_verts_template + 24);
      std::vector<uint32_t> all_indices (cube_indices_template, cube_indices_template + 36);
      qDebug() << "using single cube template with" << all_vertices.size() / 3 << "vertices and" << all_indices.size() << "indices for BLAS.";

      // upload cube mesh to gpu
      m_vertexBuffer = createHostVisibleBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                               physDev, dev, f, df, all_vertices.size() * sizeof(float));
      updateHostData(m_vertexBuffer, dev, df, all_vertices.data(), all_vertices.size() * sizeof(float));

      m_indexBuffer = createHostVisibleBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                              physDev, dev, f, df, all_indices.size() * sizeof(uint32_t));
      updateHostData(m_indexBuffer, dev, df, all_indices.data(), all_indices.size() * sizeof(uint32_t));

      // color buffer (one vec4 per point)
      std::vector<QVector4D> all_colors;
      all_colors.reserve(m_point_colors.size());
      for (size_t i = 0; i < m_point_colors.size(); ++i)
        all_colors.push_back(m_point_colors[i]);

      m_colorBuffer = createHostVisibleBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              physDev, dev, f, df, all_colors.size() * sizeof(QVector4D));
      updateHostData(m_colorBuffer, dev, df, all_colors.data(), all_colors.size() * sizeof(QVector4D));

      // --------------------------------------------------------
      // build BLAS: triangles (single cube)
      // --------------------------------------------------------
      qDebug() << "[TIMESTAMP] BLAS creation started at" << timer.elapsed() << "ms.";
      VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress = {};
      vertexBufferDeviceAddress.deviceAddress = m_vertexBuffer.addr;
      VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress = {};
      indexBufferDeviceAddress.deviceAddress = m_indexBuffer.addr;

      VkAccelerationStructureGeometryKHR asGeom = {};
      asGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
      asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // keep opaque for fastest path
      asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
      asGeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
      asGeom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
      asGeom.geometry.triangles.vertexData = vertexBufferDeviceAddress;
      asGeom.geometry.triangles.vertexStride = 3 * sizeof(float);
      asGeom.geometry.triangles.maxVertex = (all_vertices.size() / 3) - 1;
      asGeom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
      asGeom.geometry.triangles.indexData = indexBufferDeviceAddress;

      VkAccelerationStructureBuildGeometryInfoKHR asBuildGeomInfo = {};
      asBuildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
      asBuildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      asBuildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; // you can swap to FAST_BUILD if startup matters more
      asBuildGeomInfo.geometryCount = 1;
      asBuildGeomInfo.pGeometries = &asGeom;

      const uint32_t primitiveCountPerGeometry = static_cast<uint32_t>(all_indices.size() / 3);
      VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
      sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
      vkGetAccelerationStructureBuildSizesKHR(dev,
                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &asBuildGeomInfo,
                                              &primitiveCountPerGeometry,
                                              &sizeInfo);

      qDebug() << "blas buffer size" << sizeInfo.accelerationStructureSize;
      m_blasBuffer = createASBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                    physDev, dev, f, df, sizeInfo.accelerationStructureSize);

      VkAccelerationStructureCreateInfoKHR asCreateInfo = {};
      asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
      asCreateInfo.buffer = m_blasBuffer.buf;
      asCreateInfo.size = sizeInfo.accelerationStructureSize;
      asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      vkCreateAccelerationStructureKHR(dev, &asCreateInfo, nullptr, &m_blas);

      qDebug() << "blas scratch buffer size" << sizeInfo.buildScratchSize;
      Buffer scratchBLAS = createASBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          physDev, dev, f, df, sizeInfo.buildScratchSize);

      memset(&asBuildGeomInfo, 0, sizeof(asBuildGeomInfo));
      asBuildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
      asBuildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      asBuildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      asBuildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      asBuildGeomInfo.dstAccelerationStructure = m_blas;
      asBuildGeomInfo.geometryCount = 1;
      asBuildGeomInfo.pGeometries = &asGeom;
      asBuildGeomInfo.scratchData.deviceAddress = scratchBLAS.addr;

      VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo = {};
      asBuildRangeInfo.primitiveCount = primitiveCountPerGeometry;
      asBuildRangeInfo.primitiveOffset = 0;
      asBuildRangeInfo.firstVertex = 0;
      asBuildRangeInfo.transformOffset = 0;

      VkAccelerationStructureBuildRangeInfoKHR *rangeInfo = &asBuildRangeInfo;

      // record build on command buffer (nvidia typically reports no host build)
      vkCmdBuildAccelerationStructuresKHR(cb, 1, &asBuildGeomInfo, &rangeInfo);

      // get device address for this BLAS
      VkAccelerationStructureDeviceAddressInfoKHR asAddrInfo = {};
      asAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
      asAddrInfo.accelerationStructure = m_blas;
      m_blasAddr = vkGetAccelerationStructureDeviceAddressKHR(dev, &asAddrInfo);

      // barrier to make blas build visible before tlas build
      {
        VkMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        const VkAccessFlags accelAccess = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        memoryBarrier.srcAccessMask = accelAccess;
        memoryBarrier.dstAccessMask = accelAccess;
        df->vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
      }

      // --------------------------------------------------------
      // build TLAS: instance the single cube for each point
      // --------------------------------------------------------
      qDebug() << "[TIMESTAMP] TLAS creation started at" << timer.elapsed() << "ms.";

      // create instances array with per-instance translate (and custom index)
      std::vector<VkAccelerationStructureInstanceKHR> instances;
      instances.reserve(m_point_positions.size());

      const float scale = 5.f; // your scene scale
      for (size_t i = 0; i < m_point_positions.size(); ++i) {
        const auto& pos = m_point_positions[i];

        VkAccelerationStructureInstanceKHR instance = {};
        QMatrix4x4 instanceTransform; // identity
        instanceTransform.translate(pos.x() * scale, pos.y() * scale, pos.z() * scale);

        // vulkan wants 3x4 row-major; qmatrix4x4 is column-major → transpose then copy 12 floats
        instanceTransform = instanceTransform.transposed();
        memcpy(instance.transform.matrix, instanceTransform.constData(), 12 * sizeof(float));

        instance.instanceCustomIndex = static_cast<uint32_t>(i); // used in rchit via gl_InstanceCustomIndexEXT
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = m_blasAddr;

        instances.push_back(instance);
      }
      qDebug() << "created" << instances.size() << "instances for the tlas build.";

      // upload instances
      m_instanceBuffer = createHostVisibleBuffer(
          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
          physDev, dev, f, df,
          static_cast<uint32_t>(instances.size() * sizeof(VkAccelerationStructureInstanceKHR)));
      updateHostData(m_instanceBuffer, dev, df, instances.data(),
                     instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

      VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress = {};
      instanceDataDeviceAddress.deviceAddress = m_instanceBuffer.addr;

      VkAccelerationStructureGeometryKHR asGeomTLAS = {};
      asGeomTLAS.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
      asGeomTLAS.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
      asGeomTLAS.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
      asGeomTLAS.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
      asGeomTLAS.geometry.instances.arrayOfPointers = VK_FALSE;
      asGeomTLAS.geometry.instances.data = instanceDataDeviceAddress;

      VkAccelerationStructureBuildGeometryInfoKHR asBuildGeomInfoTLAS = {};
      asBuildGeomInfoTLAS.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
      asBuildGeomInfoTLAS.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      asBuildGeomInfoTLAS.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      asBuildGeomInfoTLAS.geometryCount = 1;
      asBuildGeomInfoTLAS.pGeometries = &asGeomTLAS;

      const uint32_t tlasCount = static_cast<uint32_t>(instances.size());
      VkAccelerationStructureBuildSizesInfoKHR sizeInfoTLAS = {};
      sizeInfoTLAS.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
      vkGetAccelerationStructureBuildSizesKHR(dev,
                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &asBuildGeomInfoTLAS,
                                              &tlasCount,
                                              &sizeInfoTLAS);

      qDebug() << "tlas buffer size" << sizeInfoTLAS.accelerationStructureSize;
      m_tlasBuffer = createASBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                    physDev, dev, f, df, sizeInfoTLAS.accelerationStructureSize);

      VkAccelerationStructureCreateInfoKHR asCreateInfoTLAS = {};
      asCreateInfoTLAS.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
      asCreateInfoTLAS.buffer = m_tlasBuffer.buf;
      asCreateInfoTLAS.size = sizeInfoTLAS.accelerationStructureSize;
      asCreateInfoTLAS.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      vkCreateAccelerationStructureKHR(dev, &asCreateInfoTLAS, nullptr, &m_tlas);

      qDebug() << "tlas scratch buffer size" << sizeInfoTLAS.buildScratchSize;
      Buffer scratchTLAS = createASBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          physDev, dev, f, df, sizeInfoTLAS.buildScratchSize);

      memset(&asBuildGeomInfoTLAS, 0, sizeof(asBuildGeomInfoTLAS));
      asBuildGeomInfoTLAS.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
      asBuildGeomInfoTLAS.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      asBuildGeomInfoTLAS.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      asBuildGeomInfoTLAS.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      asBuildGeomInfoTLAS.dstAccelerationStructure = m_tlas;
      asBuildGeomInfoTLAS.geometryCount = 1;
      asBuildGeomInfoTLAS.pGeometries = &asGeomTLAS;
      asBuildGeomInfoTLAS.scratchData.deviceAddress = scratchTLAS.addr;

      VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfoTLAS = {};
      asBuildRangeInfoTLAS.primitiveCount = tlasCount;
      asBuildRangeInfoTLAS.primitiveOffset = 0;
      asBuildRangeInfoTLAS.firstVertex = 0;
      asBuildRangeInfoTLAS.transformOffset = 0;

      VkAccelerationStructureBuildRangeInfoKHR *rangeInfoTLAS = &asBuildRangeInfoTLAS;
      vkCmdBuildAccelerationStructuresKHR(cb, 1, &asBuildGeomInfoTLAS, &rangeInfoTLAS);

      // fetch tlas device address
      VkAccelerationStructureDeviceAddressInfoKHR asAddrInfoTLAS = {};
      asAddrInfoTLAS.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
      asAddrInfoTLAS.accelerationStructure = m_tlas;
      m_tlasAddr = vkGetAccelerationStructureDeviceAddressKHR(dev, &asAddrInfoTLAS);

      qDebug() << "[TIMESTAMP] TLAS creation finished at" << timer.elapsed() << "ms.";

      // --------------------------------------------------------
      // pipeline (rgen/rmiss/rchit), sbt, descriptors
      // --------------------------------------------------------
      // descriptor set layout: 0=tlas, 1=output image, 2=ubo, 3=colors
      VkDescriptorSetLayoutBinding asLayoutBinding = {};
      asLayoutBinding.binding = 0;
      asLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      asLayoutBinding.descriptorCount = 1;
      asLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

      VkDescriptorSetLayoutBinding outputLayoutBinding = {};
      outputLayoutBinding.binding = 1;
      outputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      outputLayoutBinding.descriptorCount = 1;
      outputLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

      VkDescriptorSetLayoutBinding ubLayoutBinding = {};
      ubLayoutBinding.binding = 2;
      ubLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      ubLayoutBinding.descriptorCount = 1;
      ubLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

      VkDescriptorSetLayoutBinding colorLayoutBinding = {};
      colorLayoutBinding.binding = 3;
      colorLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      colorLayoutBinding.descriptorCount = 1;
      colorLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      const VkDescriptorSetLayoutBinding bindings[4] = {
          asLayoutBinding,
          outputLayoutBinding,
          ubLayoutBinding,
          colorLayoutBinding,
      };

      VkDescriptorSetLayoutCreateInfo descSetLayoutCreateInfo = {};
      descSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      descSetLayoutCreateInfo.bindingCount = 4;
      descSetLayoutCreateInfo.pBindings = bindings;
      df->vkCreateDescriptorSetLayout(dev, &descSetLayoutCreateInfo, nullptr, &m_descSetLayout);

      VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
      pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      pipelineLayoutCreateInfo.setLayoutCount = 1;
      pipelineLayoutCreateInfo.pSetLayouts = &m_descSetLayout;
      df->vkCreatePipelineLayout(dev, &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout);

      VkPipelineShaderStageCreateInfo stages[3] = {
          getShader(":/shaders/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR, dev, df),
          getShader(":/shaders/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR, dev, df),
          getShader(":/shaders/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, dev, df)
      };

      VkRayTracingShaderGroupCreateInfoKHR shaderGroups[3];
      {
        // rgen group
        VkRayTracingShaderGroupCreateInfoKHR g = {};
        g.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        g.generalShader = 0;
        g.closestHitShader = VK_SHADER_UNUSED_KHR;
        g.anyHitShader = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[0] = g;

        // rmiss group
        g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        g.generalShader = 1;
        g.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[1] = g;

        // triangles hit group
        g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        g.generalShader = VK_SHADER_UNUSED_KHR;
        g.closestHitShader = 2;
        g.anyHitShader = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[2] = g;
      }

      VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo = {};
      pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
      pipelineCreateInfo.stageCount = 3;
      pipelineCreateInfo.pStages = stages;
      pipelineCreateInfo.groupCount = 3;
      pipelineCreateInfo.pGroups = shaderGroups;
      pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
      pipelineCreateInfo.layout = m_pipelineLayout;
      vkCreateRayTracingPipelinesKHR(dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_pipeline);

      // shader binding table (rgen, miss, hit)
      const uint32_t handleSize = m_rtProps.shaderGroupHandleSize;
      const uint32_t handleSizeAligned = aligned(handleSize, m_rtProps.shaderGroupHandleAlignment);
      const uint32_t groupSize = 3;
      const uint32_t handleListByteSize = groupSize * handleSize;

      std::vector<uint8_t> handles(handleListByteSize);
      vkGetRayTracingShaderGroupHandlesKHR(dev, m_pipeline, 0, groupSize, handleListByteSize, handles.data());

      // sbt entry stride must honor handle alignment and base alignment
      const uint32_t sbtBufferEntrySize = aligned(handleSizeAligned, m_rtProps.shaderGroupBaseAlignment);
      const uint32_t sbtBufferSize = groupSize * sbtBufferEntrySize;

      m_sbt = createHostVisibleBuffer(VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
                                      physDev, dev, f, df, sbtBufferSize);
      std::vector<uint8_t> sbtBufData(sbtBufferSize);
      for (uint32_t i = 0; i < groupSize; ++i)
        memcpy(sbtBufData.data() + i * sbtBufferEntrySize, handles.data() + i * handleSize, handleSize);
      updateHostData(m_sbt, dev, df, sbtBufData.data(), sbtBufferSize);

      // allocate and update descriptor sets for each frame-in-flight
      VkDescriptorSetAllocateInfo descSetAllocInfo = {};
      descSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descSetAllocInfo.descriptorPool = m_descPool;
      descSetAllocInfo.descriptorSetCount = 1;
      descSetAllocInfo.pSetLayouts = &m_descSetLayout;

      for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        df->vkAllocateDescriptorSets(dev, &descSetAllocInfo, &m_descSets[i]);

        // binding 0: tlas
        VkWriteDescriptorSetAccelerationStructureKHR descSetAS = {};
        descSetAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        descSetAS.accelerationStructureCount = 1;
        descSetAS.pAccelerationStructures = &m_tlas;

        VkWriteDescriptorSet asWrite = {};
        asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        asWrite.pNext = &descSetAS;
        asWrite.dstSet = m_descSets[i];
        asWrite.dstBinding = 0;
        asWrite.descriptorCount = 1;
        asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        // binding 1: storage image (raytracing output)
        VkDescriptorImageInfo descOutputImage = {};
        descOutputImage.imageView = outputImageView;
        descOutputImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet imageWrite = {};
        imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imageWrite.dstSet = m_descSets[i];
        imageWrite.dstBinding = 1;
        imageWrite.descriptorCount = 1;
        imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        imageWrite.pImageInfo = &descOutputImage;

        // binding 2: uniform buffer (projInv + viewInv)
        VkDescriptorBufferInfo descUniformBuffer = {};
        descUniformBuffer.buffer = m_uniformBuffers[i].buf;
        descUniformBuffer.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet ubWrite = {};
        ubWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ubWrite.dstSet = m_descSets[i];
        ubWrite.dstBinding = 2;
        ubWrite.descriptorCount = 1;
        ubWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubWrite.pBufferInfo = &descUniformBuffer;

        // binding 3: color buffer (vec4 per point)
        VkDescriptorBufferInfo colorBufferInfo = { m_colorBuffer.buf, 0, m_colorBuffer.size };

        VkWriteDescriptorSet colorWrite = {};
        colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        colorWrite.dstSet = m_descSets[i];
        colorWrite.dstBinding = 3;
        colorWrite.descriptorCount = 1;
        colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        colorWrite.pBufferInfo = &colorBufferInfo;

        VkWriteDescriptorSet writeSets[] = { asWrite, imageWrite, ubWrite, colorWrite };
        df->vkUpdateDescriptorSets(dev, 4, writeSets, 0, VK_NULL_HANDLE);
      }

      qDebug() << "[TIMESTAMP] full setup finished in " << timer.elapsed() << "ms.";
  }

  // ----------------------------------------------------------
  // per-frame: image layout transition for storage write
  // ----------------------------------------------------------
  {
      VkImageMemoryBarrier barrier = {};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;
      barrier.oldLayout = currentOutputImageLayout;
      barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      barrier.image = outputImage;

      df->vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                               0, 0, nullptr, 0, nullptr,
                               1, &barrier);
  }

  // ----------------------------------------------------------
  // per-frame: update camera (view/proj) and upload to ubo
  // ----------------------------------------------------------
  {
    QVector3D upVector = QVector3D(0.0f, 1.0f, 0.0f); // world up
    m_view.setToIdentity();
    m_view.lookAt(m_cameraPosition, m_cameraCenter, upVector);

    m_proj.setToIdentity();
    m_proj.perspective(m_fov, float(pixelSize.width()) / pixelSize.height(), 0.1f, 512.0f);

    m_projInv = m_proj.inverted();
    m_viewInv = m_view.inverted();

    uchar ubData[136];
    memcpy(ubData,        m_projInv.constData(), 64);
    memcpy(ubData + 64,   m_viewInv.constData(), 64);
    memcpy(ubData + 128, &m_fov, 4);
    memcpy(ubData + 132, &m_projectionMode,4);

    updateHostData(m_uniformBuffers[currentFrameSlot], dev, df, ubData, 136);
  }

  // ----------------------------------------------------------
  // per-frame: bind pipeline + descriptors and trace rays
  // ----------------------------------------------------------
  {
    const uint32_t handleSize = m_rtProps.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = aligned(handleSize, m_rtProps.shaderGroupHandleAlignment);
    const uint32_t sbtBufferEntrySize = aligned(handleSizeAligned, m_rtProps.shaderGroupBaseAlignment);

    VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry = {};
    raygenShaderSbtEntry.deviceAddress = m_sbt.addr;
    raygenShaderSbtEntry.stride = handleSizeAligned;
    raygenShaderSbtEntry.size = handleSize;

    VkStridedDeviceAddressRegionKHR missShaderSbtEntry = {};
    missShaderSbtEntry.deviceAddress = m_sbt.addr + sbtBufferEntrySize;
    missShaderSbtEntry.stride = handleSizeAligned;
    missShaderSbtEntry.size = handleSize;

    VkStridedDeviceAddressRegionKHR hitShaderSbtEntry = {};
    hitShaderSbtEntry.deviceAddress = m_sbt.addr + sbtBufferEntrySize * 2;
    hitShaderSbtEntry.stride = handleSizeAligned;
    hitShaderSbtEntry.size = handleSize;

    VkStridedDeviceAddressRegionKHR callableShaderSbtEntry = {};

    df->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
    df->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                m_pipelineLayout, 0, 1, &m_descSets[currentFrameSlot], 0, nullptr);

    vkCmdTraceRaysKHR(cb,
                      &raygenShaderSbtEntry,
                      &missShaderSbtEntry,
                      &hitShaderSbtEntry,
                      &callableShaderSbtEntry,
                      pixelSize.width(), pixelSize.height(), 1);
  }

  // ----------------------------------------------------------
  // per-frame: transition to shader-read for post use
  // ----------------------------------------------------------
  {
      VkImageMemoryBarrier barrier = {};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;
      barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.image = outputImage;

      df->vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0, 0, nullptr, 0, nullptr,
                               1, &barrier);
  }

  return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// ------------------------------------------------------------
// update point cloud buffers (positions feed instances; colors feed SSBO)
// ------------------------------------------------------------
void VkRayTracer::setPointCloud(const std::vector<QVector4D>& positions, const std::vector<QVector4D>& colors){
  if (positions.empty())
  {
    qDebug() << "point cloud data is not valid";
    return;
  }

  m_pointCount = positions.size();
  m_point_positions = positions;
  m_point_colors = colors;

  qDebug() << "[RayTracer] update point cloud successfully, number:" << m_pointCount;
}

// ------------------------------------------------------------
// update camera params for per-frame lookAt + perspective
// ------------------------------------------------------------
void VkRayTracer::setCamera(const QVector3D& position, const QVector3D& center, float fov, int projectionMode){
  m_cameraPosition = position;
  m_cameraCenter   = center;
  m_fov            = fov;
  m_projectionMode = projectionMode;
}

// ------------------------------------------------------------
// buffer helpers (as / host-visible / update / free / address)
// ------------------------------------------------------------
VkRayTracer::Buffer VkRayTracer::createASBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size)
{
    // usage = storage buffer or acceleration structure storage
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBuffer buf = VK_NULL_HANDLE;
    df->vkCreateBuffer(dev, &bufferCreateInfo, nullptr, &buf);

    VkMemoryRequirements memReq = {};
    df->vkGetBufferMemoryRequirements(dev, buf, &memReq);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memReq.size;

    quint32 memIndex = UINT_MAX;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    f->vkGetPhysicalDeviceMemoryProperties(physDev, &physDevMemProps);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (!(memReq.memoryTypeBits & (1 << i))) continue;
        if (physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            memIndex = i;
            break;
        }
    }
    if (memIndex == UINT_MAX)
        qFatal("No suitable memory type");

    memoryAllocateInfo.memoryTypeIndex = memIndex;

    VkDeviceMemory bufMem = VK_NULL_HANDLE;
    df->vkAllocateMemory(dev, &memoryAllocateInfo, nullptr, &bufMem);
    df->vkBindBufferMemory(dev, buf, bufMem, 0);

    Buffer result { buf, bufMem, 0, size };
    result.addr = getBufferDeviceAddress(dev, result);
    return result;
}

VkRayTracer::Buffer VkRayTracer::createHostVisibleBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size)
{
    // usage = build-read-only / sbt / ubo / etc., mapped on host for uploads
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBuffer buf = VK_NULL_HANDLE;
    df->vkCreateBuffer(dev, &bufferCreateInfo, nullptr, &buf);

    VkMemoryRequirements memReq = {};
    df->vkGetBufferMemoryRequirements(dev, buf, &memReq);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memReq.size;

    quint32 memIndex = UINT_MAX;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    f->vkGetPhysicalDeviceMemoryProperties(physDev, &physDevMemProps);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (!(memReq.memoryTypeBits & (1 << i))) continue;
        if ((physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (physDevMemProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            memIndex = i;
            break;
        }
    }
    if (memIndex == UINT_MAX)
        qFatal("No suitable memory type");

    memoryAllocateInfo.memoryTypeIndex = memIndex;

    VkDeviceMemory bufMem = VK_NULL_HANDLE;
    auto res = df->vkAllocateMemory(dev, &memoryAllocateInfo, nullptr, &bufMem);
    Q_ASSERT(res == VK_SUCCESS);
    df->vkBindBufferMemory(dev, buf, bufMem, 0);

    Buffer result { buf, bufMem, 0, size };
    result.addr = getBufferDeviceAddress(dev, result);
    return result;
}

void VkRayTracer::updateHostData(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df, const void *data, size_t dataLen)
{
    // note: assumes b.size >= dataLen
    void *p = nullptr;
    df->vkMapMemory(dev, b.mem, 0, b.size, 0, &p);
    memcpy(p, data, dataLen);
    df->vkUnmapMemory(dev, b.mem);
}

void VkRayTracer::freeBuffer(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df)
{
    // release buffer + memory
    df->vkDestroyBuffer(dev, b.buf, nullptr);
    df->vkFreeMemory(dev, b.mem, nullptr);
}

VkDeviceAddress VkRayTracer::getBufferDeviceAddress(VkDevice dev, const Buffer &b)
{
    VkBufferDeviceAddressInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = b.buf;
    return vkGetBufferDeviceAddressKHR(dev, &info);
}
