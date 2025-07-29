#include <score/tools/Debug.hpp>
#include <QVector4D>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <vector>
#include <miniply/miniply.h>

namespace QVKRT
{

bool loadPly(const QString& filePath,
                     std::vector<QVector4D>& outPoints,
                     std::vector<QVector4D>& outColors)
{
    outPoints.clear();
    outColors.clear();

    // 1. Open file to check if it's ASCII or Binary
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "无法打开 PLY 文件:" << filePath;
        return false;
    }

    QByteArray headerData = file.read(1024); // read part of the header
    file.close();

    bool isBinary = headerData.contains("format binary");
    if (!isBinary)
    {
        // -----------------------
        // Fallback to ASCII parser
        // -----------------------
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;

        QTextStream in(&file);
        QString line;
        bool headerParsed = false;
        int vertexCount = 0;
        bool hasColor = false;

        while (!in.atEnd())
        {
            line = in.readLine();
            if (line.startsWith("element vertex"))
                vertexCount = line.section(' ', 2, 2).toInt();
            else if (line.startsWith("property uchar red"))
                hasColor = true;
            else if (line == "end_header")
            {
                headerParsed = true;
                break;
            }
        }

        if (!headerParsed || vertexCount == 0)
            return false;

        outPoints.reserve(vertexCount);
        outColors.reserve(vertexCount);

        int readCount = 0;
        while (!in.atEnd() && readCount < vertexCount)
        {
            line = in.readLine();
            const auto parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 3)
            {
                bool okX, okY, okZ;
                float x = parts[0].toFloat(&okX);
                float y = parts[1].toFloat(&okY);
                float z = parts[2].toFloat(&okZ);
                if (okX && okY && okZ)
                {
                    outPoints.emplace_back(x, y, z, 1.0f);
                    if (hasColor && parts.size() >= 6)
                    {
                        float r = parts[3].toFloat() / 255.f;
                        float g = parts[4].toFloat() / 255.f;
                        float b = parts[5].toFloat() / 255.f;
                        outColors.emplace_back(r, g, b, 1.0f);
                    }
                    else
                    {
                        outColors.emplace_back(1.f, 1.f, 1.f, 1.f);
                    }
                    ++readCount;
                }
            }
        }

        return true;
    }

    // -----------------------
    // Binary mode (using miniply)
    // -----------------------
    miniply::PLYReader reader(filePath.toStdString().c_str());
    if (!reader.valid())
    {
        qDebug() << "无法解析二进制 PLY 文件:" << filePath;
        return false;
    }

    while (reader.has_element())
    {
        if (reader.element_is(miniply::kPLYVertexElement) && reader.load_element())
        {
            const uint32_t N = reader.num_rows();
            outPoints.reserve(N);
            outColors.reserve(N);

            uint32_t pos_indices[3];
            uint32_t col_indices[3];
            bool hasPos = reader.find_pos(pos_indices);
            bool hasColor = reader.find_color(col_indices);

            if (!hasPos)
                return false;

            std::vector<float> posData(3 * N);
            reader.extract_properties(pos_indices, 3, miniply::PLYPropertyType::Float, posData.data());

            std::vector<float> colorData;
            if (hasColor)
            {
                colorData.resize(3 * N);
                reader.extract_properties(col_indices, 3, miniply::PLYPropertyType::Float, colorData.data());

                // normalize if original type is uchar
                auto t = reader.element()->properties[col_indices[0]].type;
                if (t == miniply::PLYPropertyType::UChar)
                {
                    for (float& c : colorData)
                        c /= 255.f;
                }
            }

            for (uint32_t i = 0; i < N; i++)
            {
                outPoints.emplace_back(posData[3 * i + 0], posData[3 * i + 1], posData[3 * i + 2], 1.0f);
                if (hasColor)
                    outColors.emplace_back(colorData[3 * i + 0], colorData[3 * i + 1], colorData[3 * i + 2], 1.0f);
                else
                    outColors.emplace_back(1.f, 1.f, 1.f, 1.f);
            }

            return true;
        }
        reader.next_element();
    }

    return false;
}

} // namespace QVKRT
