#include "Executor.hpp"

#include <Gfx/GfxApplicationPlugin.hpp>
#include <Gfx/GfxContext.hpp>
#include <Gfx/Graph/PhongNode.hpp>
#include <Gfx/TexturePort.hpp>
#include <Process/Dataflow/Port.hpp>
#include <Process/ExecutionContext.hpp>

#include <score/document/DocumentContext.hpp>

#include <ossia/dataflow/port.hpp>

#include <fulldome_voxel/Node.hpp>
#include <fulldome_voxel/Process.hpp>
namespace vkfrt
{
class mesh_node final : public Gfx::gfx_exec_node
{
public:
  mesh_node(Gfx::GfxExecutionAction& ctx)
      : gfx_exec_node{ctx}
  {
  }

  void init()
  {
    auto n = std::make_unique<vkfrt::Node>();
    id = exec_context->ui->register_node(std::move(n));
  }

  ~mesh_node() { exec_context->ui->unregister_node(id); }

  std::string label() const noexcept override { return "vkfrt"; }
};

ProcessExecutorComponent::ProcessExecutorComponent(
    vkfrt::Model& element,
    const Execution::Context& ctx,
    QObject* parent)
    : ProcessComponent_T{element, ctx, "vkfrtExecutorComponent", parent}
{
  try
  {
    auto n = std::make_shared<mesh_node>(
        ctx.doc.plugin<Gfx::DocumentPlugin>().exec);

    n->root_outputs().push_back(new ossia::texture_outlet);
    n->root_inputs().push_back(new ossia::geometry_inlet);

    for(std::size_t i = 1; i <= 4; i++)
    {
      auto ctrl = qobject_cast<Process::ControlInlet*>(element.inlets()[i]);
      auto& p = n->add_control();
      ctrl->setupExecution(*n->root_inputs().back(), this);
      p->value = ctrl->value();

      QObject::connect(
          ctrl,
          &Process::ControlInlet::valueChanged,
          this,
          Gfx::con_unvalidated{ctx, i, 0, n});
    }

    n->init();
    this->node = n;
    m_ossia_process = std::make_shared<ossia::node_process>(n);
  }
  catch (...)
  {
  }
}
}
