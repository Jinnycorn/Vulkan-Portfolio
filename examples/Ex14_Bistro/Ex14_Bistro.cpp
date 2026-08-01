#include "engine/Application.h"

using namespace hlab;

int main()
{
    ApplicationConfig config;

    config.models.push_back(
        ModelConfig("models/AmazonLumberyardBistroMorganMcGuire/exterior.obj", "Bistro")
            .setBistroModel(true)
            .setTransform(glm::scale(glm::mat4(1.0f), glm::vec3(0.01f))));

    config.camera = CameraConfig::forBistro();

    auto app = std::make_unique<Application>(config);

    app->run();
    return 0;
}
