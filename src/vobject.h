#pragma once
#include <vector>
#include <glm/glm.hpp>


struct Vertex {
    glm::vec4 position;
    glm::vec4 color;
};

class VObject {
public:
    std::vector<Vertex> vertices;
    glm::vec4 position;
};

class VCurve : public VObject {
public:
    VCurve(std::vector<glm::vec3> points);
    VCurve(std::vector<float> x, std::vector<float> y, std::vector<float> z);
    ~VCurve();
};