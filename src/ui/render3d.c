/* ZenithOS v6.0 - ui/render3d.c - rotating 3D neural grid (Q8.8 fixed point).
 *
 * Pipeline per frame:
 *   1. rotate every lattice vertex around Y then X (fixed_sin/fixed_cos,
 *      angle argument is 0..255 == 0..2*PI, result is Q8.8)
 *   2. perspective-project: p = pos * FOCAL / (z + VIEWER)
 *   3. draw lattice edges with vga_line, nodes as pulsing 3x3 sprites
 *   4. pace ~33 fps via the PIT counter, exit on ESC (edge-latched keys)
 *
 * All drawing goes through the VGA driver (back buffer + vga_swap); timing
 * comes from the PIT driver; keys come from the keyboard driver. No floating
 * point anywhere.
 */
#include "ui/render3d.h"
#include "drivers/vga.h"
#include "drivers/pit.h"
#include "drivers/keyboard.h"
#include "hal/x86/cpu.h"
#include "lib/math.h"
#include "lib/string.h"
#include "config.h"

#define GRID_N      3            /* neurons per axis: 27 nodes                  */
#define NODE_COUNT  (GRID_N * GRID_N * GRID_N)
#define PITCH       (3 * 256 / 2)    /* lattice spacing 1.5 units, Q8.8         */
#define VIEWER      (5 * 256)        /* viewer distance 5 units, Q8.8            */
#define FOCAL       190              /* projection focal length, pixels         */

/* Lattice edges: 54 axis links + 4 body diagonals as long-range synapses. */
#define EDGE_COUNT  (3 * 2 * GRID_N * GRID_N + 4)

typedef struct { int32_t x, y, z; } vec3_t;   /* Q8.8 world space */

static vec3_t nodes[NODE_COUNT];
static int edge_a[EDGE_COUNT], edge_b[EDGE_COUNT];
static int edge_count = 0;

static int node_index(int i, int j, int k) { return (i * GRID_N + j) * GRID_N + k; }

static void link(int ia, int ja, int ka, int ib, int jb, int kb) {
    if (edge_count >= EDGE_COUNT) return;
    edge_a[edge_count] = node_index(ia, ja, ka);
    edge_b[edge_count] = node_index(ib, jb, kb);
    edge_count++;
}

static void build_mesh(void) {
    int i, j, k;
    for (i = 0; i < GRID_N; i++)
        for (j = 0; j < GRID_N; j++)
            for (k = 0; k < GRID_N; k++) {
                vec3_t *v = &nodes[node_index(i, j, k)];
                v->x = (int32_t)(i - 1) * PITCH;
                v->y = (int32_t)(j - 1) * PITCH;
                v->z = (int32_t)(k - 1) * PITCH;
            }
    for (i = 0; i < GRID_N; i++)
        for (j = 0; j < GRID_N; j++)
            for (k = 0; k < GRID_N; k++) {
                if (i + 1 < GRID_N) link(i, j, k, i + 1, j, k);
                if (j + 1 < GRID_N) link(i, j, k, i, j + 1, k);
                if (k + 1 < GRID_N) link(i, j, k, i, j, k + 1);
            }
    /* long-range "synapses": the four body diagonals of the cube */
    link(0, 0, 0, 2, 2, 2);
    link(2, 0, 0, 0, 2, 2);
    link(0, 2, 0, 2, 0, 2);
    link(2, 2, 0, 0, 0, 2);
}

/* Rotate v around Y by ay, then around X by ax, project to screen. */
static void project(const vec3_t *v, int *px, int *py, uint8_t ax, uint8_t ay) {
    int32_t cosY = fixed_cos(ay), sinY = fixed_sin(ay);
    int32_t cosX = fixed_cos(ax), sinX = fixed_sin(ax);
    int32_t x = fixed_mul(v->x, cosY) + fixed_mul(v->z, sinY);
    int32_t z = fixed_mul(-v->x, sinY) + fixed_mul(v->z, cosY);
    int32_t y = fixed_mul(v->y, cosX) - fixed_mul(z, sinX);
    int32_t z2 = fixed_mul(v->y, sinX) + fixed_mul(z, cosX);
    int32_t denom = z2 + VIEWER;
    if (denom < 256) denom = 256;          /* never divide by ~0 */
    *px = VGA_WIDTH / 2 + (int32_t)((fixed_div(x, denom) * FOCAL) >> 8);
    *py = VGA_HEIGHT / 2 + (int32_t)((fixed_div(y, denom) * FOCAL) >> 8);
}

static void draw_frame(uint8_t ax, uint8_t ay, uint32_t t) {
    static const uint8_t node_col[3] = { COL_CYAN, COL_WHITE, COL_MAGENTA };
    int px[NODE_COUNT], py[NODE_COUNT];
    int i, e;

    for (i = 0; i < NODE_COUNT; i++) project(&nodes[i], &px[i], &py[i], ax, ay);

    for (e = 0; e < edge_count; e++) {
        int a = edge_a[e], b = edge_b[e];
        /* one edge in seven fires per frame: travelling pulse */
        uint8_t fire = (uint8_t)((t >> 1) + (uint32_t)e) % 7u;
        vga_line(px[a], py[a], px[b], py[b], fire ? COL_TEAL : COL_WHITE);
    }

    for (i = 0; i < NODE_COUNT; i++) {
        uint8_t c = node_col[(uint32_t)((t >> 2) + i) % 3u];
        vga_fill_rect(px[i] - 1, py[i] - 1, 3, 3, c);
    }
}

void render3d_run(void) {
    if (edge_count == 0) build_mesh();     /* first call initialises the mesh */
    keyboard_clear_all();                  /* ignore buffered shell keystrokes */

    for (;;) {
        uint32_t t = pit_ticks();
        uint8_t ay = (uint8_t)(t >> 1);               /* ~5 s per full turn   */
        uint8_t ax = (uint8_t)((t >> 1) + (t >> 4));  /* tumbling second axis */

        vga_clear(COL_BLACK);
        draw_frame(ax, ay, t);
        vga_puts(4, 4, "ZENITHOS  NEURAL  GRID  3D", COL_CYAN, COL_BLACK, 1);
        vga_puts(4, VGA_HEIGHT - 12, "ESC: back to shell", COL_GRAY, COL_BLACK, 1);
        vga_swap();

        if (keyboard_pressed_clear(KEY_ESC)) break;

        /* pace to ~33 fps: sleep with interrupts on, wake on any IRQ */
        {
            uint32_t next = t + 3;   /* 3 ticks == 30 ms at PIT_HZ 100 */
            while ((int32_t)(pit_ticks() - next) < 0 && !keyboard_has_char())
                cpu_hlt();
        }
    }

    keyboard_clear_all();
    vga_clear(COL_BG);
    vga_swap();
}
