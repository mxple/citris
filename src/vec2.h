#pragma once

template <typename T>
struct Vec2T {
  T x, y;

  constexpr Vec2T() : x(0), y(0) {}
  constexpr Vec2T(T x, T y) : x(x), y(y) {}

  Vec2T operator+(const Vec2T &other) const {
    return Vec2T(x + other.x, y + other.y);
  }

  Vec2T operator-(const Vec2T &other) const {
    return Vec2T(x - other.x, y - other.y);
  }

  Vec2T operator*(T scalar) const { return Vec2T(x * scalar, y * scalar); }

  Vec2T &operator+=(const Vec2T &other) {
    x += other.x;
    y += other.y;
    return *this;
  }

  Vec2T &operator-=(const Vec2T &other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  Vec2T &operator*=(T scalar) {
    x *= scalar;
    y *= scalar;
    return *this;
  }

  bool operator==(const Vec2T &other) const {
    return x == other.x && y == other.y;
  }

  bool operator!=(const Vec2T &other) const { return !(*this == other); }
};

using Vec2 = Vec2T<int>;
using Vec2f = Vec2T<float>;
