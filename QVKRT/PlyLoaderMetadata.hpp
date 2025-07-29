#pragma once
#include <Process/ProcessMetadata.hpp>

namespace QVKRT {
class PlyLoader;
}

PROCESS_METADATA(,
                 QVKRT::PlyLoader,
                 "cdefb5de-f465-4df5-8c43-b6ea88ed09be", // ✅ 这个是你的 UUID
                 "PLY Loader",                           // Internal name
                 "PLY Loader",                           // Pretty name
                 Process::ProcessCategory::Visual,       // Category
                 "GFX",                                  // Category group
                 "Load and parse PLY point cloud files", // Description
                 "ossia team",                           // Author
                 (QStringList{"pointcloud", "ply"}),     // Tags
                 {},                                     // Inputs
                 {},                                     // Outputs
                 QUrl{},                                 // Doc link
                 Process::ProcessFlags::SupportsAll      // Flags
)