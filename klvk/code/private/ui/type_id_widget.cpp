#include <fmt/format.h>
#include <imgui.h>

#include <limits>

#include "CppReflection/GetStaticTypeInfo.hpp"
#include "CppReflection/TypeRegistry.hpp"
#include "ass/fixed_unordered_map.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/reflection/matrix_reflect.hpp"  // IWYU pragma: keep
#include "klvk/ui/type_id_widget_minimal.hpp"

namespace klvk
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

template <typename T>
static constexpr ImGuiDataType_ CastDataType() noexcept
{
    if constexpr (std::is_same_v<T, int8_t>)
    {
        return ImGuiDataType_S8;
    }
    if constexpr (std::is_same_v<T, uint8_t>)
    {
        return ImGuiDataType_U8;
    }
    if constexpr (std::is_same_v<T, int16_t>)
    {
        return ImGuiDataType_S16;
    }
    if constexpr (std::is_same_v<T, uint16_t>)
    {
        return ImGuiDataType_U16;
    }
    if constexpr (std::is_same_v<T, int32_t>)
    {
        return ImGuiDataType_S32;
    }
    if constexpr (std::is_same_v<T, uint32_t>)
    {
        return ImGuiDataType_U32;
    }
    if constexpr (std::is_same_v<T, int64_t>)
    {
        return ImGuiDataType_S64;
    }
    if constexpr (std::is_same_v<T, uint64_t>)
    {
        return ImGuiDataType_U64;
    }
    if constexpr (std::is_same_v<T, float>)
    {
        return ImGuiDataType_Float;
    }
    if constexpr (std::is_same_v<T, double>)
    {
        return ImGuiDataType_Double;
    }
}

template <typename T>
class ScalarPropWidget
{
public:
    static bool Widget(std::string_view name, void* address)
    {
        constexpr T min = std::numeric_limits<T>::lowest();
        constexpr T max = std::numeric_limits<T>::max();
        auto value = reinterpret_cast<T*>(address);  // NOLINT
        return ImGui::DragScalar(name.data(), CastDataType<T>(), value, 1.0f, &min, &max);
    }

    static void ConstWidget(std::string_view name, const void* address)
    {
        constexpr T min = std::numeric_limits<T>::lowest();
        constexpr T max = std::numeric_limits<T>::max();
        auto value = *reinterpret_cast<const T*>(address);  // NOLINT
        ImGui::BeginDisabled(true);
        [[maybe_unused]] bool value_changed =
            ImGui::DragScalar(name.data(), CastDataType<T>(), &value, 1.0f, &min, &max);
        assert(!value_changed);
        ImGui::EndDisabled();
    }
};

template <typename T, size_t N>
bool VectorProperty(
    std::string_view title,
    edt::Matrix<T, N, 1>& value,
    T min = std::numeric_limits<T>::lowest(),
    T max = std::numeric_limits<T>::max()) noexcept
{
    return ImGui::DragScalarN(title.data(), CastDataType<T>(), value.data(), N, 0.01f, &min, &max, "%.3f");
}

template <typename T>
class VectorPropWidget
{
public:
    static bool Widget(std::string_view name, void* address)
    {
        T& value = *reinterpret_cast<T*>(address);  // NOLINT
        return VectorProperty(name, value);
    }

    static void ConstWidget(std::string_view name, const void* address)
    {
        T value = *reinterpret_cast<const T*>(address);  // NOLINT
        ImGui::BeginDisabled(true);
        [[maybe_unused]] const bool value_changed = VectorProperty(name, value);
        assert(!value_changed);
        ImGui::EndDisabled();
    }
};

template <typename Matrix, typename T = typename Matrix::Component>
bool MatrixProperty(
    const std::string_view title,
    Matrix& value,
    T min = std::numeric_limits<T>::lowest(),
    T max = std::numeric_limits<T>::max()) noexcept
{
    constexpr size_t num_rows = Matrix::NumRows();
    constexpr size_t num_columns = Matrix::NumColumns();
    constexpr bool is_const = std::is_const_v<Matrix>;
    bool matrix_changed = false;
    if (ImGui::TreeNode(title.data()))
    {
        for (size_t row_index = 0; row_index != num_rows; ++row_index)
        {
            edt::Matrix<T, num_columns, 1> row = value.GetRow(row_index).Transposed();
            ImGui::PushID(static_cast<int>(row_index));

            if constexpr (is_const)
            {
                ImGui::BeginDisabled(true);
            }

            [[maybe_unused]] const bool row_changed = ImGui::DragScalarN(
                "",
                CastDataType<T>(),
                row.data(),
                static_cast<int>(num_columns),
                0.01f,
                &min,
                &max,
                "%.3f");

            if constexpr (is_const)
            {
                assert(!row_changed);
                ImGui::EndDisabled();
            }

            ImGui::PopID();

            if constexpr (!is_const)
            {
                [[unlikely]] if (row_changed)
                {
                    value.SetRow(row_index, row);
                    matrix_changed = true;
                }
            }
        }
        ImGui::TreePop();
    }
    return matrix_changed;
}

