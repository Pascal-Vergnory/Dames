#include <sys/time.h>
#include <x86intrin.h>  // for __rdtsc()

#include "engine.h"

// Checker pieces
enum piece_t {
    TYPE    = 3,
    PAWN    = 1,
    QUEEN   = 2,
    WHITE   = 4,
    BLACK   = 8,
    W_PAWN  = 5,
    W_QUEEN = 6,
    B_PAWN  = 9,
    B_QUEEN = 10,
    COLORS  = 12,
    STOP    = 16
};

// A checker board is the 5 squares per row times 10 rows, plus a few "dummy" squares to handle the borders.
#define BOARD_AND_BORDER_SIZE 64
#define BOARD_SIZE            56  // Smallest contiguous area we need to copy
#define FIRST_BOARD_START     16  // Some border + mem aligment...

#define MAX_TURNS             500

static char boards[FIRST_BOARD_START + BOARD_AND_BORDER_SIZE * MAX_TURNS] __attribute__((aligned(16)));

#define BOARD0 &boards[FIRST_BOARD_START]
char *board_ptr = BOARD0;
#define B(x) (*(board_ptr + (x)))

static int lc_to_sq_table[100] = {
    0, 1, 0, 2, 0, 3, 0, 4, 0, 5,
    6, 0, 7, 0, 8, 0, 9, 0, 10, 0,
    0, 12, 0, 13, 0, 14, 0, 15, 0, 16,
    17, 0, 18, 0, 19, 0, 20, 0, 21, 0,
    0, 23, 0, 24, 0, 25, 0, 26, 0, 27,
    28, 0, 29, 0, 30, 0, 31, 0, 32, 0,
    0, 34, 0, 35, 0, 36, 0, 37, 0, 38,
    39, 0, 40, 0, 41, 0, 42, 0, 43, 0,
    0, 45, 0, 46, 0, 47, 0, 48, 0, 49,
    50, 0, 51, 0, 52, 0, 53, 0, 54, 0};

static int sq_to_l_table[] = {
    0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 0,
    2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 0,
    4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 0,
    6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 0,
    8, 8, 8, 8, 8,
    9, 9, 9, 9, 9, 0};

static int sq_to_c_table[] = {
    0, 1, 3, 5, 7, 9,
    0, 2, 4, 6, 8, 0,
    1, 3, 5, 7, 9,
    0, 2, 4, 6, 8, 0,
    1, 3, 5, 7, 9,
    0, 2, 4, 6, 8, 0,
    1, 3, 5, 7, 9,
    0, 2, 4, 6, 8, 0,
    1, 3, 5, 7, 9,
    0, 2, 4, 6, 8, 0};

static int white_sq_val[] = {
   0, 0,  0,  0,  0,  0,
   28, 31, 32, 31, 28, 0,
     27, 30, 32, 30, 27,
   25, 28, 31, 28, 25, 0,
     22, 25, 27, 25, 22,
   18, 20, 22, 20, 18, 0,
     13, 14, 15, 14, 13,
    7,  7,  9,  7,  7, 0,
      0,  0,  0,  0,  0,
    0,  3,  3,  3,  3, 0};

static int black_sq_val[] = {
  0,  3,  3,  3,  3,  0,
    0,  0,  0,  0,  0, 0,
      7,  7,  9,  7,  7,
   13, 14, 15, 14, 13, 0,
     18, 20, 22, 20, 18,
   22, 25, 27, 25, 22, 0,
     25, 28, 31, 28, 25,
   27, 30, 32, 30, 27, 0,
     28, 31, 32, 31, 28,
    0,  0,  0,  0,  0, 0};


static move_t best_sequence[LEVEL_MAX + 1], best_move[LEVEL_MAX + 1], next_best[LEVEL_MAX + 1];

// Move choosen by the checker engine
char *engine_move_str;

int game_state = WAIT_GS;

