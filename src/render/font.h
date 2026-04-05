#pragma once
#include <SFML/Graphics.hpp>
#include <iostream>

#include "FreeMono_otf.h"

inline sf::Font &get_font() {
  static sf::Font font = [] {
    sf::Font f;
    bool ok = f.openFromMemory(FreeMono_otf, FreeMono_otf_len);
    if (!ok)
      std::cerr << "Failed to load embedded font\n";
    return f;
  }();
  return font;
}
