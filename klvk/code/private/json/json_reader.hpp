#pragma once

#include <array>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

// A name and a value in a document, read together so that every failure can say
// which field was wrong. Reading is always checked: a value of the wrong type or
// outside its range is an error, never a silently converted one.
//
// This is what a parser is built out of. Business logic never sees one - it
// receives the object a parser produced.
class JsonReader
{
public:
    JsonReader(const nlohmann::json& value, std::string path) : value_(&value), path_(std::move(path)) {}

    [[nodiscard]] const std::string& Path() const noexcept { return path_; }
    [[nodiscard]] const nlohmann::json& Value() const noexcept { return *value_; }

    // Every key an object is allowed to carry. A document naming anything else
    // is a typo the reader refuses rather than ignores.
    void EnsureKnownKeys(std::initializer_list<std::string_view> known_keys) const
    {
        EnsureObject();
        for (const auto& [key, ignored] : value_->items())
        {
            const bool known = std::ranges::find(known_keys, key) != known_keys.end();
            ErrorHandling::Ensure(known, "Unknown field '{}.{}'", path_, key);
        }
    }

    [[nodiscard]] bool Contains(std::string_view key) const { return value_->is_object() && value_->contains(key); }

    [[nodiscard]] JsonReader Field(std::string_view key) const
    {
        EnsureObject();
        ErrorHandling::Ensure(value_->contains(key), "Field '{}.{}' is missing", path_, key);
        return {value_->at(key), fmt::format("{}.{}", path_, key)};
    }

    [[nodiscard]] std::optional<JsonReader> OptionalField(std::string_view key) const
    {
        if (!Contains(key)) return std::nullopt;
        return Field(key);
    }

    [[nodiscard]] std::vector<JsonReader> Elements() const
    {
        ErrorHandling::Ensure(value_->is_array(), "Field '{}' must be an array", path_);
        std::vector<JsonReader> elements;
        elements.reserve(value_->size());
        for (size_t index = 0; index != value_->size(); ++index)
        {
            elements.emplace_back(value_->at(index), fmt::format("{}[{}]", path_, index));
        }
        return elements;
    }

    // Object fields in document order, for the places where the keys are data
    // rather than a fixed vocabulary.
    [[nodiscard]] std::vector<std::pair<std::string, JsonReader>> Fields() const
    {
        EnsureObject();
        std::vector<std::pair<std::string, JsonReader>> fields;
        for (const auto& [key, value] : value_->items())
        {
            fields.emplace_back(key, JsonReader{value, fmt::format("{}.{}", path_, key)});
        }
        return fields;
    }

    void EnsureObject() const { ErrorHandling::Ensure(value_->is_object(), "Field '{}' must be an object", path_); }

    [[nodiscard]] u64 UInt() const
    {
        ErrorHandling::Ensure(value_->is_number_integer(), "Field '{}' must be an integer", path_);
        if (value_->is_number_unsigned()) return value_->get<u64>();
        const i64 result = value_->get<i64>();
        ErrorHandling::Ensure(result >= 0, "Field '{}' cannot be negative", path_);
        return static_cast<u64>(result);
    }

    [[nodiscard]] u64 PositiveUInt() const
    {
        const u64 result = UInt();
        ErrorHandling::Ensure(result > 0, "Field '{}' is one-based and must be positive", path_);
        return result;
    }

    [[nodiscard]] double Number(bool allow_zero = true) const
    {
        ErrorHandling::Ensure(value_->is_number(), "Field '{}' must be a number", path_);
        const double result = value_->get<double>();
        ErrorHandling::Ensure(
            std::isfinite(result) && (allow_zero ? result >= 0.0 : result > 0.0),
            "Field '{}' must be a finite {} number",
            path_,
            allow_zero ? "non-negative" : "positive");
        return result;
    }

    [[nodiscard]] float Float() const
    {
        ErrorHandling::Ensure(value_->is_number(), "Field '{}' must be a number", path_);
        const double parsed = value_->get<double>();
        const auto result = static_cast<float>(parsed);
        ErrorHandling::Ensure(
            std::isfinite(parsed) && std::isfinite(result),
            "Field '{}' must be a finite float",
            path_);
        return result;
    }

    [[nodiscard]] bool Bool() const
    {
        ErrorHandling::Ensure(value_->is_boolean(), "Field '{}' must be a boolean", path_);
        return value_->get<bool>();
    }

    [[nodiscard]] std::string String() const
    {
        ErrorHandling::Ensure(value_->is_string(), "Field '{}' must be a string", path_);
        return value_->get<std::string>();
    }

    [[nodiscard]] std::string NonEmptyString() const
    {
        std::string result = String();
        ErrorHandling::Ensure(!result.empty(), "Field '{}' cannot be empty", path_);
        return result;
    }

    // One table per enumeration drives reading and writing alike, so a value can
    // never be readable but unwritable, or spelled differently in each direction.
    template <typename Enum>
    struct EnumName
    {
        std::string_view name;
        Enum value;
    };

    template <typename Enum, size_t kCount>
    [[nodiscard]] Enum EnumValue(const std::array<EnumName<Enum>, kCount>& table, std::string_view expectation) const
    {
        const std::string text = String();
        const auto found = std::ranges::find(table, text, &EnumName<Enum>::name);
        ErrorHandling::Ensure(
            found != std::ranges::end(table),
            "Unknown value '{}' in '{}' (expected {})",
            text,
            path_,
            expectation);
        return found->value;
    }

    template <typename Enum, size_t kCount>
    [[nodiscard]] static std::string_view NameOf(const std::array<EnumName<Enum>, kCount>& table, Enum value)
    {
        const auto found = std::ranges::find(table, value, &EnumName<Enum>::value);
        ErrorHandling::Ensure(found != std::ranges::end(table), "Enumeration value has no name in its table");
        return found->name;
    }

private:
    const nlohmann::json* value_;
    std::string path_;
};

}  // namespace klvk