int play            = 0;
int nb_plays        = 0;
int verbose         = 1;
int randomize       = 1;
int level_max_max   = LEVEL_MAX;
long time_budget_ms = 2000;
static char engine_side;

// The first moves we accept to play
static const move_t first_ply[4] = {
    {.sq[0] = 35, .sq[1] = 30, .sq[2] = 0},
    {.sq[0] = 36, .sq[1] = 30, .sq[2] = 0},
    {.sq[0] = 36, .sq[1] = 31, .sq[2] = 0},
    {.sq[0] = 37, .sq[1] = 31, .sq[2] = 0}
};

static move_t *move_ptr;

// Track of the situation at all turns
static move_t moved[MAX_TURNS];
static int board_val[MAX_TURNS];
static int nb_pieces[MAX_TURNS];

//                                       p     q
static const int piece_value[17] = {0, 100, 250, 0,
                                    0, -100, -250, 0,  // WHITE
                                    0, 100, 250, 0,    // BLACK
                                    0, 0, 0, 0, 0};

//------------------------------------------------------------------------------------
// Misc conversion functions
//------------------------------------------------------------------------------------

int str_to_move(char *str, move_t *m)
{
    int val[16];
    char sep[16];
    int i;
    int n = sscanf(str, "%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d",
                   &val[0], &sep[0],
                   &val[1], &sep[1],
                   &val[2], &sep[2],
                   &val[3], &sep[3],
                   &val[4], &sep[4],
                   &val[5], &sep[5],
                   &val[6], &sep[6],
                   &val[7], &sep[7],
                   &val[8], &sep[8],
                   &val[9], &sep[9],
                   &val[10], &sep[10],
                   &val[11], &sep[11],
                   &val[12], &sep[12],
                   &val[13], &sep[13],
                   &val[14], &sep[14],
                   &val[15]);

    if (n < 3) return 0;
    for (i = 0; i < (n + 1) / 2; i++) {
        if (val[i] < 1 || val[i] > 54) return 0;
        m->sq[i] = (char)(val[i]);
        //      m->sq[i] = m->sq[i] + (m->sq[i]/10);
    }
    if (i < 15) m->sq[i] = 0;
    return 1;
}

static char mv_str[64];
char *move_str(move_t m)
{
    memset(mv_str, 0, sizeof(mv_str));
    for (int i = 0; m.sq[i] && i < 16; i++) {
        //        std_sq = sq[i] - sq[i]/10
        if (i < 15 && m.sq[i + 1]) sprintf(mv_str + strlen(mv_str), "%d-", (int)(m.sq[i]));
        else sprintf(mv_str + strlen(mv_str), "%d", (int)(m.sq[i]));
    }
    return mv_str;
}

static char piece_char[16] = " ....PQ..pq.....";

void set_piece(char ch, int sq)
{
    char *ptr = strchr(piece_char, ch);
    if (ptr == NULL) return;
    char piece = ptr - piece_char;

    char prev = B(sq);
    B(sq)     = piece;

    if ((prev & COLORS) == 0) nb_pieces[play]++;
    else {
        board_val[play] -= piece_value[(int)prev];
        if      (prev == W_PAWN) board_val[play] += white_sq_val[sq];
        else if (prev == B_PAWN) board_val[play] -= black_sq_val[sq];
    }

    board_val[play] += piece_value[(int)piece];
    if      (piece == W_PAWN) board_val[play] -= white_sq_val[sq];
    else if (piece == B_PAWN) board_val[play] += black_sq_val[sq];
}

char get_piece(int sq)
{
    return piece_char[(int)(B(sq))];
}

char *get_move_str(int p)
{
    return move_str(moved[p]);
}

static struct timeval tv0, tv1;
static long total_ms = 0;

#define start_chrono() gettimeofday(&tv0, NULL);

static long get_chrono(void)
{
    long s, us;
    gettimeofday(&tv1, NULL);
    s  = tv1.tv_sec - tv0.tv_sec;
    us = tv1.tv_usec - tv0.tv_usec;
    return 1000 * s + us / 1000;
}

