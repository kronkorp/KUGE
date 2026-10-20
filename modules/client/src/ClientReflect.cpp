#include "ClientReflect.hpp"
#include "render/Components.hpp"

void kuge::describeClient(ComponentRegistry& registry)
{
    registry.describe<Sprite>("Sprite", {
        field<Sprite>("size", &Sprite::size, {.help = "In the world; 0, 0: the size of the picture"}),
        field<Sprite>("pivot", &Sprite::pivot),
        field<Sprite>("frame", &Sprite::frame, {.min = 0}),
        field<Sprite>("layer", &Sprite::layer),
        field<Sprite>("z", &Sprite::z),
        field<Sprite>("flipX", &Sprite::flipX),
        field<Sprite>("flipY", &Sprite::flipY),
        field<Sprite>("visible", &Sprite::visible),
    });
    registry.describe<Camera2D>("Camera2D", {
        field<Camera2D>("position", &Camera2D::position),
        field<Camera2D>("zoom", &Camera2D::zoom, {.min = 0.01}),
        field<Camera2D>("logicalSize", &Camera2D::logicalSize, {.help = "0, 0: the zoom decides"}),
        field<Camera2D>("fit", &Camera2D::fit, {.names = {"Stretch", "Letterbox", "IntegerLetterbox", "FitHeight"}}),
    });
}
