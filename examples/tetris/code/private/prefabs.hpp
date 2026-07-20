#pragma once

#include <ass/fixed_bitset.hpp>
#include <functional>
#include <optional>
#include <ranges>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/error_handling.hpp"

template <std::integral T>
[[nodiscard]] constexpr auto Make2dCoords(edt::Vec2<T> size)
{
    return std::views::join(
        std::views::iota(T{0}, size.y()) |
        std::views::transform(
            [width = size.x()](T y)
            {
                return std::views::iota(T{0}, width) | std::views::transform([y](T x) { return edt::Vec2<T>{x, y}; });
            }));
}

[[nodiscard]] constexpr size_t Coord2dToIndex(edt::Vec2<size_t> coordinate, size_t width)
{
    return width * coordinate.y() + coordinate.x();
}

class BlockPrefab
{
public:
    [[nodiscard]] constexpr auto AllCoords() const
    {
        return Make2dCoords(size) | std::views::filter(std::bind_front(&BlockPrefab::GetCell, this));
    }
    [[nodiscard]] constexpr bool GetCell(edt::Vec2<size_t> coordinate) const
    {
        return bits.Get(Coord2dToIndex(coordinate, 4));
    }
    constexpr void SetCell(edt::Vec2<size_t> coordinate) { bits.Set(Coord2dToIndex(coordinate, 4), true); }

    edt::Vec2<size_t> size{};
    ass::FixedBitset<16> bits;
};

[[nodiscard]] constexpr BlockPrefab ParseBlockPrefab(std::string_view text)
{
    std::optional<char> block_character;
    BlockPrefab block;
    size_t occupied = 0;
    size_t cell = 0;
    for (char character : text)
    {
        if (character >= 'A' && character <= 'Z')
        {
            if (block_character)
            {
                klvk::ErrorHandling::Ensure(*block_character == character, "Multiple block characters");
            }
            else
            {
                block_character = character;
            }
            klvk::ErrorHandling::Ensure(cell < 16, "Too many cells");
            const edt::Vec2<size_t> coordinate{cell % 4, 3 - cell / 4};
            block.SetCell(coordinate);
            block.size.x() = std::max(block.size.x(), coordinate.x() + 1);
            block.size.y() = std::max(block.size.y(), coordinate.y() + 1);
            ++occupied;
        }
        else if (character != '.')
        {
            continue;
        }
        ++cell;
    }
    klvk::ErrorHandling::Ensure(occupied == 4, "Expected 4 cells to be occupied");
    return block;
}

[[nodiscard]] constexpr auto Rotations(std::string_view a, std::string_view b, std::string_view c, std::string_view d)
{
    return std::array{ParseBlockPrefab(a), ParseBlockPrefab(b), ParseBlockPrefab(c), ParseBlockPrefab(d)};
}

inline constexpr auto RotationsS =
    Rotations(".SS.SS..........", ".S...SS...S.....", ".....SS.SS......", "S...SS...S......");
inline constexpr auto RotationsI =
    Rotations("....IIII........", "..I...I...I...I.", "........IIII....", ".I...I...I...I..");
inline constexpr auto RotationsO =
    Rotations(".OO..OO.........", ".OO..OO.........", ".OO..OO.........", ".OO..OO.........");
inline constexpr auto RotationsT =
    Rotations(".T..TTT.........", ".T...TT..T......", "....TTT..T......", ".T..TT...T......");
inline constexpr auto RotationsL =
    Rotations("..L.LLL.........", ".L...L...LL.....", "....LLL.L.......", "LL...L...L......");
inline constexpr auto RotationsJ =
    Rotations("J...JJJ.........", ".JJ..J...J......", "....JJJ...J.....", ".J...J..JJ......");
inline constexpr auto RotationsZ =
    Rotations("ZZ...ZZ.........", "..Z..ZZ..Z......", "....ZZ...ZZ.....", ".Z..ZZ..Z.......");

inline constexpr std::array
    Prefabs{&RotationsI, &RotationsS, &RotationsO, &RotationsT, &RotationsL, &RotationsJ, &RotationsZ};

[[nodiscard]] constexpr const BlockPrefab& GetBlockPrefab(size_t block, size_t rotation)
{
    return Prefabs.at(block)->at(rotation);
}
