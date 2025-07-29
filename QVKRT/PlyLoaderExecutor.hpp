#pragma once
#include <Process/Execution/ProcessComponent.hpp>
#include <ossia/dataflow/node_process.hpp>

namespace QVKRT
{
class PlyLoader;
class PlyLoaderExecutor final
    : public Execution::ProcessComponent_T<QVKRT::PlyLoader, ossia::node_process>
{
  COMPONENT_METADATA("8c2cb8fb-1a7f-4f4e-a29d-22358db512aa") // 生成一个新的 UUID

public:
  PlyLoaderExecutor(PlyLoader& element, const Execution::Context& ctx, QObject* parent);

  void onSetup(Execution::SetupContext& ctx);
  void onExecutionStart(const Execution::Context& ctx);
  void onExecutionStop(const Execution::Context& ctx);


private:
  void loadPLYFile(const QString& path);
};
using PlyLoaderExecutorFactory = Execution::ProcessComponentFactory_T<PlyLoaderExecutor>;
}
