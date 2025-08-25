#pragma once
#include <Process/ProcessMetadata.hpp>

namespace vkfrt
{
class Model;
}

PROCESS_METADATA(,
                 vkfrt::Model,
                 "785ab719-fb92-448f-8771-19762daeb570",
                 "vkfrt",                           // Internal name
                 "Fulldome voxel rendering",                           // Pretty name
                 Process::ProcessCategory::Visual,  // Category
                 "GFX",                             // Category
                 "Fulldome voxel rendering with vulkan raytracing",                          // Description
                 "ossia team",                      // Author
                 (QStringList{"shader", "gfx"}),    // Tags
                 {},                                // Inputs
                 {},                                // Outputs
                 QUrl{},                            // Doc link
                 Process::ProcessFlags::SupportsAll // Flags
)

