#pragma once

#include <vector>
#include <QVector>
#include <QString>

namespace QVKRT
{
bool loadStanfordPLY(const QString& filePath, std::vector<QVector4D>& outPoints, std::vector<QVector4D>& outColors);
}
