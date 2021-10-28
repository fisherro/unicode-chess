#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

/*
 * TODO:
 * DONE Print FEN.
 * Include "metadata" in FEN output.
 * Commands to place pieces on the board. (Ne4 would put a kinght on e4?)
 * DONE Command to clear the board.
 * Convert/display PGN files using Unicode pieces instead of letters.
 * Command to display captures.
 * General C++17 updates?
 * Update to use C++20 ranges/algorithms?
 *
 * Break up different parts?
 *  Position structure
 *  FEN & X-FEN parsing & serialization
 *  Move structure & parsing
 *  Move execution
 *  Input/output
 */

/*
 * PGN standard: http://www.saremba.de/chessgml/standards/pgn/pgn-complete.htm
 * (Is there a more canonical link?)
 * https://www.chessprogramming.org/Main_Page
 * http://www.cs.cmu.edu/afs/cs/project/ai-repository/ai/areas/games/chess/san/
 */

/*
 * A couple of interesting Unicode characters:
 * U+1FA4F WHITE CHESS KNIGHT-ROOK
 * U+1FA50 WHITE CHESS KNIGHT-BISHOP
 * U+1FA52 BLACK CHESS KNIGHT-ROOK
 * U+1FA53 BLACK CHESS KNIGHT-BISHOP
 */

//TODO: Thinking based on 0x88 research:
/*
 * Move generation for everything except pawns, knights, & castling:
 *
 * bishop: (+1,+1), (+1,-1), (-1,+1), (-1,-1)
 * rook: (+1,0), (-1,0), (0,+1), (0,-1)
 * queen: bishop + rook
 *
 * Or...
 *  Find candidate pieces
 *  Filter out those that aren't orthogonal or diagonal as required
 *  Use difference in co-ordinates as vector to check intervening squares
 *
 * Scanning the whole board for candidates is more work than just scanning
 * in the directions of movement. So, I should probably change to that.
 *
 * I'm not sure whether the 0x88 board representation would be significantly
 * more efficient on modern hardware.
 */

bool use_unicode = true;

int file_to_index(char c)
{
    return c - 'a';
}

int rank_to_index(int r)
{
    //TODO: Should we really be doing ASCII math?
    return 8 - (r - '0');
}

struct Bad_move: public std::runtime_error {
    using std::runtime_error::runtime_error;
    Bad_move(std::string_view sv): std::runtime_error{std::string{sv}} {}
};

struct Position {
    using Square = std::pair<char, char>;
    using Square_list = std::vector<Square>;

    using Rank = std::array<char, 8>;
    using Board = std::array<Rank, 8>;
    Board board;
    //How do we use this?
    //board.at(rank_index).at(file_index) = x
    //= board.at(rank_index).at(file_index)
    //Iteration over ranks & files within a rank
    //Should we make the board private?
    //How important would enforcing invariants be?

    //Returns the piece in the given square.
    //Returns '.' for an empty square.
    //Returns '!' for an off-the-board square.
    char get(char file, char rank) const
    {
        auto fi { file_to_index(file) };
        auto ri { rank_to_index(rank) };
        if ((fi < 0) or (fi > 7) or (ri < 0) or (ri > 7)) {
            return '!';
        }
        return board[ri][fi];
    }

    void put(char file, char rank, char piece)
    {
        try {
            auto fi { file_to_index(file) };
            auto ri { rank_to_index(rank) };
            board.at(ri).at(fi) = piece;
        } catch (...) {
            std::cout << "Exception caught in put: " << piece << static_cast<int>(file) << static_cast<int>(rank) << '\n';
        }
    }

