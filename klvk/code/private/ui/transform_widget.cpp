#include "klvk/ui/transform_widget.hpp"

#include <imgui.h>

#include "klvk/ui/simple_type_widget.hpp"

namespace klvk
{

void TransformWidget(edt::Transform& transform)
{
    if (!ImGui::CollapsingHeader("Transform")) return;

    SimpleTypeWidget("translation", transform.translation);
    SimpleTypeWidget("yaw", transform.rotation.yaw);
    SimpleTypeWidget("pitch", transform.rotation.pitch);
    SimpleTypeWidget("roll", transform.rotation.roll);
    SimpleTypeWidget("scale", transform.scale);
}
}  // namespace klvk