template <typename Matrix>
class MatrixPropWidget
{
public:
    static bool Widget(std::string_view name, void* address)
    {
        auto& value = *reinterpret_cast<Matrix*>(address);  // NOLINT
        return MatrixProperty(name, value);
    }

    static void ConstWidget(std::string_view name, const void* address)
    {
        const auto& value = *reinterpret_cast<const Matrix*>(address);  // NOLINT
        MatrixProperty(name, value);
    }
};

struct TypeWidgets
{
    using ConstFn = void (*)(std::string_view name, const void* data);
    using NonConstFn = bool (*)(std::string_view name, void* data);

    NonConstFn fn{};
    ConstFn const_fn{};
};

template <typename Map>
[[nodiscard]] constexpr size_t CountSlotsWithoutCollisions(const Map& m)
{
    ass::FixedBitset<Map::Capacity()> bits;
    for (const auto& [key, value] : m)
    {
        using Hasher = typename Map::Hasher;
        bits.Set(m.ToIndex(Hasher{}(key)), true);
    }

    return bits.CountOnes();
}

template <int rot1, int rot2>
struct GUIDHasher
{
    [[nodiscard]] constexpr size_t operator()(const edt::GUID& guid) const
    {
        size_t a = guid.part1;
        size_t b = guid.part2;

        if constexpr (rot1)
        {
            a = std::rotr(a, rot1);
        }

        if constexpr (rot2)
        {
            b = std::rotr(b, rot2);
        }

        // const uint64_t prime = 0x9e3779b97f4a7c15;
        // return (a * prime) ^ (b * (prime >> 1));

        return a ^ b;
    }
};

using GuidToWidgetHasher = GUIDHasher<6, -6>;
inline constexpr size_t kGuidToWidgetCapacity = 25;
inline constexpr auto kGuidToWidget = []
{
    ass::FixedUnorderedMap<kGuidToWidgetCapacity, edt::GUID, TypeWidgets, GuidToWidgetHasher> map;

    auto add = [&]<typename T, typename Widget>(const std::tuple<T, Widget>&)
    {
        auto guid = cppreflection::GetStaticTypeGUID<T>();
        if (map.Contains(guid))
        {
            throw std::runtime_error("Cannot add twice");
        }

        map.Add(guid, TypeWidgets{.fn = Widget::Widget, .const_fn = Widget::ConstWidget});
    };

    auto add_scalar = [&]<typename T>(const std::tuple<T>&)
    {
        add(std::tuple<T, ScalarPropWidget<T>>{});
    };

    auto add_vector = [&]<typename T>(const std::tuple<T>&)
    {
        add(std::tuple<T, VectorPropWidget<T>>{});
    };

    auto add_matrix = [&]<typename T>(const std::tuple<T>&)
    {
        add(std::tuple<T, MatrixPropWidget<T>>{});
    };

    add_scalar(std::tuple<float>{});
    add_scalar(std::tuple<double>{});
    add_scalar(std::tuple<int8_t>{});
    add_scalar(std::tuple<int16_t>{});
    add_scalar(std::tuple<int32_t>{});
    add_scalar(std::tuple<int64_t>{});
    add_scalar(std::tuple<uint8_t>{});
    add_scalar(std::tuple<uint16_t>{});
    add_scalar(std::tuple<uint32_t>{});
    add_scalar(std::tuple<uint64_t>{});
    add_vector(std::tuple<Vec2f>{});
    add_vector(std::tuple<Vec3f>{});
    add_vector(std::tuple<Vec4f>{});
    add_matrix(std::tuple<Mat3f>{});
    add_matrix(std::tuple<Mat4f>{});

    return map;
}();

inline constexpr size_t nIndices = CountSlotsWithoutCollisions(kGuidToWidget);
static_assert(nIndices == kGuidToWidget.Size());

const TypeWidgets& GetTypeWidgets(const edt::GUID& guid)
{
    [[likely]] if (kGuidToWidget.Contains(guid))
    {
        return kGuidToWidget.Get(guid);
    }

    const auto type_info = cppreflection::GetTypeRegistry()->FindType(guid);
    if (!type_info)
    {
        const auto char_arr = guid.ToCharArray();
        throw ErrorHandling::RuntimeErrorWithMessage(
            "Could not find a type with guid \"{}\", in the type registry",
            std::string_view{char_arr.data(), char_arr.size()});
    }

    throw ErrorHandling::RuntimeErrorWithMessage(
        "type type \"{}\" is not supported by simple type widget feature",
        type_info->GetName());
}

bool SimpleTypeWidget(edt::GUID type_guid, std::string_view name, void* value)
{
    return GetTypeWidgets(type_guid).fn(name, value);
}

bool SimpleTypeWidget(edt::GUID type_guid, std::string_view name, const void* value)
{
    GetTypeWidgets(type_guid).const_fn(name, value);
    return false;
}

void TypeIdWidget(edt::GUID type_guid, void* base, bool& value_changed)
{
    const cppreflection::Type* type_info = cppreflection::GetTypeRegistry()->FindType(type_guid);
    for (const cppreflection::Field* field : type_info->GetFields())
    {
        void* pmember = field->GetValue(base);
        value_changed |= SimpleTypeWidget(field->GetType()->GetGuid(), field->GetName(), pmember);
    }
}

}  // namespace klvk
