#pragma once
#include <QVKRT/TinyObj.hpp>
#include <halp/geometry.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>
#include <QVector4D>
#include <vector>

namespace QVKRT
{

class PlyLoader
{
public:
  halp_meta(name, "PLY Loader")
  halp_meta(category, "Visuals/3D")
  halp_meta(c_name, "ply_loader")
  halp_meta(authors, "ossia team")
  halp_meta(uuid, "cdefb5de-f465-4df5-8c43-b6ea88ed09af")

  struct ins
  {
    struct ply_t : halp::file_port<"PLY file">
    {
      halp_meta(extensions, "PLY files (*.ply)");
      static std::function<void(PlyLoader&)> process(file_type data);
    } ply;
  } inputs;

  struct
  {
    struct : halp::mesh
    {
      halp_meta(name, "Geometry");
      std::vector<halp::dynamic_geometry> mesh;
    } geometry;
  } outputs;

  void operator()();

  void rebuild_geometry();

  std::vector<mesh> meshinfo{};
  float_vec complete;
};

} // namespace QVKRT
