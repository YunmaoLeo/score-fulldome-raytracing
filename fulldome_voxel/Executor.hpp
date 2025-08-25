#pragma once
#include <Process/Execution/ProcessComponent.hpp>

#include <ossia/dataflow/node_process.hpp>

namespace vkfrt
{
class Model;
class ProcessExecutorComponent final
    : public Execution::ProcessComponent_T<vkfrt::Model, ossia::node_process>
{
  COMPONENT_METADATA("f40f3511-592a-4cab-bede-c3681f1f2dfb")
public:
  ProcessExecutorComponent(
      Model& element,
      const Execution::Context& ctx,
      QObject* parent);
};

using ProcessExecutorComponentFactory
    = Execution::ProcessComponentFactory_T<ProcessExecutorComponent>;
}