int lc_to_sq(int l, int c)
{
    return lc_to_sq_table[10 * l + c];
}

void sq_to_lc(int sq, int *l, int *c)
{
    *l    = sq_to_l_table[sq];
    *c    = sq_to_c_table[sq];
}

void print_board(char *brd)
{
    int i;
    printf("-- %3lld --------------------------\n  ", brd - BOARD0);
    for (i = -5; i < 1; i++) printf("%2d  ", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 6; i++) printf("  %2d", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 12; i++) printf("%2d  ", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 17; i++) printf("  %2d", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 23; i++) printf("%2d  ", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 28; i++) printf("  %2d", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 34; i++) printf("%2d  ", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 39; i++) printf("  %2d", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 45; i++) printf("%2d  ", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 50; i++) printf("  %2d", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 56; i++) printf("%2d  ", (int)(*(brd + i)));
    printf("\n  ");
    for (; i < 61; i++) printf("  %2d", (int)(*(brd + i)));
    printf("\n");
}

void check_borders(char *msg, move_t m)
{
    if (B(0) != 16 || B(11) != 16 || B(22) != 16 || B(33) != 16 || B(44) != 16) {
        printf("%s: border error ! move is ", msg);
        printf("  %d", m.sq[0]);
        for (int i = 1; m.sq[i]; i++) printf(" - %d", m.sq[i]);
        print_board(board_ptr - 64);
        print_board(board_ptr);
    }
}

//------------------------------------------------------------------------------------
// Pengy hash used for the openings book and transposition table
//------------------------------------------------------------------------------------

static uint64_t pengyhash(const void *p, size_t size, uint32_t seed)
{
    uint64_t b[4] = {0};
    uint64_t s[4] = {0, 0, 0, size};
    int i;

    for (; size >= 32; size -= 32, p = (const char *)p + 32) {
        memcpy(b, p, 32);

        s[1] = (s[0] += s[1] + b[3]) + (s[1] << 14 | s[1] >> 50);
        s[3] = (s[2] += s[3] + b[2]) + (s[3] << 23 | s[3] >> 41);
        s[3] = (s[0] += s[3] + b[1]) ^ (s[3] << 16 | s[3] >> 48);
        s[1] = (s[2] += s[1] + b[0]) ^ (s[1] << 40 | s[1] >> 24);
    }

    memcpy(b, p, size);

    for (i = 0; i < 6; i++) {
        s[1] = (s[0] += s[1] + b[3]) + (s[1] << 14 | s[1] >> 50) + seed;
        s[3] = (s[2] += s[3] + b[2]) + (s[3] << 23 | s[3] >> 41);
        s[3] = (s[0] += s[3] + b[1]) ^ (s[3] << 16 | s[3] >> 48);
        s[1] = (s[2] += s[1] + b[0]) ^ (s[1] << 40 | s[1] >> 24);
    }

    return s[0] + s[1] + s[2] + s[3];
}

//------------------------------------------------------------------------------------
// Transposition Table management
//------------------------------------------------------------------------------------

// Transposition table to stores move choices for each encountered board situations
#define NEW_BOARD   0
#define OTHER_DEPTH 1
#define UPPER_BOUND 2
#define LOWER_BOUND 3
#define EXACT_VALUE 4

typedef struct {
    union {
        uint64_t hash;      // Pengy hash. Useless LSB taken for depth & flag => 16B table entry size
        struct {            // /!\ struct for little endian CPU !
            uint8_t  depth;
            uint8_t  flag;
            uint16_t dummy[3];
        };
    };
    union {
        int32_t move_val;
        struct {            // /!\ struct for little endian CPU !
            char sq[3];
            char move_index;
        };
    };
    int32_t  eval;
} table_t;

#define TABLE_ENTRIES (1 << 23) // 8 Mega entries.x 16B = 128 MB memory
static table_t table[TABLE_ENTRIES] __attribute__((aligned(16)));

