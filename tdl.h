#ifndef TDL_H
#define TDL_H

#include <stdbool.h>

struct pixel {
    char chr;
    int chr_color;
    int chr_layer;
    unsigned short color;
    int layer;
};

struct tdl_texture {
    int width;
    int height;
    struct pixel* data;
};

struct tty_struct {
    int width, height;
    int vpx, vpy;
    struct pixel* buffer;
};

typedef enum {
    EVT_KEYDOWN,
    EVT_KEYUP,
    EVT_SUSPEND,
    EVT_RESUME,
    EVT_RESIZE,
    EVT_QUIT,

    EVT_MOUSE_DOWN,
    EVT_MOUSE_UP,
    EVT_MOUSE_WHEEL,

    EVT_FOCUS_GAINED,
    EVT_FOCUS_LOST
} event_type;

typedef struct {
    int type;
    int key;
    int x, y;
    int button;
    int wheel;
    double timestamp;
} tdl_event;

#define MOUSE_1 0
#define MOUSE_2 1
#define MOUSE_3 2

#define MOD_SHIFT  (1<<0)
#define MOD_CTRL   (1<<1)
#define MOD_ALT    (1<<2)
#define MOD_SUPER  (1<<3)

#define KEY_UP     1000
#define KEY_DOWN   1001
#define KEY_RIGHT  1002
#define KEY_LEFT   1003

#define KEY_SHIFT  1004
#define KEY_CTRL   1005
#define KEY_ALT    1006

int loadtty();
bool initaudio();
struct tdl_texture loadtexture(const char* path);

void free_tty();
void freetexture(struct tdl_texture* tex);

bool pollevent(tdl_event* e);

double getdelta();
void framebegin();
void fpscap(double target);

void audioplay(const char* filename);
void audiostop();
void uninitaudio();

void renderpixel(int x, int y, unsigned short color, int layer);
void renderline(int x1, int y1, int x2, int y2, unsigned short color, int layer);
void renderrect(int x, int y, int w, int h, bool fill, unsigned short color, int layer);
void rendertriangle(int x1, int y1, int x2, int y2, int x3, int y3, bool fill, unsigned short color, int layer);
void rendertext(int x, int y, const char* text, unsigned short color, int layer);
void rendertexture(int px, int py, int w, int h, float angle, struct tdl_texture* tex, int layer);
void renderclear();
void rendertty();
struct tty_struct* getty();

void tdl_pumpevents();
bool tdl_quitrequested();
void tdl_quiterminal();

#endif