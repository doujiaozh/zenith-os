/* ZenithOS v6.0 - render3d: interactive 3D wireframe renderer.
 *
 * A rotating 3x3x3 "neural lattice" drawn with fixed-point (Q8.8) rotation
 * matrices and a perspective projection, using the VGA driver's Bresenham
 * line primitives. The animation is paced by the PIT tick counter; the loop
 * returns to the caller when the user presses ESC.
 */
#ifndef ZENITH_RENDER3D_H
#define ZENITH_RENDER3D_H

/* Run the 3D neural-grid demo. Blocks until ESC is pressed; on return the
 * screen has been cleared (COL_BG) and the keyboard state flushed. */
void render3d_run(void);

#endif /* ZENITH_RENDER3D_H */