static int nb_dedup;
static int nb_hash;

static int get_table_entry(int depth, int side, int* flag, int* eval, int* move_index, move_t* list)
{
    move_t* move;

    // compute the hash of the board. Seed = side to play
    uint64_t hash = pengyhash((void *)board_ptr, BOARD_SIZE, side);

    // Look if the hash is in the transposition table
    int h = hash & (TABLE_ENTRIES - 1);
    if ((table[h].hash ^ hash) < TABLE_ENTRIES) {
        // To reduce hash collisions, reject an entry with impossible move
        move = list + table[h].move_index;
        if (table[h].sq[0] == move->sq[0] && table[h].sq[1] == move->sq[1] && table[h].sq[2] == move->sq[2]) {
            // Only entries with same depth search are usable, but a move
            // from other depth search is interesting (example: PV move)
            *flag = (table[h].depth == depth) ? table[h].flag : OTHER_DEPTH;
            *eval = table[h].eval;
            *move_index = (int)(table[h].move_index);
            return h;
        }
    }

    // The hash was not present or was for another board. Set the table entry
    table[h].hash     = hash;
    table[h].move_val = 0;
    *flag          = NEW_BOARD;
    nb_hash++;
    return h;
}

//------------------------------------------------------------------------------------
// Set a board using a FEN string
//------------------------------------------------------------------------------------

static void FEN_to_board(char *str)
{
    int sq, fm;

    // skip the board occupancy
    char ch, color;
    char *str0 = str;
    while (*str++ != ' ') continue;

    // get the playing side
    color = *str++;
    str++;

    // get the "full moves" counter and deduce the ply
    for (fm = 0; (ch = *str++) >= '0'; fm = fm * 10 + ch - '0') continue;
    play      = 2 * (fm - 1) + (color == 'w') ? 0 : 1;
    nb_plays  = play;
    board_ptr = BOARD0 + BOARD_AND_BORDER_SIZE * play;

    // Now that we know the ply, empty the board ...
    for (sq = 1; sq < 55; sq++)
        if (sq % 11) set_piece(' ', sq);

    // ... and fill it with the board occupancy information
    sq = 1;
    while ((ch = *str0++) != ' ') {
        if (ch == '/') continue;
        else if (ch > '0' && ch < '6') sq += ch - '0';
        else set_piece(ch, sq++);
        if ((sq % 11) == 0) sq++;
    }
}

//------------------------------------------------------------------------------------
// Game init
//------------------------------------------------------------------------------------

void init_game(char *FEN_string)
{
    memset(boards, STOP, sizeof(boards));  // Set the boards to all borders
    memset(table, 0, sizeof(table));       // Reset the transposition table

    if (FEN_string) FEN_to_board(FEN_string);
    else FEN_to_board("ppppp/ppppp/ppppp/ppppp/5/5/PPPPP/PPPPP/PPPPP/PPPPP w 0");

    game_state     = WAIT_GS;
    time_budget_ms = 2000;
    total_ms       = 0;
    randomize      = 1;
    level_max_max  = LEVEL_MAX;
}

//------------------------------------------------------------------------------------
// Make the move, undo it, redo it
//------------------------------------------------------------------------------------

