#pragma once
#include <Process/Process.hpp>
#include <Process/ProcessFactory.hpp>
#include <score/model/Entity.hpp>
#include <vector>
#include <QVector>
#include <QString>



namespace QVKRT
{
bool loadPly(const QString& filePath, std::vector<QVector4D>& outPoints, std::vector<QVector4D>& outColors);
}
