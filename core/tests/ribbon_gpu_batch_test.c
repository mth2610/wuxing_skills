/* Production runtime functions are extracted to isolate device submission from
 * initialization/render dependencies. The fixture never substitutes staging logic. */
#include <stdlib.h>
int main(void) {
    return system("python3 core/tests/test_ribbon_gpu_batch.py") == 0 ? 0 : 1;
}
