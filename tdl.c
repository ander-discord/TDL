#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <string.h>
#include <time.h>
#include <math.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "tdl.h"

#define EVENT_MAX 64
#define KEY_MAX   65536

static tdl_event event_queue[EVENT_MAX];
static int evt_head = 0;
static int evt_tail = 0;

static bool key_active[KEY_MAX];
static bool key_seen[KEY_MAX];
static unsigned int key_mods = 0;

static double delta_time = 0.0;
static double last_time = 0.0;

static struct tty_struct tty;
bool needed_resize = false;

static volatile sig_atomic_t quit_requested = 0;

static struct termios oldt;

static ma_engine engine;    

static void processinp(void);

struct tty_struct* getty() {
    return &tty;
}

static void rgb565_to_rgb888(unsigned short color, int* r, int* g, int* b) {
    *r = ((color >> 11) & 0x1F) * 255 / 31;
    *g = ((color >> 5) & 0x3F) * 255 / 63;
    *b = (color & 0x1F) * 255 / 31;
}

static unsigned short rgb888_to_rgb565(int r, int g, int b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void pushevent(tdl_event e) {
    int next = (evt_head + 1) % EVENT_MAX;
    if (next == evt_tail) return;
    event_queue[evt_head] = e;
    evt_head = next;
}

bool pollevent(tdl_event* e) {
    if (evt_tail == evt_head)
        return false;

    *e = event_queue[evt_tail];
    evt_tail = (evt_tail + 1) % EVENT_MAX;
    return true;
}

bool tdl_quitrequested() {
    return quit_requested;
}

void tdl_pumpevents() {
    processinp();
}

static double now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

double getdelta() {
    return delta_time;
}

void framebegin() {
    double t = now();
    if (last_time == 0.0) {
        last_time = t;
        delta_time = 0.0;
        return;
    }
    delta_time = t - last_time;
    last_time = t;
}

void fpscap(double target) {
    double end = now();
    double frame = end - last_time;
    double wait = target - frame;
    if (wait > 0) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = wait * 1e9;
        nanosleep(&ts, NULL);
    }
}

static void onquit(int sig) {
    quit_requested = 1;
}

static void onresume(int sig) {
    tdl_event e = { .type = EVT_RESUME };
    pushevent(e);
}

static void onsuspend(int sig) {
    tdl_event e = { .type = EVT_SUSPEND };
    pushevent(e);
}

static void onresize(int sig) {
    tdl_event e = { .type = EVT_RESIZE };
    pushevent(e);
    needed_resize = true;
}

void tdl_quiterminal() {
    // put back
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    // enable focus reporting
    printf("\033[?1004l");

    // reset all text formatting
    printf("\033[0m\033[?25h");
    printf("\033[?1000l");
    printf("\033[?1006l");

    fflush(stdout);
}

static void parsemouse(const char* buf, int n) {
    int b, x, y;
    char type;

    // check if format is correct
    if (sscanf(buf, "\033[<%d;%d;%d%c", &b, &x, &y, &type) != 4) return;

    // create event plus calculate position andd put data on event
    tdl_event e;
    e.x = x - 1;
    e.y = (y - 1) * 2;

    e.button = b & 3;

    // check if it is mouse up or down or wheel
    if (type == 'M') {
        // if this is wheel or down
        if (b == 64 || b == 65) {
            // check wheel goes up or down
            e.type = EVT_MOUSE_WHEEL;
            e.wheel = (b == 64) ? 1 : -1;
        } else {
            e.type = EVT_MOUSE_DOWN;
        }
    } else {
        e.type = EVT_MOUSE_UP;
    }

    pushevent(e);
}

