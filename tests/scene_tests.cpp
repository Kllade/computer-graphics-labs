#include "scene.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
using namespace scene;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool near(float a, float b) {
    return std::abs(a - b) < 1e-4f;
}
std::array<float, 4> transform(Mat4 m, std::array<float, 4> p) {
    std::array<float, 4> q{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            q[r] += m.at(r, c) * p[c];
    return q;
}
int main() {
    try {
        auto v = vertices();
        std::map<std::pair<int, int>, int> edges;
        float edgeLength = -1;
        for (size_t i = 0; i < indices.size(); i += 3) {
            auto a = v[indices[i]].position, b = v[indices[i + 1]].position,
                 c = v[indices[i + 2]].position;
            require(dot(cross(b - a, c - a), a) > 0, "Face winding must be outward");
            for (int j = 0; j < 3; ++j) {
                int p = indices[i + j], q = indices[i + (j + 1) % 3];
                auto d = v[p].position - v[q].position;
                float length = dot(d, d);
                if (edgeLength < 0)
                    edgeLength = length;
                require(near(length, edgeLength), "All 30 edges must have equal length");
                edges[std::minmax(p, q)]++;
            }
        }
        require(edges.size() == 30 && v.size() - edges.size() + 20 == 2,
                "Closed icosahedron topology");
        for (auto &[edge, count] : edges)
            require(count == 2, "Every edge belongs to two faces");
        for (auto vertex : v) {
            require(near(dot(vertex.position, vertex.position), 1), "Unit circumradius");
            auto c = vertex.color;
            require(c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1 && c.z >= 0 && c.z <= 1,
                    "Vertex color bounds");
        }
        for (auto p : {perspective(45, 1.6f, .1f, 100), orthographic(5, 1.6f, .1f, 100)}) {
            auto n = transform(p, {0, 0, -.1f, 1}), f = transform(p, {0, 0, -100, 1});
            require(near(n[2] / n[3], 0) && near(f[2] / f[3], 1),
                    "Vulkan depth range must be [0,1]");
            auto up = transform(p, {0, 1, -2, 1});
            require(up[1] < 0, "Vulkan viewport Y correction");
        }
        auto p = perspective(45, 1, .1f, 100), o = orthographic(5, 1, .1f, 100);
        auto pn = transform(p, {1, 0, -2, 1}), pf = transform(p, {1, 0, -4, 1});
        require(near(pn[0] / pn[3], 2 * pf[0] / pf[3]), "Perspective foreshortening");
        auto on = transform(o, {1, 0, -2, 1}), of = transform(o, {1, 0, -4, 1});
        require(near(on[0], of[0]), "Orthographic size invariant");
        ObjectState c;
        c.angles = {0, 0, 90};
        c.scale = {2, 1, 1};
        c.position = {3, 0, 0};
        auto point = transform(model(c), {1, 0, 0, 1});
        require(near(point[0], 3) && near(point[1], 2), "Transform order T*R*S");
        advance(c, .05);
        require(c.animationTime == 0, "Paused animation must not advance");
        c.playing = true;
        c.speed = 2;
        advance(c, .05);
        require(std::abs(c.animationTime - .1) < 1e-8, "Animation speed");
        c.playing = false;
        auto frozen = model(c);
        advance(c, 1);
        auto after = model(c);
        for (int i = 0; i < 16; ++i)
            require(near(frozen.v[i], after.v[i]), "Pause must preserve full pose");
        ObjectState independent;
        auto unchanged = model(independent);
        c.playing = true;
        advance(c, .1);
        auto stillUnchanged = model(independent);
        for (int i = 0; i < 16; ++i)
            require(near(unchanged.v[i], stillUnchanged.v[i]), "Object states must be independent");
        c.animationTime = 0;
        auto start = model(c);
        c.animationTime = 2 * pi;
        auto path = trajectory(c);
        require(dot(path, path) < 1e-10f, "Trajectory must be closed");
        (void)start;
        std::cout << "PASS: regular icosahedron, topology, winding, colors, projections, "
                     "transforms, animation and pause\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