void do_move(move_t m)
{
    // Remember the whole previous game to be able to undo the move
    memcpy(board_ptr + BOARD_AND_BORDER_SIZE, board_ptr, BOARD_SIZE);
    board_ptr += BOARD_AND_BORDER_SIZE;

    int s, d, sq;
    int piece = B(m.sq[0]);

    moved[play] = m;
    play++;

    B(m.sq[0]) = 0;

    board_val[play] = board_val[play - 1];
    nb_pieces[play] = nb_pieces[play - 1];

    char amplitude = m.sq[1] - m.sq[0];

    // Handle simple move without eating
    if (-10 < amplitude && amplitude < 10) {

        if (piece == W_PAWN) {
            board_val[play] += white_sq_val[(int)m.sq[0]];
            if (m.sq[1] > 5) board_val[play] -= white_sq_val[(int)m.sq[1]];
            else {
                board_val[play] += piece_value[W_QUEEN] - piece_value[W_PAWN]; // Handle pawn going to queen.
                piece = W_QUEEN;
            }
        }
        else if (piece == B_PAWN) {
            board_val[play] -= black_sq_val[(int)m.sq[0]];
            if (m.sq[1] < 50) board_val[play] += white_sq_val[(int)m.sq[1]];
            else {
                board_val[play] += piece_value[B_QUEEN] - piece_value[B_PAWN]; // Handle pawn going to queen.
                piece = B_QUEEN;
            }
        }

        B(m.sq[1]) = piece;
        check_borders("1", m);
        return;
    }

    // Handle queen move or eating move with potentially many steps
    for (s = 0; m.sq[s + 1]; s++) {
        // Determine move step direction
        int sq1 = m.sq[s];
        int sq2 = m.sq[s + 1];
        int c1 = sq_to_c_table[sq1];
        int c2 = sq_to_c_table[sq2];
        d = (sq2 > sq1) ? ((c2 > c1) ? 6 : 5) : ((c2 > c1) ? -5 : -6);

        // Walk through the step line and handle any eaten piece in the way
        for (sq = m.sq[s] + d; sq != m.sq[s + 1]; sq += d) {
            if (B(sq)) {
                board_val[play] -= piece_value[(int)B(sq)];
                if      (B(sq) == W_PAWN) board_val[play] += white_sq_val[sq];
                else if (B(sq) == B_PAWN) board_val[play] -= black_sq_val[sq];
                B(sq) = 0;
                nb_pieces[play]--;
            }
        }
    }

    if (piece == W_PAWN) {
        board_val[play] += white_sq_val[(int)m.sq[0]];
        if (m.sq[s] > 5) board_val[play] -= white_sq_val[(int)m.sq[s]];
        else {
            board_val[play] += piece_value[W_QUEEN] - piece_value[W_PAWN]; // Handle pawn going to queen.
            piece = W_QUEEN;
        }
    }
    else if (piece == B_PAWN) {
        board_val[play] -= black_sq_val[(int)m.sq[0]];
        if (m.sq[s] < 50) board_val[play] += white_sq_val[(int)m.sq[s]];
        else {
            board_val[play] += piece_value[B_QUEEN] - piece_value[B_PAWN]; // Handle pawn going to queen.
            piece = B_QUEEN;
        }
    }

    B(m.sq[s]) = piece;
}

static inline void undo_move(void)
{
    play--;
    board_ptr -= BOARD_AND_BORDER_SIZE;
}

void user_undo_move(void)
{
    if (play) undo_move();
}

void user_redo_move(void)
{
    if (play >= nb_plays) return;
    play++;
    board_ptr += BOARD_AND_BORDER_SIZE;
}

//------------------------------------------------------------------------------------
// List possible moves
//------------------------------------------------------------------------------------

int directions[4] = {5, 6, -5, -6};
int max_step;

void check_eat(int color, int from, int step, move_t *list)
{
    char eaten         = 0;
    move_ptr->sq[step] = from;

    for (int dir = 0; dir < 4; dir++) {
        int d = directions[dir];
        if ((B(from + d) & color) && B(from + d + d) == 0) {
            eaten       = B(from + d);
            B(from + d) = 0;
            check_eat(color, from + d + d, step + 1, list);
            B(from + d) = eaten;
        }
    }

    if (eaten == 0) {
        if (step == 0 || step < max_step) return;
        move_ptr->sq[step + 1] = 0;
        if (step == max_step) {
            *(move_ptr + 1) = *move_ptr;
            move_ptr++;
        }
        else {
            *(list)     = *move_ptr;
            *(list + 1) = *move_ptr;
            move_ptr    = list + 1;
            max_step    = step;
        }
    }
}

