/* Behavioral data tests use production header/helpers; no GPU/window required. */
#include <stdlib.h>
int main(void) {return system("python3 core/tests/test_ribbon_appearance.py")==0?0:1;}
