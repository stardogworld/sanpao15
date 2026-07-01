#include "sanpao15/notation.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sanpao15/bitboard.h"

namespace sanpao15 {

std::string boardToString(const Position& pos) {
    std::string text;
    for (int row = 0; row < BoardSize; ++row) {
        for (int col = 0; col < BoardSize; ++col) {
            const int square = row * BoardSize + col;
            if (hasBit(pos.soldiers, square)) {
                text += 'S';
            } else if (hasBit(pos.cannons, square)) {
                text += 'C';
            } else {
                text += '.';
            }
        }
        if (row + 1 < BoardSize) {
            text += '\n';
        }
    }
    return text;
}

std::string positionToNotation(const Position& pos) {
    std::string text;
    for (int row = 0; row < BoardSize; ++row) {
        for (int col = 0; col < BoardSize; ++col) {
            const int square = row * BoardSize + col;
            if (hasBit(pos.soldiers, square)) {
                text += 'S';
            } else if (hasBit(pos.cannons, square)) {
                text += 'C';
            } else {
                text += '.';
            }
        }
        if (row + 1 < BoardSize) {
            text += '/';
        }
    }
    text += ' ';
    text += pos.side == Side::Cannon ? 'c' : 's';
    return text;
}

Position parsePositionNotation(const std::string& text) {
    std::istringstream input(text);
    std::string boardPart;
    std::string sidePart;
    if (!(input >> boardPart >> sidePart)) {
        throw std::invalid_argument("notation must contain board and side");
    }
    std::string extra;
    if (input >> extra) {
        throw std::invalid_argument("notation contains extra fields");
    }
    if (sidePart.size() != 1 || (sidePart[0] != 'c' && sidePart[0] != 's')) {
        throw std::invalid_argument("side must be c or s");
    }

    std::vector<std::string> rows;
    std::stringstream boardStream(boardPart);
    std::string row;
    while (std::getline(boardStream, row, '/')) {
        rows.push_back(row);
    }
    if (rows.size() != BoardSize) {
        throw std::invalid_argument("board must contain five rows");
    }

    Position pos;
    for (int r = 0; r < BoardSize; ++r) {
        if (rows[r].size() != BoardSize) {
            throw std::invalid_argument("each board row must contain five cells");
        }
        for (int c = 0; c < BoardSize; ++c) {
            const int square = r * BoardSize + c;
            const char ch = rows[r][c];
            if (ch == 'S') {
                pos.soldiers = setBit(pos.soldiers, square);
            } else if (ch == 'C') {
                pos.cannons = setBit(pos.cannons, square);
            } else if (ch != '.') {
                throw std::invalid_argument("board contains an invalid cell character");
            }
        }
    }

    if ((pos.cannons & pos.soldiers) != 0) {
        throw std::invalid_argument("cannons and soldiers overlap");
    }
    pos.side = sidePart[0] == 'c' ? Side::Cannon : Side::Soldier;
    return pos;
}

}  // namespace sanpao15
