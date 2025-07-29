#include "score_addon_qvkrt.hpp"

#include <Avnd/Factories.hpp>
#include "QVKRT/PlyLoader.hpp"
#include "QVKRT/PlyLoaderExecutor.hpp"

#include <Library/LibraryInterface.hpp>
#include <Library/LibrarySettings.hpp>
#include <Library/ProcessesItemModel.hpp>

#include <score/plugins/FactorySetup.hpp>

#include <QVKRT/Executor.hpp>
#include <QVKRT/Layer.hpp>
#include <QVKRT/Process.hpp>
#include <score_plugin_engine.hpp>
#include <score_plugin_gfx.hpp>


namespace QVKRT
{
class PlyLibraryHandler final : public QObject, public Library::LibraryInterface
{
  SCORE_CONCRETE("b25f6b47-3d0d-4d3d-91e5-123456789abc") // 生成一个新的 UUID

  QSet<QString> acceptedFiles() const noexcept override { return {"ply"}; }

  Library::Subcategories categories;

  using proc = oscr::ProcessModel<PlyLoader>;
  void setup(Library::ProcessesItemModel& model, const score::GUIApplicationContext& ctx) override
  {
    const auto& key = Metadata<ConcreteKey_k, proc>::get();
    QModelIndex node = model.find(key);
    if (node == QModelIndex{})
      return;

    categories.init(node, ctx);
  }

  void addPath(std::string_view path) override
  {
    auto p = QString::fromUtf8(path.data(), path.length());
    QFileInfo file{p};

    Library::ProcessData pdata;
    pdata.prettyName = file.completeBaseName();        // 用文件名显示
    pdata.key = Metadata<ConcreteKey_k, proc>::get();  // 关键的 ProcessModel Key
    pdata.customData = p;                              // 保存路径
    categories.add(file, std::move(pdata));
  }
};

class PlyDropHandler final : public Process::ProcessDropHandler
{
  SCORE_CONCRETE("c5d6b843-2d1b-4e7c-889e-abcdef987654") // 新 UUID

  QSet<QString> fileExtensions() const noexcept override { return {"ply"}; }

  using proc = oscr::ProcessModel<PlyLoader>;
  void dropData(
      std::vector<ProcessDrop>& vec,
      const DroppedFile& data,
      const score::DocumentContext& ctx) const noexcept override
  {
    const auto& [filename, content] = data;

    Process::ProcessDropHandler::ProcessDrop p;
    p.creation.key = Metadata<ConcreteKey_k, proc>::get();
    p.creation.prettyName = filename.basename;
    p.setup = [s = filename.relative](Process::ProcessModel& m, score::Dispatcher& disp) mutable
    {
      auto& pp = static_cast<proc&>(m);
      auto& inl = *safe_cast<Process::ControlInlet*>(pp.inlets()[0]);
      disp.submit(new Process::SetControlValue{inl, s.toStdString()});
    };

    vec.push_back(std::move(p));
  }
};
}



score_addon_qvkrt::score_addon_qvkrt() { }

score_addon_qvkrt::~score_addon_qvkrt() { }

std::vector<score::InterfaceBase*>
score_addon_qvkrt::factories(
    const score::ApplicationContext& ctx,
    const score::InterfaceKey& key) const
{
  std::vector<score::InterfaceBase*> fx;
  Avnd::instantiate_fx<
      QVKRT::PlyLoader>(fx, ctx, key);

  auto add =  instantiate_factories<
      score::ApplicationContext,
      FW<Process::ProcessModelFactory, QVKRT::ProcessFactory>,
      FW<Process::LayerFactory, QVKRT::LayerFactory>,
      FW<Library::LibraryInterface,
  QVKRT::PlyLibraryHandler>,
  FW<Process::ProcessDropHandler,
  QVKRT::PlyDropHandler>,
      FW<Execution::ProcessComponentFactory,
         QVKRT::ProcessExecutorComponentFactory>>(ctx, key);

  fx.insert(
    fx.end(),
    std::make_move_iterator(add.begin()),
    std::make_move_iterator(add.end()));
  return fx;
}

auto score_addon_qvkrt::required() const -> std::vector<score::PluginKey>
{
  return {score_plugin_gfx::static_key()};
}

#include <score/plugins/PluginInstances.hpp>
SCORE_EXPORT_PLUGIN(score_addon_qvkrt)