void check_queen_eat(int color, int from, int step, move_t *list)
{
    int eat_sq, to;
    char eaten         = 0;
    move_ptr->sq[step] = from;

    for (int dir = 0; dir < 4; dir++) {
        int d = directions[dir];
        for (eat_sq = from + d; B(eat_sq) == 0; eat_sq += d) continue;
        if ((B(eat_sq) & color) && B(eat_sq + d) == 0) {
            eaten     = B(eat_sq);
            B(eat_sq) = 0;
            for (to = eat_sq + d; B(to) == 0; to += d) check_queen_eat(color, to, step + 1, list);
            B(eat_sq) = eaten;
        }
    }

    if (eaten == 0) {
        if (step == 0 || step < max_step) return;
        move_ptr->sq[step + 1] = 0;
        if (step == max_step) {
            *(move_ptr + 1) = *move_ptr;
            move_ptr++;
        }
        else {
            *(list)     = *move_ptr;
            *(list + 1) = *move_ptr;
            move_ptr    = list + 1;
            max_step    = step;
        }
    }
}

static void list_eats(int from, move_t *list)
{
    char piece = B(from);
    char other = (piece & COLORS) ^ COLORS;

    if (piece & QUEEN) check_queen_eat(other, from, 0, list);
    else check_eat(other, from, 0, list);
}

void check_move(int from, int to)
{
    if (B(to) == 0) {
        move_ptr->sq[0] = from;
        move_ptr->sq[1] = to;
        move_ptr->sq[2] = 0;
        move_ptr++;
    }
}
static void list_moves(int from)
{
    char piece = B(from);
    switch (piece) {
    case W_PAWN:
        check_move(from, from - 6);
        check_move(from, from - 5);
        break;
    case B_PAWN:
        check_move(from, from + 5);
        check_move(from, from + 6);
        break;
    case W_QUEEN:
    case B_QUEEN:
        for (int to = from + 5; B(to) == 0; to += 5) check_move(from, to);
        for (int to = from + 6; B(to) == 0; to += 6) check_move(from, to);
        for (int to = from - 5; B(to) == 0; to -= 5) check_move(from, to);
        for (int to = from - 6; B(to) == 0; to -= 6) check_move(from, to);
        break;
    }
}

//------------------------------------------------------------------------------------
// List all moves and indicate if there are none possible
//------------------------------------------------------------------------------------

int list_all_moves(move_t *list_of_moves)
{
    int side = (play & 1) ? BLACK : WHITE;
    int from;
    max_step = 0;

    // List the longuest possible eats
    move_ptr = list_of_moves;
    for (from = 1; from < 55; from++) {
        if (B(from) & side) list_eats(from, list_of_moves);
    }
    move_ptr->val = 0;
    if (max_step > 0) return EATS_GS;

    // If there are no eats, list all possible moves
    for (from = 1; from < 55; from++) {
        if (B(from) & side) list_moves(from);
    }
    move_ptr->val = 0;

    // If there is no possible eat or move, this side has lost
    if (move_ptr == list_of_moves) return MAT_GS;
    return WAIT_GS;
}

//------------------------------------------------------------------------------------
// Do the move, but only if it is legal
//------------------------------------------------------------------------------------

static int try_move(move_t move, move_t *list_of_moves)
{
    int s;
    move_t *move_in_list;

    for (move_in_list = list_of_moves; move_in_list->sq[0]; move_in_list++) {
        for (s = 0; move_in_list->sq[s] && move_in_list->sq[s] == move.sq[s]; s++) continue;
        if (move_in_list->sq[s] == 0) {
            do_move(move);
            nb_plays = play;
            return 1;
        }
    }
    return 0;
}

int try_move_str(char *move_str, move_t *list_of_moves)
{
    move_t move;
    if (str_to_move(move_str, &move) == 0) return 0;
    if (!try_move(move, list_of_moves)) return 0;
    log_info_va("Play %d: <- %s\n", play, move_str);
    return 1;
}

