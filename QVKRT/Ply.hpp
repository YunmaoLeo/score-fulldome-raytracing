#pragma once
#include <QVKRT/TinyObj.hpp>

namespace QVKRT
{
std::vector<mesh> PlyFromFile(std::string_view filename, float_vec& data);
}