static void processinp() {
    // set variables
    char buf[64];
    int n;

    // clear key seen list
    memset(key_seen, 0, sizeof(key_seen));

    while ((n = read(STDIN_FILENO, buf, sizeof(buf)-1)) > 0) {
        // check if Ctrl + C or no more input
        if (n < 0) {
            if (errno == EINTR) break;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }
        buf[n] = '\0';
        // check if this input is input mouse
        if (buf[0] == 27 && buf[1] == '[' && buf[2] == '<') {
            parsemouse(buf, n);
            continue;
        }

        // check if focus gained or list
        if (buf[0] == 27 && buf[1] == '[' && buf[2] == 'I') { pushevent((tdl_event){ .type = EVT_FOCUS_GAINED }); continue; }
        if (buf[0] == 27 && buf[1] == '[' && buf[2] == 'O') { pushevent((tdl_event){ .type = EVT_FOCUS_LOST }); continue; }

        // check if key is valid
        int key = -1;

        if (buf[0] == 27 && n >= 3 && buf[1] == '[') {
            if (buf[2] == 'A') key = KEY_UP;
            else if (buf[2] == 'B') key = KEY_DOWN;
            else if (buf[2] == 'C') key = KEY_RIGHT;
            else if (buf[2] == 'D') key = KEY_LEFT;
            else continue;
        } else {
            key = (unsigned char)buf[0];
        }

        if (key == KEY_SHIFT) key_mods |= MOD_SHIFT;
        if (key == KEY_CTRL)  key_mods |= MOD_CTRL;
        if (key == KEY_ALT)   key_mods |= MOD_ALT;

        if (key < 0 || key >= KEY_MAX) continue;

        // set key seen in list
        key_seen[key] = true;

        // push event
        if (!key_active[key]) {
            key_active[key] = true;
            tdl_event e = { .type = EVT_KEYDOWN, .key = key };
            pushevent(e);
        }
    }

    for (int k = 0; k < KEY_MAX; k++) {
        if (key_active[k] && !key_seen[k]) {
            key_active[k] = false;
            tdl_event e = { .type = EVT_KEYUP, .key = k };
            pushevent(e);
        }
    }
}

int loadtty() {
    // get tty size
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        tty.width = w.ws_col;
        tty.height = w.ws_row * 2;
    }
    else {
        printf("TDL: Not possible to get tty size\n");
        return -1;
    }

    // allocate buffer
    tty.buffer = malloc(tty.width * tty.height * sizeof(struct pixel));
    if (!tty.buffer) {
        printf("TDL: Failed to allocate buffer\n");
        return 1;
    }
    else {
        // raw mode & non blocking
        struct termios raw;
        tcgetattr(STDIN_FILENO, &oldt);
        raw = oldt;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);

        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

        // enable support mouse
        printf("\033[?1000h");
        printf("\033[?1006h");

        // enable focus reporting
        printf("\033[?1004h");

        // put signals
        signal(SIGINT, onquit);
        signal(SIGTSTP, onsuspend);
        signal(SIGCONT, onresume);
        signal(SIGWINCH, onresize);

        return 0;
    }
}

bool initaudio() {
    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) return false;
    return true;
}

void free_tty() {
    if (tty.buffer) free(tty.buffer);
    tty.buffer = NULL;
}

void setviewport(int x, int y) {
    tty.vpx = x;
    tty.vpy = y;
}

void renderpixel(int x, int y, unsigned short color, int layer) {
    if (!tty.buffer) return;

    x -= tty.vpx;
    y -= tty.vpy;
    if (x < 0 || y < 0 || x >= tty.width || y >= tty.height) return;

    int idx = y * tty.width + x;

    if (layer >= tty.buffer[idx].layer) {
        tty.buffer[idx].color = color;
        tty.buffer[idx].layer = layer;
    }
}

void renderline(int x1, int y1, int x2, int y2, unsigned short color, int layer) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        renderpixel(x1, y1, color, layer);

        if (x1 == x2 && y1 == y2)
            break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void renderrect(int x, int y, int w, int h, bool fill, unsigned short color, int layer) {
    if (fill) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                renderpixel(x + j, y + i, color, layer);
            }
        }
    }
    else {
        renderline(x, y, x + w - 1, y, color, layer);
        renderline(x, y + h - 1, x + w - 1, y + h - 1, color, layer);
        renderline(x, y, x, y + h - 1, color, layer);
        renderline(x + w - 1, y, x + w - 1, y + h - 1, color, layer);
    }
}

