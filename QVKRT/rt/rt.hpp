//reference: https://github.com/alpqr/qvkrt

#ifndef RT_H
#define RT_H

#include <QVulkanFunctions>
#include <QSize>
#include <QMatrix4x4>



class QRhi;
class QRhiCommandBuffer;
class QRhiTexture;
struct QRhiVulkanNativeHandles;

class RayTracer
{
public:
    void init(VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df);

    VkImageLayout doIt(QVulkanInstance *inst,
                       VkPhysicalDevice physDev,
                       VkDevice dev,
                       QVulkanDeviceFunctions *df,
                       QVulkanFunctions *f,
                       VkCommandBuffer cb,
                       VkImage outputImage,
                       VkImageLayout currentOutputImageLayout,
                       VkImageView outputImageView,
                       uint currentFrameSlot,
                       const QSize &pixelSize);

    void setPointCloud(const std::vector<QVector4D>& positions,
    const std::vector<QVector4D>& colors);
private:
    QRhiTexture* m_tex = nullptr;
    QSize m_size;
    VkImage m_vkImg = VK_NULL_HANDLE;
    VkImageView m_vkImgView = VK_NULL_HANDLE;

    QRhi* m_rhi = nullptr;
    QVulkanFunctions* m_f = nullptr;
    QVulkanDeviceFunctions* m_df = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDeviceMemory m_vkImgMem = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
    QVulkanInstance* m_inst = nullptr;

    void recreateTexture(int w, int h);


    static const int FRAMES_IN_FLIGHT = 2;

    struct Buffer {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceAddress addr = 0;
        size_t size = 0;
    };

    Buffer createASBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size);
    Buffer createHostVisibleBuffer(int usage, VkPhysicalDevice physDev, VkDevice dev, QVulkanFunctions *f, QVulkanDeviceFunctions *df, uint32_t size);
    void updateHostData(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df, const void *data, size_t dataLen);
    void freeBuffer(const Buffer &b, VkDevice dev, QVulkanDeviceFunctions *df);
    VkDeviceAddress getBufferDeviceAddress(VkDevice dev, const Buffer &b);

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProps;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_asFeatures;

    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;

    Buffer m_vertexBuffer;
    Buffer m_indexBuffer;
    Buffer m_transformBuffer;
    Buffer m_blasBuffer;
    VkAccelerationStructureKHR m_blas;
    VkDeviceAddress m_blasAddr = 0;

    Buffer m_instanceBuffer;
    Buffer m_tlasBuffer;
    VkAccelerationStructureKHR m_tlas;
    VkDeviceAddress m_tlasAddr = 0;

    Buffer m_uniformBuffers[FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout m_descSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;
    Buffer m_sbt;
    VkDescriptorPool m_descPool;
    VkDescriptorSet m_descSets[FRAMES_IN_FLIGHT];

    QMatrix4x4 m_proj;
    QMatrix4x4 m_projInv;
    QMatrix4x4 m_view;
    QMatrix4x4 m_viewInv;

    VkImageView m_lastOutputImageView;
};

#endif
