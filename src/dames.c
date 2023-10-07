#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include "engine.h"

char* message[12] = {
    "to whites to move", "to blacks to move",
    "whites must eat", "blacks must eat",
    "I win !", "I win !",
    "you win !", "you win !",
    "whites thinking...", "blacks thinking...",
    "whites play this !", "blacks play this !"};

move_t list_of_moves[128];

void log_info(const char* str)
{
    fputs(str, stdout);
}

void send_str(const char* str)
{
    fputs(str, stdout);
}

//------------------------------------------------------------------------------------
// Communication between 2 instances of the game
//------------------------------------------------------------------------------------

static void init_communications(void)
{
    remove("move.chs");
    remove("white_move.chs");
    remove("black_move.chs");
}

static void transmit_move(char* move)
{
    remove("white_move.chs");
    remove("black_move.chs");

    FILE* f = fopen("move.chs", "w");
    if (f == NULL) return;
    fprintf(f, "%d: %s\n", play - 1, move);
    fflush(f);
    fclose(f);

    rename("move.chs", (play & 1) ? "white_move.chs" : "black_move.chs");
}

static int receive_move(char* move)
{
    char str[50];
    int p;
    char* file_name = (play & 1) ? "black_move.chs" : "white_move.chs";
    struct stat bstat;
    if (stat(file_name, &bstat) != 0) return 0;

    FILE* f = fopen(file_name, "r");
    if (f == NULL) return 0;
    if (fscanf(f, "%d: %s\n", &p, str) < 2) {
        fclose(f);
        return 0;
    }
    fclose(f);
    remove(file_name);
    while (stat(file_name, &bstat) == 0) continue;

    if (p != play) {
        printf("Received move %s for play %d but play is %d\n", str, p, play);
        return 0;
    }
    memcpy(move, str, strlen(str) + 1);
    return 1;
}

//------------------------------------------------------------------------------------
// Save / Load a game
//------------------------------------------------------------------------------------

static void save_game(void)
{
    FILE* f = fopen("game.checker", "w");
    if (f == NULL) {
        fprintf(stderr, "Cannot open file for writing\n");
        return;
    }
    for (int p = 0; p < nb_plays; p++) fprintf(f, "%s\n", get_move_str(p));
    fclose(f);
}

static void load_game(void)
{
    FILE* f = fopen("game.checker", "r");
    if (f == NULL) {
        fprintf(stderr, "Cannot open file for reading\n");
        return;
    }

    init_game(NULL);
    char move_str[50];
    while (1) {
        memset(move_str, 0, sizeof(move_str));
        if (fscanf(f, "%s[^\n]", move_str) == EOF) break;
        fgetc(f);  // skip '\n'
        printf("play %d: move %s\n", play, move_str);

        list_all_moves(list_of_moves);
        if (!try_move_str(move_str, list_of_moves)) break;
    }
    nb_plays = play;
    fclose(f);
}

//------------------------------------------------------------------------------------
// Graphical elements
//------------------------------------------------------------------------------------

#define MARGIN   20
#define MENU_W  120
#define BOTTOM_M 44
int SQUARE_W;
int PIECE_W;
int PIECE_M;
int TEXT_X;
int TEXT_Y;
int WINDOW_W = 0;
int WINDOW_H = 0;

static TTF_Font      *s_font, *m_font, *font;
static SDL_Window*   win = NULL;
static SDL_Texture*  tex = NULL;
static SDL_Renderer* render = NULL;
static SDL_Texture*  text_texture = NULL;
static int           mx, my;  // mouse position
static int           side_view = 0;
static int playing   = 0;

static void exit_with_message(char* error_msg)
{
    fprintf(stderr, "%s\n", error_msg);
    exit(EXIT_FAILURE);
}

static void graphical_exit(char* error_msg)
{
    if (error_msg) fprintf(stderr, "%s: %s\n", error_msg, SDL_GetError());
    if (tex)       SDL_DestroyTexture(tex);
    if (render)    SDL_DestroyRenderer(render);
    if (win)       SDL_DestroyWindow(win);
    SDL_Quit();
    if (error_msg) exit(EXIT_FAILURE);
}

unsigned char font_ttf[] = {
    #include "font_ttf.h"
};

unsigned char pieces_svg[] = {
    #include "pieces_svg.h"
};

