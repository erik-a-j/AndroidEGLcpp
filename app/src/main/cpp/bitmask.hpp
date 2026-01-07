// bitmask.hpp
#pragma once
#include <type_traits>

namespace bitmask {

template <class E>
struct bitmask_traits {
    static constexpr bool enable = false;
    static constexpr E mask = static_cast<E>(~0); // default: all bits (unsafe)
};

template <class E>
concept bitmask_enum =
    std::is_enum_v<E> && bitmask_traits<E>::enable;

template <bitmask_enum E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

template <bitmask_enum E>
constexpr E clamp(E e) noexcept
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(e) & static_cast<U>(bitmask_traits<E>::mask));
}

template <bitmask_enum E>
constexpr E operator|(E lhs, E rhs) noexcept
{
    return clamp(static_cast<E>(to_underlying(lhs) | to_underlying(rhs)));
}

template <bitmask_enum E>
constexpr E operator&(E lhs, E rhs) noexcept
{
    return clamp(static_cast<E>(to_underlying(lhs) & to_underlying(rhs)));
}

template <bitmask_enum E>
constexpr E operator^(E lhs, E rhs) noexcept
{
    return clamp(static_cast<E>(to_underlying(lhs) ^ to_underlying(rhs)));
}

template <bitmask_enum E>
constexpr E operator~(E rhs) noexcept
{
    return clamp(static_cast<E>(~to_underlying(rhs)));
}

template <bitmask_enum E>
constexpr E& operator|=(E& lhs, E rhs) noexcept { return lhs = (lhs | rhs); }

template <bitmask_enum E>
constexpr E& operator&=(E& lhs, E rhs) noexcept { return lhs = (lhs & rhs); }

template <bitmask_enum E>
constexpr E& operator^=(E& lhs, E rhs) noexcept { return lhs = (lhs ^ rhs); }

// Optional helper:
template <bitmask_enum E>
constexpr bool has(E value, E bit) noexcept
{
    return (value & bit) == bit;
}

} // namespace bitmask_ops
