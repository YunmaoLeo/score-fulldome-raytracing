#pragma once
#include <Gfx/Graph/Node.hpp>
#include <Gfx/Graph/RenderList.hpp>
#include <Gfx/Graph/CommonUBOs.hpp>

namespace vkfrt
{
class Renderer;
class Node : public score::gfx::NodeModel
{
public:
  Node();
  virtual ~Node();

  score::gfx::NodeRenderer*
  createRenderer(score::gfx::RenderList& r) const noexcept override;
  void process(score::gfx::Message&& msg) override;
private:
  score::gfx::ModelCameraUBO ubo;

  //store camera state
  ossia::vec3f position{ -15.0f, 6.0f, -35.75f };
  ossia::vec3f look_point{ 20.0f, 0.0f, -36.75f };
  float fov{ 60.f };
  int projectionMode;

  mutable bool cameraChanged = true;

  int lastIndex = -1;
  std::vector<QVector4D> m_positions;
  std::vector<QVector4D> m_colors;

  bool m_geometryDirty = false;

  friend Renderer;
  QImage m_image;
  QRhi* m_Rhi = nullptr;
};
}
