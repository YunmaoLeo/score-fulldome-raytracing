#include "Process.hpp"

#include <Gfx/Graph/Node.hpp>
#include <Gfx/Graph/ShaderCache.hpp>
#include <Gfx/TexturePort.hpp>
#include <Process/Dataflow/Port.hpp>
#include <Process/Dataflow/WidgetInlets.hpp>

#include <wobjectimpl.h>
W_OBJECT_IMPL(QVKRT::Model)
namespace QVKRT
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
  m_outlets.push_back(new Gfx::TextureOutlet{Id<Process::Port>(0), this});
  m_inlets.push_back(new Gfx::GeometryInlet{Id<Process::Port>(0), this});

  m_inlets.push_back(new Process::XYZSpinboxes{
    ossia::vec3f{-10000., -10000., -10000.}, ossia::vec3f{10000., 10000., 10000.},
    ossia::vec3f{-1., -1., -1.}, "Position", Id<Process::Port>(1), this});
  m_inlets.push_back(new Process::XYZSpinboxes{
      ossia::vec3f{-10000., -10000., -10000.}, ossia::vec3f{10000., 10000., 10000.},
      ossia::vec3f{}, "Center", Id<Process::Port>(2), this});

  m_inlets.push_back(
      new Process::FloatSlider{0.01, 359.999, 90., "FOV", Id<Process::Port>(3), this});
}

QString Model::prettyName() const noexcept
{
  return tr("Vulkan Ratracing");
}

}
template <>
void DataStreamReader::read(const QVKRT::Model& proc)
{
  readPorts(*this, proc.m_inlets, proc.m_outlets);

  insertDelimiter();
}

template <>
void DataStreamWriter::write(QVKRT::Model& proc)
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
void JSONReader::read(const QVKRT::Model& proc)
{
  readPorts(*this, proc.m_inlets, proc.m_outlets);
}

template <>
void JSONWriter::write(QVKRT::Model& proc)
{
  writePorts(
      *this,
      components.interfaces<Process::PortFactoryList>(),
      proc.m_inlets,
      proc.m_outlets,
      &proc);
}
