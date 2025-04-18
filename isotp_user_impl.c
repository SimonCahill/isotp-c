#include "isotp_user.h"
#include "isotp.h"
#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

void isotp_user_debug(const char* message, ...) {
    va_list args;
    va_start(args, message);
    vprintf(message, args);
    va_end(args);
}

int isotp_user_send_can(const uint32_t arbitration_id,
                       const uint8_t* data, const uint8_t size
#if ISO_TP_USER_SEND_CAN_ARG
                       ,void *arg
#endif
                      ) {
    // TODO: Implement actual CAN sending
    (void)arbitration_id;
    (void)data;
    (void)size;
    return ISOTP_RET_OK;
}

uint32_t isotp_user_get_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    static LARGE_INTEGER start = {0};
    LARGE_INTEGER now;
    
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    
    QueryPerformanceCounter(&now);
    return (uint32_t)((now.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart);
#else
    static struct timespec start = {0};
    struct timespec now;
    
    if (start.tv_sec == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((now.tv_sec - start.tv_sec) * 1000000 + 
                     (now.tv_nsec - start.tv_nsec) / 1000);
#endif
}