SDL_HitTestResult is_drag_or_resize_area(SDL_Window* win, const SDL_Point* area, void* data)
{
    (void) win;
    (void) data;

    if (area->x < MARGIN) {
        if (area->y < MARGIN)             return SDL_HITTEST_RESIZE_TOPLEFT;
        if (area->y >= WINDOW_H - MARGIN) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        return SDL_HITTEST_RESIZE_LEFT;
    }
    if (area->x >= WINDOW_W - MARGIN) {
        if (area->y < MARGIN)             return SDL_HITTEST_NORMAL;
        if (area->y >= WINDOW_H - MARGIN) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        return SDL_HITTEST_RESIZE_RIGHT;
    }
    if (area->y < MARGIN)             return SDL_HITTEST_RESIZE_TOP;
    if (area->y >= WINDOW_H - MARGIN) return SDL_HITTEST_RESIZE_BOTTOM;

    if (area->x >= 2*MARGIN && area->x < 2*MARGIN + 10*SQUARE_W
     && area->y >= 2*MARGIN && area->y < 2*MARGIN + 10*SQUARE_W) {
        int c = (area->x - 2*MARGIN) / SQUARE_W;
        int l = (area->y - 2*MARGIN) / SQUARE_W;
        if (((l + c) & 1) == 0) return SDL_HITTEST_DRAGGABLE;
        int delta_x = (area->x - 2*MARGIN) % SQUARE_W;
        int delta_y = (area->y - 2*MARGIN) % SQUARE_W;
        if (delta_x < 5) return SDL_HITTEST_DRAGGABLE;
        if (delta_x >= SQUARE_W - 5) return SDL_HITTEST_DRAGGABLE;
        if (delta_y < 5) return SDL_HITTEST_DRAGGABLE;
        if (delta_y >= SQUARE_W - 5) return SDL_HITTEST_DRAGGABLE;
    }
    if (3*MARGIN + 10*SQUARE_W <= area->x && 220 <= area->y && area->y < WINDOW_H - 72)
        return SDL_HITTEST_DRAGGABLE;

    return SDL_HITTEST_NORMAL;
}

static void set_resizable_params(int w, int h)
{
    int prev_w = WINDOW_W, prev_h = WINDOW_H;

    int menu_w = (w > h + MENU_W/2) ? MENU_W : 0;
    SQUARE_W = (h - 3*MARGIN - (menu_w ? BOTTOM_M : MARGIN))/10;
    if (SQUARE_W < 38) SQUARE_W = 38;
    PIECE_W  = SQUARE_W - 4;
    PIECE_M  = (SQUARE_W - PIECE_W) / 2;
    TEXT_X   = 4*MARGIN + 10*SQUARE_W;
    TEXT_Y   = 2*MARGIN + SQUARE_W/2 - 6;
    WINDOW_W = 4*MARGIN + 10*SQUARE_W + menu_w;
    WINDOW_H = 3*MARGIN + 10*SQUARE_W + (menu_w ? BOTTOM_M : MARGIN);

    if (WINDOW_W != prev_w || WINDOW_H != prev_h) {
        // Load the checkers pieces image and scale them to the intended size
        SDL_RWops* rw_hdl = SDL_RWFromConstMem((void*)pieces_svg, sizeof(pieces_svg));
        SDL_Surface* surface = IMG_LoadSizedSVG_RW(rw_hdl, 4*PIECE_W, PIECE_W);
        if (surface == NULL) exit_with_message("error: pieces image not found");
        tex = SDL_CreateTextureFromSurface(render, surface);
        SDL_FreeSurface(surface);
        SDL_SetWindowSize(win, WINDOW_W, WINDOW_H);
    }
}

