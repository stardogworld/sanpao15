#pragma once

#include <string>

#include "sanpao15/position.h"

namespace sanpao15 {

std::string boardToString(const Position& pos);
std::string positionToNotation(const Position& pos);
Position parsePositionNotation(const std::string& text);

}  // namespace sanpao15
