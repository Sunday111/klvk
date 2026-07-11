#include "klvk/camera/camera_3d.hpp"

#include <imgui.h>

#include "klvk/reflection/matrix_reflect.hpp"  // IWYU pragma: keep
#include "klvk/ui/simple_type_widget.hpp"

namespace klvk
{

bool Camera3d::Widget()
{
    if (!ImGui::CollapsingHeader("Camera")) return false;

    bool update_view = false;
    update_view |= SimpleTypeWidget("eye", eye_);
    update_view |= SimpleTypeWidget("yaw", rotation_.yaw);
    update_view |= SimpleTypeWidget("pitch", rotation_.pitch);
    update_view |= SimpleTypeWidget("roll", rotation_.roll);

    auto forward = GetViewCache().forward;
    auto right = GetViewCache().right;
    auto up = GetViewCache().up;
    SimpleTypeWidget("forward", forward);
    SimpleTypeWidget("right", right);
    SimpleTypeWidget("up", up);

    if (update_view) view_cache_.reset();

    ImGui::Separator();
    bool update_projection = false;
    update_projection |= SimpleTypeWidget("near", near_);
    update_projection |= SimpleTypeWidget("far", far_);
    update_projection |= SimpleTypeWidget("fov", fov_);
    return update_view || update_projection;
}

}  // namespace klvk