void rendertriangle(int x1, int y1, int x2, int y2, int x3, int y3, bool fill, unsigned short color, int layer) {
    if (!fill) {
        renderline(x1, y1, x2, y2, color, layer);
        renderline(x2, y2, x3, y3, color, layer);
        renderline(x3, y3, x1, y1, color, layer);
        return;
    }

    if (y1 > y2) { int t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y2 > y3) { int t; t = y2; y2 = y3; y3 = t; t = x2; x2 = x3; x3 = t; }
    if (y1 > y2) { int t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }

    int total_height = y3 - y1;
    if (total_height == 0) return;

    for (int y = y1; y <= y3; y++) {
        float alpha = (float)(y - y1) / total_height;
        int sx = x1 + (int)((x3 - x1) * alpha);

        if (y < y2) {
            int sh = y2 - y1;
            float beta = sh ? (float)(y - y1) / sh : 0;
            int ex = x1 + (int)((x2 - x1) * beta);
            if (sx > ex) { int t = sx; sx = ex; ex = t; }
            for (int x = sx; x <= ex; x++) renderpixel(x, y, color, layer);
        }
        else {
            int sh = y3 - y2;
            float beta = sh ? (float)(y - y2) / sh : 0;
            int ex = x2 + (int)((x3 - x2) * beta);
            if (sx > ex) { int t = sx; sx = ex; ex = t; }
            for (int x = sx; x <= ex; x++) renderpixel(x, y, color, layer);
        }
    }
}

void rendercircle(int cx, int cy, int r, bool fill, unsigned short color, int layer) {
    if (fill) {
        for (int y = -r; y <= r; y++) {
            int dx = (int)sqrt(r * r - y * y);
            for (int x = -dx; x <= dx; x++) {
                renderpixel(cx + x, cy + y, color, layer);
            }
        }
        return;
    }

    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        renderpixel(cx + x, cy + y, color, layer);
        renderpixel(cx + y, cy + x, color, layer);
        renderpixel(cx - y, cy + x, color, layer);
        renderpixel(cx - x, cy + y, color, layer);
        renderpixel(cx - x, cy - y, color, layer);
        renderpixel(cx - y, cy - x, color, layer);
        renderpixel(cx + y, cy - x, color, layer);
        renderpixel(cx + x, cy - y, color, layer);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        }
        else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void rendertext(int x, int y, const char* text, unsigned short color, int layer) {
    if (!tty.buffer) return;

    // calculate viewport position
    int cx = x;
    int cy = y;
    cx -= tty.vpx;
    cy -= tty.vpy;

    if (cy % 2 != 0) cy--;

    // print
    for (int i = 0; i < strlen(text); i++) {
        char c = text[i];

        // if character is \n then cy += 2
        if (c == '\n') {
            cx = x;
            cy += 2;
            continue;
        }

        // check if on border
        if (cx < 0 || cx >= tty.width || cy < 0 || cy >= tty.height) break;

        // get index
        int idx = cy * tty.width + cx;
        
        if (layer >= tty.buffer[idx].chr_layer) {
            tty.buffer[idx].chr = c;
            tty.buffer[idx].chr_color = color;
            tty.buffer[idx].chr_layer = layer;
        }

        cx++;
    }
}

struct tdl_texture loadtexture(const char* path) {
    struct tdl_texture tex = { 0 };

    // load image via stb_image.h
    int w, h, c;
    unsigned char* img = stbi_load(path, &w, &h, &c, 4);
    if (!img) return tex;

    // set "metadata" & allocate
    tex.width = w;
    tex.height = h;
    tex.data = malloc(w * h * sizeof(struct pixel));
    if (!tex.data) {
        stbi_image_free(img);
        return tex;
    }

    // load image to tdl_texture
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 4;
            int r = img[i + 0];
            int g = img[i + 1];
            int b = img[i + 2];
            int a = img[i + 3];

            struct pixel* p = &tex.data[y * w + x];
            p->layer = -1;

            if (a > 0) {
                p->color = rgb888_to_rgb565(r, g, b);
                p->layer = 0;
            }
        }
    }

    stbi_image_free(img);
    return tex;
}