//------------------------------------------------------------------------------------
// Board Evaluation
//------------------------------------------------------------------------------------

static int evaluate(int side)
{
    // Take the total of the values of the pieces present on the board
    return (side == BLACK) ? board_val[play] : -board_val[play];

    // Add strategic evaluation only if close to alpha and beta
    // if (side == BLACK) { if ( res > b + 170 ||  res < a - 170) return  res; }
    // else               { if (-res > b + 170 || -res < a - 170) return -res; }

    // Strategic criterions: TODO
}

//------------------------------------------------------------------------------------
// The min-max recursive algo with alpha-beta pruning
//------------------------------------------------------------------------------------

static int level_max;
static int ab_moves, next_ab_moves_time_check;

static int nega_alpha_beta(int level, int a, int b, int side, move_t *upper_sequence)
{
    int i, state, eval, flag, max = -300000 + level, one_possible = 0;
    move_t list_of_moves[128];
    move_t sequence[LEVEL_MAX];
    move_t *m;
    move_t mm_move;
    mm_move.sq[0] = 0;
    int move_index = -1;

    state = list_all_moves(list_of_moves);

    // If we can't move, we'v lost!
    if (state == MAT_GS) return max;

    // Last level: finish eatings if any, then evaluate the board
    int depth = level_max - level;
    if (depth <= 0 && state != EATS_GS) return evaluate(side);

    int nb_moves = move_ptr - list_of_moves;
    move_ptr->sq[0] = 0;

    // Search the board in the transposition table
    int h = get_table_entry(depth, side, &flag, &eval, &move_index, list_of_moves);

    int old_a = a;
    if (flag == LOWER_BOUND)      { if (a < eval) a = eval; }
    else if (flag == UPPER_BOUND) { if (b > eval) b = eval; }
    if (flag == EXACT_VALUE || (a >= b && flag > OTHER_DEPTH)) {
        nb_dedup++;
        mm_move          = *(list_of_moves + move_index);
        next_best[level] = best_move[level];
        best_move[level] = mm_move;
        sequence[level]  = mm_move;
        memcpy(upper_sequence, sequence, level_max * sizeof(move_t));  // reductible...
        return eval;
    }

    // Set the Futility level
    // int futility = 300000;  // by default, no futility
    // if (depth == 1 && !check && nb_pieces[play] > 23)
    //    futility = 50 + ((side == BLACK) ? board_val[play] : -board_val[play]);

    // Try the transposition table move first
    if (move_index >= 0) {
        m = list_of_moves + move_index;

        // set the board with this possible move
        do_move(*m);

        one_possible = 1;
        eval = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence);

        // undo the move to evaluate the others
        undo_move();

        // The player wants to maximize his score
        if (eval > max) {
            max              = eval;  // max = max( max, eval )
            mm_move          = *m;
            next_best[level] = best_move[level];
            best_move[level] = mm_move;
            sequence[level]  = mm_move;
            memcpy(upper_sequence, sequence, level_max * sizeof(move_t));

            if (max >= b) goto end_update_tt;
            if (max > a) a = max;
        }
    }

    // Try each possible move
    m = list_of_moves;
    if (randomize) m += ((((int)__rdtsc()) & 0x7FFFFFFF) % nb_moves);

    for (i = 0; i < nb_moves; i++, m++) {
        if (m->sq[0] == 0) m = list_of_moves;
        if (m == list_of_moves + move_index) continue;

        // Futility pruning
        // if (futility < max && one_possible && state == EATS_GS) continue;

        // set the board with this possible move
        do_move(*m);

        // evaluate this move
        if (one_possible == 0) {
            one_possible = 1;
            eval         = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence);
        }
        else {
            eval = -nega_alpha_beta(level + 1, -a - 1, -a, side ^ COLORS, sequence);
            if (a < eval && eval < b && depth > 2)
                eval = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence);
        }

        // undo the move to evaluate the others
        undo_move();

        // Every 10000 moves, look at elapsed time
        if (++ab_moves > next_ab_moves_time_check) {
            // if time's up, stop search and keep previous lower depth search move
            if (get_chrono() >= time_budget_ms) return -400000;
            next_ab_moves_time_check = ab_moves + 10000;
        }

        // The player wants to maximize his score
        if (eval > max) {
            max              = eval;  // max = max( max, eval )
            mm_move          = *m;
            move_index       = m - list_of_moves;
            next_best[level] = best_move[level];
            best_move[level] = mm_move;
            sequence[level]  = mm_move;
            memcpy(upper_sequence, sequence, level_max * sizeof(move_t));

            if (max >= b) break;
            if (max > a) a = max;
        }
    }
