#pragma once

#include "settings.h"
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Window.hpp>

class Game {
public:
  Game();
  void run();

  void render();
  void handle_events();

private:
  sf::RenderWindow window_;
};

Game::Game() {
  // init settings
  window_ = sf::RenderWindow(sf::VideoMode({640, 480}), "Tetris");
  window_.setFramerateLimit(60);
}

void Game::handle_events() {
  while (auto event = window_.pollEvent()) {
    if (event->is<sf::Event::Closed>()) {
      window_.close();
      return;
    }

    Settings settings;
    if (auto *kp = event->getIf<sf::Event::KeyPressed>()) {
    }
  }
}

void Game::run() {
  while (window_.isOpen()) {
    handle_events();
    render();
  }
}

void Game::render() {
  window_.clear(sf::Color(30, 30, 30));
  window_.display();
}
