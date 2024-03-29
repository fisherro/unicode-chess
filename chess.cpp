#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
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

void set_position(Position& pos, std::string_view fen)
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

template <typename I, typename T>
I find_not(I first, I last, T x)
{
    return std::find_if(first, last, [x](T y){ return y != x; });
}

std::string pos_to_fen(const Position& pos)
{
    //TODO: Just use a string or vector instead of a stringstream?
    std::ostringstream ss;
    bool first{true};
    for (const auto& rank: pos.board) {
        if (not std::exchange(first, false)) {
            //Output the rank separator.
            ss << '/';
        }
        //Assume we start with a dot.
        auto dot = rank.begin();
        while (true) {
            //Find the next piece on this rank.
            auto piece = find_not(dot, rank.end(), '.');
            //If the length of the dot run is zero, do nothing...
            if (dot != piece) {
                //...otherwise output the length of the dot run.
                ss << std::to_string(std::distance(dot, piece));
            }
            //Are we done?
            if (rank.end() == piece) {
                break;
            }
            //Copy a run of pieces.
            dot = std::find(piece, rank.end(), '.');
            std::copy(piece, dot, std::ostream_iterator<char>(ss));
            //Are we done?
            if (rank.end() == dot) {
                break;
            }
        }
    }
    return ss.str();
    //TODO: Need to track the info to add the annotations at the end.
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

void do_move(Position& pos, std::string_view input)
{
    if (input.size() < 5) {
        std::cout << std::quoted(input) << " is not a valid move.\n";
        return;
    }
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

void save(const Position& pos)
{
    std::ofstream out{"game.fen"};
    out << pos_to_fen(pos) << '\n';
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
        "fen\tOutput position in FEN format (incomplete)\n"
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
        if (("quit" == line) or ("exit" == line)) {
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
        } else if ("fen" == line) {
            std::cout << pos_to_fen(pos) << '\n';
        } else {
            do_move(pos, line);
        }
        print_position(pos);

        std::cout << "> " << std::flush;
    }
}