static void graphical_inits(char* name)
{
    SDL_RWops* rw_hdl;

    // Load the text fonts
    TTF_Init();

    rw_hdl = SDL_RWFromConstMem((void*)font_ttf, sizeof(font_ttf));
    s_font = TTF_OpenFontRW(rw_hdl, 1, 14);
    if (s_font == NULL) exit_with_message("error: small font not found");

    rw_hdl = SDL_RWFromConstMem((void*)font_ttf, sizeof(font_ttf));
    m_font = TTF_OpenFontRW(rw_hdl, 1, 18);
    if (m_font == NULL) exit_with_message("error: medium font not found");

    rw_hdl = SDL_RWFromConstMem((void*)font_ttf, sizeof(font_ttf));
    font   = TTF_OpenFontRW(rw_hdl, 1, 20);
    if (font == NULL) exit_with_message("error: normal font not found");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        graphical_exit("SDL init error");

    win = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 712, 606, SDL_WINDOW_RESIZABLE);
    if (!win) graphical_exit("SDL window creation error");

    render = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!render) graphical_exit("SDL render creation error");

    set_resizable_params(840, 744); // (836 for pieces drawing size of 60x60)

    SDL_SetWindowHitTest(win, is_drag_or_resize_area, NULL);

    SDL_SetWindowBordered(win, SDL_FALSE);

    // Capture also 1st click event than regains the window
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
}

static void put_text(TTF_Font* f, char* text, int x, int y)
{
    SDL_Color textColor  = {40, 40, 40, 0};
    SDL_Surface* surface = TTF_RenderText_Blended(f, text, textColor);
    SDL_Rect text_rect   = {x - surface->w/2, y, surface->w, surface->h};

    text_texture = SDL_CreateTextureFromSurface(render, surface);
    SDL_FreeSurface(surface);

    SDL_RenderCopy(render, text_texture, NULL, &text_rect);
    SDL_DestroyTexture(text_texture);
}

static int put_menu_text(char* text, int x, int y, int id)
{
    int ret = 0;

    SDL_Color textColor  = {40, 40, 40, 0};
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, textColor);
    if (x <= mx && mx < x + surface->w + 20 && y <= my && my < y + surface->h + 6) {
        SDL_Rect rect = {x, y, surface->w + 20, surface->h + 6};
        SDL_SetRenderDrawColor(render, 250, 238, 203, 255);
        SDL_RenderFillRect(render, &rect);
        ret = id;
    }
    text_texture = SDL_CreateTextureFromSurface(render, surface);
    SDL_FreeSurface(surface);

    SDL_Rect text_rect = {x + 10, y + 3, surface->w, surface->h};
    SDL_RenderCopy(render, text_texture, NULL, &text_rect);
    SDL_DestroyTexture(text_texture);

    return ret;
}

static void draw_piece(char piece, int x, int y)
{
    if (piece == ' ') return;

    // get piece zone in pieces PNG file
    char* piece_ch  = "pqPQ";
    int p           = strchr(piece_ch, piece) - piece_ch;
    SDL_Rect sprite = {p * PIECE_W, 0, PIECE_W, PIECE_W};

    SDL_Rect dest = {x, y, PIECE_W, PIECE_W};
    SDL_RenderCopy(render, tex, &sprite, &dest);
}

static int mouse_to_sq(int x, int y)
{
    if (x < 2*MARGIN) return -1;
    if (y < 2*MARGIN) return -1;

    int l = (side_view) ? 9 - (y - 2 * MARGIN) / SQUARE_W : (y - 2 * MARGIN) / SQUARE_W;
    int c = (side_view) ? 9 - (x - 2 * MARGIN) / SQUARE_W : (x - 2 * MARGIN) / SQUARE_W;

    if (c < 0 || c > 9 || l < 0 || l > 9) return -1;
    if (((c + l) & 1) == 0) return -1;

    return lc_to_sq(l, c);
}

#define MOUSE_OVER_NEW  1
#define MOUSE_OVER_PLAY 2
#define MOUSE_OVER_BACK 3
#define MOUSE_OVER_FWD  4
#define MOUSE_OVER_BOOK 5
#define MOUSE_OVER_RAND 6
#define MOUSE_OVER_VERB 7
#define MOUSE_OVER_BRD  8
#define MOUSE_OVER_BB   9
#define MOUSE_OVER_QUIT 10

