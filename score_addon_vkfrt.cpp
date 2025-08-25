#include "score_addon_vkfrt.hpp"

#include <Avnd/Factories.hpp>

#include <Library/LibraryInterface.hpp>
#include <Library/LibrarySettings.hpp>
#include <Library/ProcessesItemModel.hpp>

#include <score/plugins/FactorySetup.hpp>

#include <fulldome_voxel/Executor.hpp>
#include <fulldome_voxel/Layer.hpp>
#include <fulldome_voxel/Process.hpp>
#include <score_plugin_gfx.hpp>


score_addon_vkfrt::score_addon_vkfrt() { }

score_addon_vkfrt::~score_addon_vkfrt() { }

std::vector<score::InterfaceBase*>
score_addon_vkfrt::factories(
    const score::ApplicationContext& ctx,
    const score::InterfaceKey& key) const
{
  return instantiate_factories<
      score::ApplicationContext,
      FW<Process::ProcessModelFactory, vkfrt::ProcessFactory>,
      FW<Process::LayerFactory, vkfrt::LayerFactory>,
      FW<Execution::ProcessComponentFactory,
         vkfrt::ProcessExecutorComponentFactory>>(ctx, key);

}

auto score_addon_vkfrt::required() const -> std::vector<score::PluginKey>
{
  return {score_plugin_gfx::static_key()};
}

#include <score/plugins/PluginInstances.hpp>
SCORE_EXPORT_PLUGIN(score_addon_vkfrt)
