# TDL (Terminal DirectMedia Layer, renders all in terminal/tty)

GitHub repo: https://github.com/ander-discord/TDL

---

## Platforms

- Linux

---

## TTY

### `int loadtty()`

Initializes the terminal  
Returns 0 on success.

Return values:
- `-1` — not possible to get tty size
- `1` — failed to allocate buffer

---

### `tdl_quiterminal`

Restore terminal.

---

### `struct tty_struct* getty()`

Returns tty.

---

## Timing

### `void framebegin()`

Updates delta time (call once per frame)

---

### `double getdelta()`

Returns delta time (secounds)

---

### `void fpscap(double target)`

Caps frame time to a target duration (secounds)

Recommend: 30 fps

Example:
```c
fpscap(1.0 / 30.0);
````

---

## Event system

### `void tdl_pumpevents()`

Reads keyboard, mouse, focus, if tty quit, and tty resize.

---

### `bool pollevent(tdl_event* e)`

Retrieves the next event if available.
Returns false when queue is empty.

---

### Event types

* `EVT_KEYDOWN`
* `EVT_KEYUP`
* `EVT_MOUSE_DOWN`
* `EVT_MOUSE_UP`
* `EVT_MOUSE_WHEEL`
* `EVT_RESIZE`
* `EVT_FOCUS_GAINED`
* `EVT_FOCUS_LOST`
* `EVT_SUSPEND`
* `EVT_RESUME`

---

## Rendering

### Color Format

TDL uses RGB565

---

### `void renderclear()`

Clears framebuffer

---

### `void rendertty()`

Flushes framebuffer to tty

---

## Utilities

### `void settysize(int w, int h)`

Renders a warning overlay when the current terminal size does not match the expected size

---

## Drawing

```c
void renderpixel(int x, int y, unsigned short color, int layer);
void renderline(int x1, int y1, int x2, int y2, unsigned short color, int layer);
void renderrect(int x, int y, int w, int h, bool fill, unsigned short color, int layer);
void rendertriangle(int x1, int y1, int x2, int y2, int x3, int y3, bool fill, unsigned short color, int layer);
void rendercircle(int cx, int cy, int r, bool fill, unsigned short color, int layer);
```

---

### Text

```c
void rendertext(int x, int y, const char* text, unsigned short color, int layer);
```

Supports newlines

---

## Viewport

```c
void setviewport(int x, int y);
```

Moves viewport

---

## Textures

(Powered by stb_image)

```c
void rendertexture(int px, int py, int w, int h, float angle, struct tdl_texture* tex, int layer);
void freetexture(struct tdl_texture* tex);
```

---

## Audio

(Powered by miniaudio)

---

### `bool initaudio()`

Initializes the audio engine

---

### `void audioplay(const char* filename);`

Plays a sound file

---

### `void audiostop();`

Stops all sounds

---

## Example

```c
#include "tdl.h"
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

int main() {
    if (loadtty() != 0) return 1;
    struct tty_struct* t = getty();
    if (!t->buffer) return 1;

    double ttime = 0.0;
    bool quit = false;
    bool first = true;

    char message[64] = "Q to quit";

    while (!quit && !tdl_quitrequested()) {
        framebegin();
        t = getty();
        int width = t->width;
        int height = t->height;

        renderclear();

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                unsigned short r = (unsigned short)((sin(ttime + x * 0.1) * 0.5 + 0.5) * 31);
                unsigned short g = (unsigned short)((sin(ttime + y * 0.1) * 0.5 + 0.5) * 63);
                unsigned short b = (unsigned short)((sin(ttime + (x + y) * 0.05) * 0.5 + 0.5) * 31);
                unsigned short color = (r << 11) | (g << 5) | b;
                renderpixel(x, y, color, 0);
            }
        }

        renderrect(0, 0, strlen(message) + 2, 4, true, 0xf800, 1);
        rendertext(0, 0, message, 0xffff, 2);

        rendertty();
        ttime += getdelta();

        tdl_pumpevents();

        tdl_event e;
        while (pollevent(&e)) {
            if (e.type == EVT_KEYDOWN) {
                int len = strlen(message);

                if (first) {
                    first = false;
                    message[0] = '\\0';
                    len = 0;
                }

                if (len < 63 && e.key >= 32 && e.key <= 126) {
                    message[len] = (char)e.key;
                    message[len + 1] = '\\0';
                }

                if (e.key == 'q' || e.key == 'Q')
                    quit = true;
            }
        }

        fpscap(1.0 / 20.0);
    }

    tdl_quiterminal();
    free_tty();
    return 0;
}
```
