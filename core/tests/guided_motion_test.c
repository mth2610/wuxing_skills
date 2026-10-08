/* Production composition is compiled by the Python fixture so its public
 * configuration can be extracted without pulling the rendering translation unit.
 * The fixture stubs component allocation, never movement or Emitter scheduling. */
#include <stdlib.h>
int main(void) {
    return system("python3 core/tests/test_guided_motion.py") == 0 ? 0 : 1;
}
