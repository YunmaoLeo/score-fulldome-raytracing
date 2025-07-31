#pragma once
#include <Gfx/Graph/Node.hpp>
#include <Gfx/Graph/RenderList.hpp>
#include <Gfx/Graph/CommonUBOs.hpp>

namespace QVKRT
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

  int lastIndex = -1;

  std::vector<QVector4D> m_positions;
  std::vector<QVector4D> m_colors;

  bool m_geometryDirty = false;

  friend Renderer;
  QImage m_image;
  QRhi* m_Rhi = nullptr;
};
}
