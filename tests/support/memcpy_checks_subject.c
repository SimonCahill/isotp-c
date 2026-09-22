/* Include libc's declarations and fortified inline wrappers before redirecting
 * calls in the implementation. A command-line rename also renames the wrappers,
 * which can inline the real memmove and bypass the stub in optimized builds.
 */
#include <string.h>

void* isotp_test_memmove(void* destination, const void* source, size_t count);

#define memmove isotp_test_memmove
#include "../../isotp.c"
