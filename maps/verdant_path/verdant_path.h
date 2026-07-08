#ifndef VERDANT_PATH_MAP_H
#define VERDANT_PATH_MAP_H

// A grass field with a stone path and scattered rocks — 100m x 75m
// rectangle (diagonal 125m), sized so a corner-to-corner walk at the game
// screen's 3.5 m/s pace takes ~36s (see verdant_path.c for the math).
void InitVerdantPathMap(void);
void DrawVerdantPathMap(void);

#endif // VERDANT_PATH_MAP_H
