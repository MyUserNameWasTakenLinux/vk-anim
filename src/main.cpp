#include "render.h"
#include <iostream>

int main(void) {
    std::vector<Vertex> vertices {
        {glm::vec4(0.0, -0.5, 0.0, 1.0), glm::vec4(0.2f, 0.5f, 0.5f, 1.0f)},
        {glm::vec4(0.5, 0.5, 0.0, 1.0), glm::vec4(0.2f, 0.5f, 0.5f, 1.0f)},
        {glm::vec4(-0.5, 0.5, 0.0, 1.0), glm::vec4(0.2f, 0.5f, 0.5f, 1.0f)},
        {glm::vec4(0.0, -0.5, 0.0, 1.0), glm::vec4(0.2f, 0.5f, 0.5f, 1.0f)}
    };
    VObject triangle {vertices, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)};
    Render r(640, 800, "vk-anim");
    r.add_vobject(triangle);
    r.loop();
    return 0;
}