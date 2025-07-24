// load_ply.cpp
// Parses a Stanford .ply file (ASCII) and loads point positions (and optional RGB colors)
// into std::vector<QVector4D> and std::vector<QVector3D>
// This version is adapted for ossia score + qvkrt-based vulkan raytracing addon

#include <score/tools/Debug.hpp>
#include <QVector3D>
#include <QVector4D>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <vector>

namespace QVKRT
{

// Function to load vertex positions (and optional RGB colors) from a Stanford PLY file (ASCII only)
bool loadStanfordPLY(const QString& filePath, std::vector<QVector4D>& outPoints, std::vector<QVector4D>& outColors)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "无法打开 PLY 文件:" << filePath;
        return false;
    }

    QTextStream in(&file);
    QString line;
    bool headerParsed = false;
    int vertexCount = 0;
    bool hasColor = false;

    while (!in.atEnd()) {
        line = in.readLine();
        if (line.startsWith("element vertex")) {
            vertexCount = line.section(' ', 2, 2).toInt();
        }
        else if (line.startsWith("property uchar red")) {
            hasColor = true;
        }
        else if (line == "end_header") {
            headerParsed = true;
            break;
        }
    }

    if (!headerParsed || vertexCount == 0) {
        qDebug() << "无效或不支持的 PLY 文件:" << filePath;
        return false;
    }

    outPoints.clear();
    outPoints.reserve(vertexCount);
    outColors.clear();
    outColors.reserve(vertexCount);

    int readCount = 0;
    while (!in.atEnd() && readCount < vertexCount) {
        line = in.readLine();
        const auto parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            bool okX, okY, okZ;
            float x = parts[0].toFloat(&okX);
            float y = parts[1].toFloat(&okY);
            float z = parts[2].toFloat(&okZ);
            if (okX && okY && okZ) {
                outPoints.emplace_back(QVector4D(x, y, z, 1.0f));
                if (hasColor && parts.size() >= 6) {
                    float r = parts[3].toFloat() / 255.f;
                    float g = parts[4].toFloat() / 255.f;
                    float b = parts[5].toFloat() / 255.f;
                    outColors.emplace_back(QVector4D(r, g, b, 1.0f));
                } else {
                    outColors.emplace_back(QVector3D(1.f, 1.f, 1.f)); // default white
                }
                ++readCount;
            }
        }
    }

    if (readCount != vertexCount) {
        qDebug() << "警告: 预期顶点数量" << vertexCount << ", 实际读取数量" << readCount;
    }

    return true;
}

} // namespace QVKRT
