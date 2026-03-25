#pragma once

struct Vec2 {
    int x, y;
    
    constexpr Vec2() : x(0), y(0) {}
    constexpr Vec2(int x, int y) : x(x), y(y) {}
    
    Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }
    
    Vec2 operator-(const Vec2& other) const {
        return Vec2(x - other.x, y - other.y);
    }
    
    Vec2 operator*(int scalar) const {
        return Vec2(x * scalar, y * scalar);
    }
    
    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }
    
    Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    
    Vec2& operator*=(int scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }
    
    bool operator==(const Vec2& other) const {
        return x == other.x && y == other.y;
    }
    
    bool operator!=(const Vec2& other) const {
        return !(*this == other);
    }
};
