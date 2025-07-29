#include "PlyLoaderExecutor.hpp"

#include "load_ply.hpp"
#include "PlyLoader.hpp"
#include "load_ply.hpp"

namespace QVKRT
{
PlyLoaderExecutor::PlyLoaderExecutor(PlyLoader& element,
                                     const Execution::Context& ctx,
                                     QObject* parent)
    : ProcessComponent_T{element, ctx, "PlyLoaderExecutor", parent}
{
}

void PlyLoaderExecutor::onSetup(Execution::SetupContext& ctx)
{
  // 在开始时加载文件
  loadPLYFile(this->process().getFilePath());
}

void PlyLoaderExecutor::onExecutionStart(const Execution::Context& ctx)
{
  loadPLYFile(this->process().getFilePath());
}

void PlyLoaderExecutor::onExecutionStop(const Execution::Context& ctx)
{
}

void PlyLoaderExecutor::loadPLYFile(const QString& path)
{
  std::vector<QVector4D> points;
  std::vector<QVector4D> colors;

  if (QVKRT::loadPly(path, points, colors))
  {
    qDebug() << "[PLY Loader] Loaded" << points.size() << "points.";
    // 🔹 这里 emit 一个 Qt Signal 给 PointCloud Node 或者存储到共享数据
  }
  else
  {
    qWarning() << "[PLY Loader] Failed to load:" << path;
  }
}
}
