#pragma once
#include <Control/DefaultEffectItem.hpp>
#include <Effect/EffectFactory.hpp>
#include <Process/GenericProcessFactory.hpp>

#include <fulldome_voxel/Process.hpp>

namespace vkfrt
{
using LayerFactory = Process::GenericDefaultLayerFactory<vkfrt::Model>;
}
