#pragma once
#include <Process/ProcessMetadata.hpp>

namespace QVKRT
{
class Model;
}

PROCESS_METADATA(,
                 QVKRT::Model,
                 "785ab719-fb92-448f-8771-19762daeb570",
                 "QVKRT",                           // Internal name
                 "QVKRT",                           // Pretty name
                 Process::ProcessCategory::Visual,  // Category
                 "GFX",                             // Category
                 "Vulkan raytracing",                          // Description
                 "ossia team",                      // Author
                 (QStringList{"shader", "gfx"}),    // Tags
                 {},                                // Inputs
                 {},                                // Outputs
                 QUrl{},                            // Doc link
                 Process::ProcessFlags::SupportsAll // Flags
)

