#ifndef _ENGINE
#define _ENGINE

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Common definitions

// game states
#define WAIT_GS  0
#define EATS_GS  1
#define MAT_GS   2
#define WIN_GS   2
#define LOST_GS  3
#define THINK_GS 4
#define ANIM_GS  5
#define QUIT_GS  6

#define LEVEL_MAX 63

// Move structure and move tables

typedef struct {
    union {
        char sq[16];
        struct {            // /!\ struct for little endian CPU !
            int32_t val;
            int32_t dummy[3];
        };
    };
} move_t;

// Common variables : game settings

extern int verbose;
extern int randomize;
extern int level_max_max;
extern int trace;

// Common variables : game current state

extern int game_state;
extern char* engine_move_str;
extern int play;
extern int nb_plays;
extern long time_budget_ms;

// Chess engine functions

void init_game(char* FEN_string);
int try_move_str(char* move_str, move_t* list_of_moves);
void do_move(move_t m);
void compute_next_move(void);

// Play interface helper functions

int lc_to_sq(int l, int c);
void sq_to_lc(int sq, int* l, int* c);
char *move_str(move_t m);
int str_to_move(char* str, move_t* m);
void set_piece(char ch, int sq);
char get_piece(int sq);
void user_undo_move(void);
void user_redo_move(void);
char* get_move_str(int play);
int list_all_moves(move_t* list);

void log_info(const char* str);
void send_str(const char* str);

#define log_info_va(...)              \
    do {                              \
        char str_va[64];              \
        sprintf(str_va, __VA_ARGS__); \
        log_info(str_va);             \
    } while (0)
#define send_str_va(...)              \
    do {                              \
        char str_va[64];              \
        sprintf(str_va, __VA_ARGS__); \
        send_str(str_va);             \
    } while (0)

#endif
