#include "Process.hpp"

#include <Gfx/Graph/Node.hpp>
#include <Gfx/Graph/ShaderCache.hpp>
#include <Gfx/TexturePort.hpp>
#include <Process/Dataflow/Port.hpp>
#include <Process/Dataflow/WidgetInlets.hpp>

#include <wobjectimpl.h>
W_OBJECT_IMPL(vkfrt::Model)
namespace vkfrt
{
Model::Model(
    const TimeVal& duration,
    const Id<Process::ProcessModel>& id,
    QObject* parent)
    : Process::ProcessModel{duration, id, "gfxProcess", parent}
{
  metadata().setInstanceName(*this);
  init();
}

Model::~Model() { }

void Model::init(){
  if (m_inlets.empty() && m_outlets.empty())
  {
    m_outlets.push_back(new Gfx::TextureOutlet{Id<Process::Port>(0), this});
    m_inlets.push_back(new Gfx::GeometryInlet{Id<Process::Port>(0), this});

    m_inlets.push_back(new Process::XYZSpinboxes{
      ossia::vec3f{-10000., -10000., -10000.}, ossia::vec3f{10000., 10000., 10000.},
      ossia::vec3f{-1., -1., -1.}, "Position", Id<Process::Port>(1), this});
    m_inlets.push_back(new Process::XYZSpinboxes{
        ossia::vec3f{-10000., -10000., -10000.}, ossia::vec3f{10000., 10000., 10000.},
        ossia::vec3f{}, "LookAtPoint", Id<Process::Port>(2), this});

    m_inlets.push_back(
        new Process::FloatSlider{0.01, 359.999, 90., "FOV", Id<Process::Port>(3), this});

  }

  if (m_inlets.size() <= 4)
  {
    std::vector<std::pair<QString, ossia::value>> projmodes{
              {"Perspective", 0},
              {"Fulldome (1-pass)", 1},
          };
    m_inlets.push_back(
        new Process::ComboBox{projmodes, 0, "Camera", Id<Process::Port>(4), this});
  }
}

QString Model::prettyName() const noexcept
{
  return tr("Vulkan Ratracing");
}

}
template <>
void DataStreamReader::read(const vkfrt::Model& proc)
{
  readPorts(*this, proc.m_inlets, proc.m_outlets);

  insertDelimiter();
}

template <>
void DataStreamWriter::write(vkfrt::Model& proc)
{
  writePorts(
      *this,
      components.interfaces<Process::PortFactoryList>(),
      proc.m_inlets,
      proc.m_outlets,
      &proc);
  checkDelimiter();
}

template <>
void JSONReader::read(const vkfrt::Model& proc)
{
  readPorts(*this, proc.m_inlets, proc.m_outlets);
}

template <>
void JSONWriter::write(vkfrt::Model& proc)
{
  writePorts(
      *this,
      components.interfaces<Process::PortFactoryList>(),
      proc.m_inlets,
      proc.m_outlets,
      &proc);
}