static int display_board(int from)
{
    SDL_Rect full_window = {0, 0, WINDOW_W, WINDOW_H};
    SDL_Rect rect        = {MARGIN, MARGIN, 10 * SQUARE_W + 2 * MARGIN, 10 * SQUARE_W + 2 * MARGIN};
    //    char ch;

    // Detect if the mouse is over the board or its border
    int ret = 0;
    if (MARGIN <= mx && mx < 3 * MARGIN + 10 * SQUARE_W && MARGIN <= my && my < 3 * MARGIN + 10 * SQUARE_W) ret = MOUSE_OVER_BB;

    if (2 * MARGIN <= mx && mx < 2 * MARGIN + 10 * SQUARE_W && 2 * MARGIN <= my && my < 2 * MARGIN + 10 * SQUARE_W) ret = MOUSE_OVER_BRD;

    //    TTF_Font* f = (ret == MOUSE_OVER_BB) ? m_font : s_font;
    //    int indices_dy = (ret == MOUSE_OVER_BB) ? -9 : -7;

    // Clear the window
    SDL_RenderClear(render);
    SDL_SetRenderDrawColor(render, 230, 217, 181, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(render, &full_window);

    SDL_SetRenderDrawColor(render, 250, 238, 203, 255);
    SDL_RenderFillRect(render, &rect);

    int msq = mouse_to_sq(mx, my);

    rect.w = SQUARE_W;
    rect.h = SQUARE_W;

    for (int l = 0; l < 10; l++) {
        for (int c = 0; c < 10; c++) {
            rect.x = 2 * MARGIN + ((side_view) ? 9 - c : c) * SQUARE_W;
            rect.y = 2 * MARGIN + ((side_view) ? 9 - l : l) * SQUARE_W;
            if (((l + c) & 1) == 0) {
                SDL_SetRenderDrawColor(render, 230, 217, 181, 255);
                SDL_RenderFillRect(render, &rect);
                continue;
            }
            SDL_SetRenderDrawColor(render, 176, 126, 83, 255);
            SDL_RenderFillRect(render, &rect);

            int sq = lc_to_sq(l, c);
            if (sq == from) continue;  // Don't draw the piece being moved

            char p = get_piece(sq);
            if (sq == msq && (((play - playing) & 1) != !(p & 0x20)))
                 draw_piece(p, rect.x + PIECE_M, rect.y + PIECE_M - 3);
            else draw_piece(p, rect.x + PIECE_M, rect.y + PIECE_M);
        }
        //        ch = (side_view) ? 'h' - l : 'a' + l;
        //        put_text(f, &ch, 2*MARGIN + SQUARE_W/2 - 3 + l*SQUARE_W, MARGIN + MARGIN/2 + indices_dy);
        //        put_text(f, &ch, 2*MARGIN + SQUARE_W/2 - 3 + l*SQUARE_W, 2*MARGIN + 8*SQUARE_W + 2);

        //        ch = (side_view) ? '1' + l : '8' - l;
        //        put_text(f, &ch, 3*MARGIN/2, 2*MARGIN + SQUARE_W/2 + indices_dy + l*SQUARE_W);
        //        put_text(f, &ch, 5*MARGIN/2 + 8*SQUARE_W, 2*MARGIN + SQUARE_W/2 + indices_dy + l*SQUARE_W);
    }
    return ret;
}

static int display_all(int from, int x, int y)
{
    char msg_str[64];

    set_resizable_params(WINDOW_W, WINDOW_H);
    SDL_GetMouseState(&mx, &my);

    /* Display the board and the pieces that are on it */
    int ret = display_board(from);

    /* If a piece is picked by the user or is moved by move_animation(), draw it */
    if (from) {
        char piece = get_piece(from);
        if (x || y) draw_piece(piece, x, y);
        else        draw_piece(piece, mx - PIECE_W/2, my - PIECE_W/2);
    }

    /* Display buttons and texts */
    if (WINDOW_W > WINDOW_H) {
        ret += put_menu_text("New",  TEXT_X, 40, MOUSE_OVER_NEW);
        ret += put_menu_text("Play", TEXT_X, 80, MOUSE_OVER_PLAY);
        ret += put_menu_text(randomize ? "Random"  : "Ordered",   TEXT_X, 160, MOUSE_OVER_RAND);
        ret += put_menu_text(verbose   ? "Verbose" : "No trace",  TEXT_X, 200, MOUSE_OVER_VERB);
        ret += put_menu_text("Quit", TEXT_X, WINDOW_H - 72, MOUSE_OVER_QUIT);
        ret += put_menu_text(" < ", MARGIN, WINDOW_H - 40, MOUSE_OVER_BACK);
        ret += put_menu_text(" > ", 3*MARGIN + 10*SQUARE_W - 36, WINDOW_H - 40, MOUSE_OVER_FWD);
        sprintf(msg_str, "Play %d : %s", play + 1, message[2*game_state + (play & 1)]);
        put_text(font, msg_str, 2*MARGIN + 5*SQUARE_W, WINDOW_H - 34);
    }

    // Draw the exit cross
    if (WINDOW_W - 15 <= mx && mx <= WINDOW_W - 5 && 5 <= my && my <= 15) ret = MOUSE_OVER_QUIT;
    if (ret == MOUSE_OVER_QUIT) {
        SDL_Rect rect = {WINDOW_W - MARGIN, 0, MARGIN, MARGIN};
        SDL_SetRenderDrawColor(render, 250, 238, 203, 255);
        SDL_RenderFillRect(render, &rect);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
    }
    else SDL_SetRenderDrawColor(render, 176, 126, 83, 255);
    SDL_RenderDrawLine(render, WINDOW_W - 15, 5, WINDOW_W - 5, 15);
    SDL_RenderDrawLine(render, WINDOW_W - 15, 15, WINDOW_W - 5, 5);

    SDL_RenderPresent(render);
    return ret;
}

static void move_animation(char* move)
{
    move_t m;

    if (str_to_move(move, &m) == 0) return;

    user_undo_move();

    for (int s = 0; m.sq[s + 1]; s++) {
        int l0, c0, c, l, x0, y0, dx, dy;

        sq_to_lc(m.sq[s], &l0, &c0);
        x0 = 2 * MARGIN + ((side_view) ? 9 - c0 : c0) * SQUARE_W + PIECE_M;
        y0 = 2 * MARGIN + ((side_view) ? 9 - l0 : l0) * SQUARE_W + PIECE_M - 2;  // -2 for a "lift" effect :)

        sq_to_lc(m.sq[s + 1], &l, &c);

        dx = ((side_view) ? c0 - c : c - c0) * SQUARE_W;
        dy = ((side_view) ? l0 - l : l - l0) * SQUARE_W;

        for (int i = 1; i < 12; i++) {
            display_all(m.sq[s], x0 + (i * dx) / 12, y0 + (i * dy) / 12);
            SDL_Delay(5);
        }
    }
    user_redo_move();
    display_all(0, 0, 0);
}

//------------------------------------------------------------------------------------
// debug stuff
//------------------------------------------------------------------------------------

int trace = 0;

static void debug_actions(char ch)
{
    if (ch == 't') {
        trace = 1 - trace;
        return;
    }

    if (ch == 'd') {
        for (move_t* m = list_of_moves; m->sq[0]; m++)
            printf(" %s\n", move_str(*m));
    }

    int sq = mouse_to_sq(mx, my);
    if (sq < 0) return;
    set_piece(ch, sq);
}

//------------------------------------------------------------------------------------
// Handle external actions (user, other program)
//------------------------------------------------------------------------------------

static int check_from(int mx, int my, move_t* move, int* step, move_t* list)
{
    int sq = mouse_to_sq(mx, my);
    if (sq == 0) return 0;
    // The player must pick one of his pieces
    char p = get_piece(sq);
    if (p == ' ') return 0;
    if (((play - playing) & 1) && !(p & 0x20)) return 0;
    if (!((play - playing) & 1) && (p & 0x20)) return 0;

    // First step. Look in the list if a move with same first step exists
    move_t* move_in_list = list;
    if (*step == 0) {
        while (move_in_list->sq[0]) {
            if (move_in_list->sq[0] == sq) {
                move->sq[0] = sq;
                return sq;
            }
            move_in_list++;
        }
        return 0;
    }
    // next steps: start from previous step
    if (sq != move->sq[*step]) return 0;
    return sq;
}

static int check_to(int mx, int my, move_t* move, int* step, move_t* list)
{
    int sq = mouse_to_sq(mx, my);
    if (sq == 0) return 0;

    int s;
    move_t* move_in_list = list;
    while (move_in_list->sq[0]) {
        for (s = 0; s <= *step && move_in_list->sq[s] == move->sq[s]; s++) continue;
        if (s > *step) {
            if (sq == move_in_list->sq[s]) {
                if (s > 1) user_undo_move();
                move->sq[s]     = sq;
                *step           = s;
                move->sq[s + 1] = 0;
                do_move(*move);
                playing = 1;
                if (move_in_list->sq[s + 1]) return 0;
                playing = 0;
                return 1;
            }
        }
        move_in_list++;
    }
    return 0;
}

static int handle_user_turn(char* move_str)
{
    int from = 0, step = 0;  // 0 = "no piece currently picked by the user"
    int mouse_over;
    move_t move;
    list_all_moves(list_of_moves);

    int refresh = 1;
    while (1) {
        // Refresh the display
        if (refresh) mouse_over = display_all(from, 0, 0);
        refresh = 0;

        // Check if a program sent us its move
        if (receive_move(move_str))
            if (try_move_str(move_str, list_of_moves)) return ANIM_GS;

        SDL_Delay(from ? 5 : 50);

        // Handle Mouse and keyboard events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Event is 'Quit'
            if (event.type == SDL_QUIT) return QUIT_GS;

            // Event is a mouse move
            if (event.type == SDL_MOUSEMOTION) refresh = 2;

            // Event is a mouse click
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                // handle mouse over a button
                switch (mouse_over) {
                case MOUSE_OVER_NEW:
                    init_game(NULL);
                    list_all_moves(list_of_moves);
                    from = 0, step = 0;
                    break;
                case MOUSE_OVER_PLAY:
                    return THINK_GS;
                case MOUSE_OVER_BACK:
                    user_undo_move();
                    init_communications();
                    list_all_moves(list_of_moves);
                    from = 0, step = 0;
                    break;
                case MOUSE_OVER_FWD:
                    user_redo_move();
                    list_all_moves(list_of_moves);
                    from = 0, step = 0;
                    break;
                case MOUSE_OVER_RAND:
                    randomize = 1 - randomize;
                    break;
                case MOUSE_OVER_VERB:
                    verbose = 1 - verbose;
                    break;
                case MOUSE_OVER_QUIT:
                    return QUIT_GS;
                case MOUSE_OVER_BB:
                    side_view = !side_view;
                    break;
                case MOUSE_OVER_BRD:
                    from = check_from(event.button.x, event.button.y, &move, &step, list_of_moves);
                }
                refresh = 3;
            }
            else if (event.type == SDL_MOUSEBUTTONUP && from) {
                if (check_to(event.button.x, event.button.y, &move, &step, list_of_moves)) {
                    display_all(0, 0, 0);
                    return THINK_GS;
                }
                from = 0;
                refresh = 4;
            }

            // Event is a keyboard input
            else if (event.type == SDL_KEYDOWN) {
                char ch = (char)(event.key.keysym.sym);
                if (event.key.keysym.sym == SDLK_LEFT) {        // undo
                    user_undo_move();
                    init_communications();
                    list_all_moves(list_of_moves);
                    from = 0, step = 0;
                }
                else if (event.key.keysym.sym == SDLK_RIGHT) {  // redo
                    user_redo_move();
                    list_all_moves(list_of_moves);
                    from = 0, step = 0;
                }
                else if (ch == 'v' || ch == 'm' || ch == 'h' || ch == SDLK_ESCAPE) {
                    WINDOW_W = 2*WINDOW_H + MENU_W - WINDOW_W;  // toggle view mode
                }
                else if (ch <= 'z') {                           // debug
                    if (event.key.keysym.mod & KMOD_SHIFT) ch += ('A' - 'a');
                    debug_actions(ch);
                }
                refresh = 5;
            }

            // Event is a window resizing event
            else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    set_resizable_params(event.window.data1, event.window.data2);
                    refresh = 6;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------
// Main: program entry, initial setup and then game loop
//------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    (void)argc;
    char move_str[64];

    // A few inits
    char* name;
    if ((name = strrchr(argv[0], '/'))) name++;        // Linux
    else if ((name = strrchr(argv[0], '\\'))) name++;  // Windows
    else name = argv[0];
    graphical_inits(name);
    init_game(NULL);
    load_game();
    randomize = 1;
    init_communications();

    // The game loop
    while (1) {
        // To the user to play
        game_state = handle_user_turn(move_str);
        if (game_state == QUIT_GS) break;
        if (game_state == ANIM_GS) {
            move_animation(move_str);
            game_state = THINK_GS;
        }
        // To the program to play
        SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
        SDL_SetCursor(cursor);
        compute_next_move();
        cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        SDL_SetCursor(cursor);
        if (game_state <= MAT_GS) {
            transmit_move(engine_move_str);
            move_animation(engine_move_str);
        }
    }
    if (play) save_game();
    init_communications();
    graphical_exit(NULL);
    return 0;
}