end_update_tt:
    table[h].eval     = max;
    table[h].move_val = mm_move.val;
    table[h].move_index  = move_index;
    table[h].depth    = depth;
    if  (max <= old_a) table[h].flag = UPPER_BOUND;
    else if (max >= b) table[h].flag = LOWER_BOUND;
    else               table[h].flag = EXACT_VALUE;
    return max;
}

//------------------------------------------------------------------------------------
// The compute engine : how we'll call the min-max recursive algo
//------------------------------------------------------------------------------------

void compute_next_move(void)
{
    move_t engine_move;

    engine_side = (play & 1) ? BLACK : WHITE;

    // Don't waist time thinking for the 1st move.
    if (play == 0) {
        engine_move.val = first_ply[rand() % 4].val;
        goto play_the_prefered_move;
    }

    // Verify the situation...
    move_t list_of_moves[128];
    if (list_all_moves(list_of_moves) == MAT_GS) {
        game_state = LOST_GS;
        return;
    }

    // Don't waist time if there is only one choice
    if (move_ptr == list_of_moves + 1) {
        engine_move = *list_of_moves;
        goto play_the_prefered_move;
    }

    long level_ms = 0, elapsed_ms = 0;
    start_chrono();

    // Search deeper and deeper the best move,
    // starting with the previous "best" move to improve prunning
    level_max       = 0;
    engine_move.val = 0;

    do {
        best_move[level_max].val = 0;
        next_best[level_max].val = 0;
        level_max++;
        ab_moves                 = 0;
        next_ab_moves_time_check = ab_moves + 10000;

        int max = nega_alpha_beta(0, -400000, 400000, engine_side, best_sequence);
        engine_move = best_sequence[0];
        if (engine_move.val == 0) {
            game_state = MAT_GS;
            return;
        }

        level_ms   = -elapsed_ms;
        elapsed_ms = get_chrono();
        level_ms  += elapsed_ms;

        if (verbose) {
            send_str_va("%2d %7d %4ld %8d ", level_max, max, elapsed_ms / 10, ab_moves);
            for (int l = 0; l < level_max && l < 13; l++)
                send_str_va(" %s", move_str(best_sequence[l]));
            send_str("\n");
        }

        // If a check-mat is un-avoidable, no need to think more
        if (max > 199800 || max < -199800) break;

        // Evaluate if we have time for the next search level
        if (level_ms * 3 > time_budget_ms - elapsed_ms) break;
    } while (level_max <= LEVEL_MAX);
    total_ms += elapsed_ms;

play_the_prefered_move:
    //    try_move(engine_move, list_of_moves);
    do_move(engine_move);
    nb_plays        = play;
    engine_move_str = move_str(engine_move);

    log_info_va("Play %d: -> %s\n", play, engine_move_str);
    log_info_va("Total think time: %d min %d sec %d ms\n", (int)(total_ms / 60000), (int)((total_ms / 1000) % 60), (int)(total_ms % 1000));

    // Return with the opponent side situation
    game_state = list_all_moves(list_of_moves);
}
