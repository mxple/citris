#pragma once

#include "settings.h"
#include "ui/widget.h"
#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>

class SettingsEditor {
public:
  SettingsEditor(sf::RenderWindow &window, Settings &settings);

  void run();

private:
  void build_widgets();
  void draw();
  void save_settings();
  float logical_height() const;

  sf::RenderWindow &window_;
  Settings &settings_;
  sf::Font &font_;

  std::vector<std::unique_ptr<Widget>> widgets_;
  float scroll_y_ = 0.f;
  float content_height_ = 0.f;

  // Adapter ints for chrono::ms fields
  int das_ms_, arr_ms_, soft_drop_ms_;
  int gravity_ms_, lock_delay_ms_, garbage_delay_ms_, hard_drop_delay_ms_;

  // Adapter ints for opacity (0-100 percentage)
  int ghost_opacity_pct_, grid_opacity_pct_;

  // Adapter int for scale factor (percentage, e.g. 150 = 1.5x)
  int scale_pct_;
};
