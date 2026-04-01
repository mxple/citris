#pragma once

#include "event.h"
#include <vector>

class IPlayer {
public:
  virtual ~IPlayer() = default;
  virtual bool process(const Event &ev, TimePoint now,
                       std::vector<Event> &pending) = 0;
};
