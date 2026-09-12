#include "terrain_column_mesh.hpp"

#include <QTest>
#include <cmath>
#include <map>

using namespace agplayer::terrain::gpu;

class TerrainColumnMeshTest : public QObject {
    Q_OBJECT
private slots:
    void straightBoxCornersAndFlatCap();
    void closedOutwardMesh_data();
    void closedOutwardMesh();
};

void TerrainColumnMeshTest::straightBoxCornersAndFlatCap()
{
    const Vertex point{{0.5F, 0.5F, -0.5F}, {1, 0, 0}};
    const auto tall = decodeColumnPosition(point, {4, 12, 4});
    QVERIFY(std::abs(tall[0] - 2.0F) < 1e-6F);
    QCOMPARE(tall[1], 6.0F);
    QCOMPARE(tall[2], -2.0F);
    const auto wide = decodeColumnPosition(point, {2, 12, 7});
    QVERIFY(std::abs(wide[0] - 1.0F) < 1e-6F);
    QCOMPARE(wide[1], 6.0F);
    QCOMPARE(wide[2], -3.5F);
    int capVertices = 0;
    std::map<std::array<float, 3>, int> faceNormals;
    for (const auto& vertex : columnVertices) {
        ++faceNormals[{vertex.normal[0], vertex.normal[1], vertex.normal[2]}];
        for (float coordinate : vertex.position)
            QCOMPARE(std::abs(coordinate), 0.5F);
        if (vertex.normal[1] == 1) {
            ++capVertices;
            QCOMPARE(vertex.position[1], 0.5F);
        }
    }
    QCOMPARE(faceNormals.size(), size_t(6));
    for (const auto& face : faceNormals) QCOMPARE(face.second, 4);
    QCOMPARE(capVertices, 4);
}

void TerrainColumnMeshTest::closedOutwardMesh_data()
{
    QTest::addColumn<float>("width");
    QTest::addColumn<float>("height");
    QTest::addColumn<float>("depth");
    QTest::newRow("cube") << 4.0F << 4.0F << 4.0F;
    QTest::newRow("tall") << 4.0F << 48.0F << 4.0F;
    QTest::newRow("short") << 4.0F << 0.02F << 4.0F;
    QTest::newRow("non-square") << 2.0F << 12.0F << 7.0F;
}

void TerrainColumnMeshTest::closedOutwardMesh()
{
    QFETCH(float, width); QFETCH(float, height); QFETCH(float, depth);
    const std::array<float, 3> extent{width, height, depth};
    QCOMPARE(columnVertices.size(), size_t(24));
    QCOMPARE(columnIndices.size(), size_t(36));
    using Point = std::array<float, 3>;
    std::map<Point, int> weld;
    std::array<int, columnVertexCount> identities{};
    std::array<Point, columnVertexCount> positions{};
    Point minimum{1e9F, 1e9F, 1e9F}, maximum{-1e9F, -1e9F, -1e9F};
    for (size_t i = 0; i < columnVertices.size(); ++i) {
        const Vertex& vertex = columnVertices[i];
        positions[i] = decodeColumnPosition(vertex, extent);
        auto [it, inserted] = weld.emplace(positions[i], int(weld.size()));
        Q_UNUSED(inserted);
        identities[i] = it->second;
        float normalLength = 0;
        for (size_t axis = 0; axis < 3; ++axis) {
            QVERIFY(std::isfinite(positions[i][axis]));
            QVERIFY(std::isfinite(vertex.normal[axis]));
            QVERIFY(std::abs(positions[i][axis]) <= extent[axis] * 0.5F + 1e-6F);
            minimum[axis] = std::min(minimum[axis], positions[i][axis]);
            maximum[axis] = std::max(maximum[axis], positions[i][axis]);
            normalLength += vertex.normal[axis] * vertex.normal[axis];
        }
        QVERIFY(std::abs(normalLength - 1.0F) < 1e-5F);
    }
    QCOMPARE(weld.size(), size_t(8));
    for (size_t axis = 0; axis < 3; ++axis) {
        QCOMPARE(minimum[axis], -extent[axis] * 0.5F);
        QCOMPARE(maximum[axis], extent[axis] * 0.5F);
    }
    std::map<std::pair<int, int>, int> directedEdges;
    double volumeSix = 0;
    for (size_t triangle = 0; triangle < columnIndices.size(); triangle += 3) {
        const auto ia = columnIndices[triangle], ib = columnIndices[triangle + 1], ic = columnIndices[triangle + 2];
        QVERIFY(ia < columnVertexCount && ib < columnVertexCount && ic < columnVertexCount);
        const Point a = positions[ia], b = positions[ib], c = positions[ic];
        Point ab{}, ac{}, cross{};
        for (size_t axis = 0; axis < 3; ++axis) { ab[axis] = b[axis] - a[axis]; ac[axis] = c[axis] - a[axis]; }
        cross = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
        double outward = 0, shading = 0;
        for (size_t axis = 0; axis < 3; ++axis) {
            outward += cross[axis] * (a[axis] + b[axis] + c[axis]);
            shading += cross[axis] * columnVertices[ia].normal[axis];
            volumeSix += double(a[axis]) * cross[axis];
        }
        QVERIFY2(outward > 0 && shading > 0, "Every decoded triangle must face outward and agree with its normal");
        ++directedEdges[{identities[ia], identities[ib]}];
        ++directedEdges[{identities[ib], identities[ic]}];
        ++directedEdges[{identities[ic], identities[ia]}];
    }
    for (const auto& edge : directedEdges) {
        QCOMPARE(edge.second, 1);
        const auto reverse = directedEdges.find({edge.first.second, edge.first.first});
        QVERIFY2(reverse != directedEdges.end(), "Each oriented edge must have a reverse partner");
        QCOMPARE(reverse->second, 1);
    }
    QCOMPARE(int(weld.size()) - int(directedEdges.size() / 2) + 12, 2);
    const double expectedVolume = double(width) * height * depth;
    QVERIFY(std::abs(volumeSix / 6 - expectedVolume) < expectedVolume * 1e-5);
}

QTEST_APPLESS_MAIN(TerrainColumnMeshTest)
#include "terrain_column_mesh_test.moc"
