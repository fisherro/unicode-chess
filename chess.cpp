#include <array>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

/*
 * TODO:
 * Print FEN.
 * Make dash in moves optional.
 * Commands to place pieces on the board. (Ne4 would put a kinght on e4?)
 * Command to clear the board.
 * Display PGN files using Unicode pieces instead of letters.
 */

bool use_unicode = true;

struct Position {
    using Rank = std::array<char, 8>;
    using Board = std::array<Rank, 8>;
    Board board;
};

Position undo_pos;

void set_position(Position& pos, std::string fen)
{
    int rank_index = 0; // 0 == rank 8
    int file_index = 0; // 0 == a
    for (const char c: fen) {
        if (' ' == c) break;
        if ('/' == c) {
            ++rank_index;
            file_index = 0;
        } else if (::isdigit(c)) {
            int count = std::stoi(std::string(1, c));
            for (int i = 0; i < count; ++i) {
                pos.board.at(rank_index).at(file_index) = '.';
                ++file_index;
            }
        } else {
            pos.board.at(rank_index).at(file_index) = c;
            ++file_index;
        }
    }
}

std::string pos_to_fen(const Position& pos)
{
    std::ostringstream ss;
    for (const auto& rank: pos.board) {
        for (const char c: rank) {
            if (::isalpha(c)) {
                ss << c;
            } else {
            }
        }
    }
    return ss.str();
}

std::string to_unicode(char c)
{
    switch (c) {
        case 'K': return "\u2654";
        case 'Q': return "\u2655";
        case 'R': return "\u2656";
        case 'B': return "\u2657";
        case 'N': return "\u2658";
        case 'P': return "\u2659";
        case 'k': return "\u265a";
        case 'q': return "\u265b";
        case 'r': return "\u265c";
        case 'b': return "\u265d";
        case 'n': return "\u265e";
        case 'p': return "\u265f";
        default: return ".";
    };
}

void print_position(const Position& pos)
{
    const std::string text_selector{"\ufe0e"};
    int current_rank = 8;
    std::cout << "   a b c d e f g h\n\n";
    for (const auto& rank: pos.board) {
        std::cout << current_rank << "  ";
        for (const char c: rank) {
            if (use_unicode) {
                std::cout << to_unicode(c) << text_selector << ' ';
            } else {
                std::cout << c << ' ';
            }
        }
        std::cout << ' ' << current_rank << '\n';
        --current_rank;
    }
    std::cout << "\n   a b c d e f g h\n";
}

int file_to_index(char c)
{
    return c - 'a';
}

int rank_to_index(int r)
{
    return 8 - (r - '0');
}

void do_move(Position& pos, const std::string& input)
{
    int start_file = file_to_index(input.at(0));
    int start_rank = rank_to_index(input.at(1));
    int end_file = file_to_index(input.at(3));
    int end_rank = rank_to_index(input.at(4));
    //TODO: Make an undo stack
    undo_pos = pos;
    pos.board.at(end_rank).at(end_file) =
        pos.board.at(start_rank).at(start_file);
    pos.board.at(start_rank).at(start_file) = '.';
}

void undo(Position& pos)
{
    std::swap(pos, undo_pos);
}

void help()
{
    std::cout <<
        "help\tThis text\n"
        "quit\tExit this program\n"
        "reset\tReset to the starting position\n"
        "undo\tGo back to the previous position\n"
        "unicode\tUse Unicode symbols\n"
        "ascii\tUse ASCII characters\n"
        "To move, enter start file & rank, a dash, and end\n"
        "e.g. e2-e4\n";
}

int main()
{
    const std::string start(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Position pos;
    set_position(pos, start);
    undo_pos = pos;
    print_position(pos);
    std::string line;
    std::cout << "> " << std::flush;
    while (std::getline(std::cin, line)) {
        if ("quit" == line) {
            break;
        } else if ("help" == line) {
            help();
        } else if ("reset" == line) {
            undo_pos = pos;
            set_position(pos, start);
        } else if ("undo" == line) {
            undo(pos);
        } else if ("unicode" == line) {
            use_unicode = true;
        } else if ("ascii" == line) {
            use_unicode = false;
        } else {
            do_move(pos, line);
        }
        print_position(pos);

        std::cout << "> " << std::flush;
    }
}