    Square_list find(const char piece) const
    {
        Square_list list;
        for (char rank{'1'}; rank <= '8'; ++rank) {
            for (char file{'a'}; file <= 'h'; ++file) {
                if (piece == get(file, rank)) {
                    list.push_back(std::make_pair(file, rank));
                }
            }
        }
        return list;
    }
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

bool contains(std::string_view sv, char c)
{
    return sv.npos != sv.find(c);
}

struct San_bits {
    char    piece       {'\0'};
    char    from_file   {'\0'};
    char    from_rank   {'\0'};
    bool    capture     {false};
    char    to_file     {'\0'};
    char    to_rank     {'\0'};
};

std::string to_string(const San_bits& bits) {
    std::string s;

    auto add = [&s](const char& c) { if ('\0' != c) s += c; };

    add(bits.piece);
    add(bits.from_file);
    add(bits.from_rank);
    if (bits.capture) s += 'x';
    add(bits.to_file);
    add(bits.to_rank);

    return s;
}

San_bits parse_san_bits(std::string_view san)
{
    using svmatch = std::match_results<std::string_view::const_iterator>;
    std::regex rx{R"REGEX(^([KQRBN])?([a-h])?([0-8])?(x)?([a-h])([0-8])?)REGEX"};
    svmatch m;
    std::regex_search(san.begin(), san.end(), m, rx);
    San_bits bits;
    if (m.empty()) return bits;

    auto get = [&m](int i, char& c)
    {
        if (m[i].matched) c = *(m[i].first);
    };

    get(1, bits.piece);
    get(2, bits.from_file);
    get(3, bits.from_rank);
    bits.capture = m[4].matched;
    get(5, bits.to_file);
    get(6, bits.to_rank);

    return bits;
}

void fillin_pawn(San_bits& bits, const Position& pos, bool white)
{
    //TODO: to_rank is optional, but I don't want to think about that yet
    if (bits.capture) {
        //TODO: Let's wait on the capture branch...
        //TODO: Need to eventually handle en passant
        if (white) {
#if 0
            auto left { pos.get(bits.file - 1, bits.rank - 1) };
            auto right { pos.get(bits.file + 1, bits.rank - 1) };
#endif
        } else {
        }
    } else {
        //TODO: Should we check to see if from_file is set first?
        bits.from_file = bits.to_file;
        if (white) {
            if ('P' == pos.get(bits.to_file, bits.to_rank - 1)) {
                bits.from_rank = bits.to_rank - 1;
            } else if (('4' == bits.to_rank) and ('P' == pos.get(bits.to_file, '2')) and ('.' == pos.get(bits.to_file, '3'))) {
                bits.from_file = bits.to_file;
                bits.from_rank = '2';
            } else {
                throw Bad_move{to_string(bits)};
            }
        } else {
            if ('p' == pos.get(bits.to_file, bits.to_rank + 1)) {
                bits.from_rank = bits.to_rank + 1;
            } else if (('5' == bits.to_rank) and ('p' == pos.get(bits.to_file, '7')) and ('.' == pos.get(bits.to_file, '6'))) {
                bits.from_file = bits.to_file;
                bits.from_rank = '7';
            } else {
                throw Bad_move{to_string(bits)};
            }
        }
    }
}

void find_candidates(Position::Square_list& squares, const San_bits& bits, const Position& pos, const char piece, const char file_offset, const char rank_offset)
{
    char file { bits.to_file };
    char rank { bits.to_rank };
    while (true) {
        file += file_offset;
        rank += rank_offset;
        const auto contents { pos.get(file, rank) };
        if (piece == contents) {
            squares.push_back(std::make_pair(file, rank));
            return;
        }
        if ('.' != contents) {
            return;
        }
    }
}

//Note: piece is already cased by player color
Position::Square_list find_slider_candidates(San_bits& bits, const Position& pos, const char piece)
{
    using Offsets_list = std::vector<std::pair<int, int>>;
    const Offsets_list bishop_offsets{{-1,-1},{-1,1},{1,-1},{1,1}};
    const Offsets_list rook_offsets{{-1,0},{0,-1},{1,0},{0,1}};
    Position::Square_list squares;
    if ('R' != bits.piece) {
        for (const auto& offsets: bishop_offsets) {
            find_candidates(squares, bits, pos, piece, offsets.first, offsets.second);
        }
    }
    if ('B' != bits.piece) {
        for (const auto& offsets: rook_offsets) {
            find_candidates(squares, bits, pos, piece, offsets.first, offsets.second);
        }
    }
    return squares;
}

void fillin_slider(San_bits& bits, const Position& pos, const bool white)
{
    const char piece = white? std::toupper(bits.piece, std::locale()): std::tolower(bits.piece, std::locale());
    auto candidates { find_slider_candidates(bits, pos, piece) };
    if (0 == candidates.size()) throw Bad_move{to_string(bits)};
    if (1 == candidates.size()) {
        bits.from_file = candidates[0].first;
        bits.from_rank = candidates[0].second;
        return;
    }
    //TODO: Disambiguate candidates if possible
    for (const auto& candidate: candidates) {
        std::cout << "candidate: " << candidate.first << candidate.second << '\n';
    }
    throw Bad_move{"Too many candidates"};
}

//TODO: This doesn't catch many illegal moves. I'm not sure if it should.
void fillin_san_blanks(San_bits& bits, const Position& pos, bool white)
{
    if ('\0' == bits.piece) bits.piece = 'P';

    if (('\0' != bits.from_file) and ('\0' != bits.from_rank) and ('\0' != bits.to_file) and ('\0' != bits.from_file)) return;

#if 0
    auto current_occupant = pos.get(bits.to_file, bits.to_rank);
    //TODO: Should probably report if the notation didn't say it was a capture
    if ('.' != current_occupant) bits.capture = true;
#endif

    switch (bits.piece) {
        case 'P':
            fillin_pawn(bits, pos, white);
            break;
        case 'B':
        case 'R':
        case 'Q':
            fillin_slider(bits, pos, white);
            break;
        default:
            throw Bad_move{std::string{"Unrecognized piece: "} + bits.piece};
            break;
    }
}

//TODO: Is this needed? Is San_bits enough? Rename it to Move?
struct Move {
    char    piece{'.'};
    char    file{'\0'};
    int     rank{0}; // 1 to 8...normal ranks not rank indices
    // Do we care if it is a capture?
    // Do we care if it is en passant?
    // How do we handle castling?
    // Do we care which color?
    // Two "two part" moves: Castling & en passant
};

#if 0
Move san_to_move(const Position& pos, std::string_view san, bool white)
#else
San_bits san_to_move(const Position& pos, std::string_view san, bool white)
#endif
{
    //Types of moves to handle:
    //  Castling (0-0 0-0-0 O-O-O) PGN use O instead of 0
    //  e4          Pawn move
    //  e8Q         Pawn promotion; PGN uses e8=Q
    //  exd5        Pawn capture (includes starting file)
    //  exd         Pawn capture without rank
    //  exd6 e.p.   Pawn capture en passant
    //  Nf3         Piece move
    //  Bxe5        Piece capture
    //  Ngf3        Piece move with starting file
    //  Ngxf3       ...with capture
    //  N1f3        Piece move with starting rank
    //  N1xf3       ...with capture
    //  Ng1f3       Piece move with starting rank & file
    //  Ng1xf3      ...with capture
    //Needing both starting rank & file can occur in a game with either
    //3 queens or 3 knights.

    //Handle empty string
    if (san.empty()) throw Bad_move{""};

    //Parse the bits of the move
    if (('0' == san[0]) || ('O' == san[0])) {
        //TODO
        //We won't do castling yet...
        throw Bad_move{san};
    }

    auto bits { parse_san_bits(san) };

    //Resolve implicit parts of bits...
    fillin_san_blanks(bits, pos, white);

    return bits;
#if 0
    //Copy bits into move...

    Move move;

    if (not white) {
        move.piece = std::tolower(move.piece, std::locale());
    }

    return move;
#endif
}

template <typename I, typename T>
I find_not(I first, I last, T x)
{
    return std::find_if(first, last, [x](T y){ return y != x; });
}

//FEN info after the piece positions:
//  Active color: w or b
//  Castling:
//      -       neither side can castle
//      KQkq    either side can castle to king- or queen- side
//  En passant target square after a pawn 2-square move (or -)
//      This is the positon "behind" the pawn that moved.
//      After e4 it would be e3.
//  Halfmove clock: The number of half-moves since the last capture or pawn advance
//  Fullmove clock: Incremented after black's move
//X-FEN has extensions to support Chess960 & Capablanca Random Chess
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

void do_move_new(Position& pos, std::string_view input, bool& white_to_play)
{
    try {
        auto bits = san_to_move(pos, input, white_to_play);
        undo_pos = pos;
        if (not white_to_play) bits.piece = std::tolower(bits.piece, std::locale());
        white_to_play = not white_to_play;
        pos.put(bits.to_file, bits.to_rank, bits.piece);
        pos.put(bits.from_file, bits.from_rank, '.');
    } catch (const Bad_move& bad) {
        std::cout << std::quoted(input) << " is not a valid move.\n";
        std::cout << bad.what() << '\n';
    }
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
        "clear\tEmpty the board\n"
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
    bool white_to_play{true}; //TODO: Make part of position? Yes, so it gets reset with undo!
    set_position(pos, start);
    undo_pos = pos;
    print_position(pos);
    std::string line;
    std::cout << (white_to_play? "white": "black") << "> " << std::flush;
    while (std::getline(std::cin, line)) {
        if (("quit" == line) or ("exit" == line)) {
            break;
        } else if ("help" == line) {
            help();
        } else if ("reset" == line) {
            undo_pos = pos;
            set_position(pos, start);
        } else if ("clear" == line) {
            undo_pos = pos;
            set_position(pos, "8/8/8/8/8/8/8/8 w KQkq - 0 1");
        } else if ("undo" == line) {
            undo(pos);
        } else if ("unicode" == line) {
            use_unicode = true;
        } else if ("ascii" == line) {
            use_unicode = false;
        } else if ("fen" == line) {
            std::cout << pos_to_fen(pos) << '\n';
        } else {
#if 0
            do_move(pos, line);
#else
            do_move_new(pos, line, white_to_play);
#endif
        }
        print_position(pos);

        std::cout << (white_to_play? "white": "black") << "> " << std::flush;
    }
}