void rendertexture(int px, int py, int w, int h, float angle, struct tdl_texture* tex, int layer) {
    float rad = angle * M_PI / 180.0f;
    float cos_theta = cosf(rad);
    float sin_theta = sinf(rad);

    int cx_tex = tex->width / 2;
    int cy_tex = tex->height / 2;
    int cx_dst = w / 2;
    int cy_dst = h / 2;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = x - cx_dst;
            float dy = y - cy_dst;

            float src_fx = cos_theta * dx + sin_theta * dy + cx_tex;
            float src_fy = -sin_theta * dx + cos_theta * dy + cy_tex;

            int tx = (int)src_fx;
            int ty = (int)src_fy;

            if (tx < 0 || ty < 0 || tx >= tex->width || ty >= tex->height)
                continue;

            struct pixel* p = &tex->data[ty * tex->width + tx];
            if (p->layer < 0) continue;

            renderpixel(px + x, py + y, p->color, layer);
        }
    }
}

void audioplay(const char* filename) {
    ma_engine_play_sound(&engine, filename, NULL);
}

void audiostop() {
    ma_engine_stop(&engine);
}

void uninitaudio() {
    ma_engine_uninit(&engine);
}

void freetexture(struct tdl_texture* tex) {
    if (tex->data) free(tex->data);
    tex->data = NULL;
}

void renderclear() {
    for (int i = 0; i < tty.width * tty.height; i++) {
        tty.buffer[i].layer = -1;
        tty.buffer[i].color = 0;
        tty.buffer[i].chr = 0;
        tty.buffer[i].chr_layer = -1;
    }
}

void rendertty() {
    if (!tty.buffer) return;
    if (needed_resize) {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) return;
        if (w.ws_col <= 0 || w.ws_row <= 0) return;

        int new_w = w.ws_col;
        int new_h = w.ws_row * 2;

        if (new_w == tty.width && new_h == tty.height) { needed_resize = false; return; }

        struct pixel* newbuf = realloc(tty.buffer, new_w * new_h * sizeof(struct pixel));

        if (!newbuf) { needed_resize = false; return; }

        tty.buffer = newbuf;
        tty.width = new_w;
        tty.height = new_h;

        renderclear();
        needed_resize = false;
    }

    for (int y = 0; y < tty.height; y += 2) {
        int last_tr = -1, last_tg = -1, last_tb = -1, last_br = -1, last_bg = -1, last_bb = -1;

        for (int x = 0; x < tty.width; x++) {
            // get pixels
            struct pixel* top = &tty.buffer[y * tty.width + x];
            struct pixel* bot = (y + 1 < tty.height) ? &tty.buffer[(y + 1) * tty.width + x] : NULL;

            // rgb565 > rgb888
            int tr = 0, tg = 0, tb = 0, br = 0, bg = 0, bb = 0;
            if (top->layer >= 0) rgb565_to_rgb888(top->color, &tr, &tg, &tb);
            if (bot && bot->layer >= 0) rgb565_to_rgb888(bot->color, &br, &bg, &bb);

            // get average between top and down if there character
            int bg_r = br, bg_g = bg, bg_b = bb;
            if (top->chr) { bg_r = (tr + br) / 2; bg_g = (tg + bg) / 2; bg_b = (tb + bb) / 2; }

            // set font color
            int fr, fg, fb;
            rgb565_to_rgb888(top->chr_color, &fr, &fg, &fb);

            // move pointer
            printf("\033[%d;%dH", y / 2 + 1, x + 1);

            if (top->chr) {
                // character
                printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm%c",
                    fr, fg, fb,
                    bg_r, bg_g, bg_b,
                    top->chr);
            }
            else {
                // pixel
                if (tr != last_tr || tg != last_tg || tb != last_tb || br != last_br || bg != last_bg || bb != last_bb) {
                    printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm", tr, tg, tb, br, bg, bb);
                    last_tr = tr; last_tg = tg; last_tb = tb;
                    last_br = br; last_bg = bg; last_bb = bb;
                }
                printf("▀");
            }
        }
    }

    fflush(stdout);
}
