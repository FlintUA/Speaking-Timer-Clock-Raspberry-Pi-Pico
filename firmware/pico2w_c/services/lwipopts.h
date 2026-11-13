#pragma once
#ifdef CLOCK_ENABLE_PHONE_WEB
// Four simultaneous clients can hold status responses and TCP copies.
#define MEM_SIZE 49152
#define MEMP_NUM_TCP_PCB 10
#else
#define MEM_SIZE 8192
#endif
#include "lwipopts_examples_common.h"

#ifdef CLOCK_ENABLE_PHONE_WEB
#define HTTPD_FSDATA_FILE "pico_fsdata.inc"
#define LWIP_HTTPD_CUSTOM_FILES 1
#define LWIP_HTTPD_SUPPORT_POST 1
// Custom responses own RAM until fs_close. TCP must copy those bytes.
#define HTTP_IS_DATA_VOLATILE(hs) \
    (((hs)->handle && ((hs)->handle->flags & FS_FILE_FLAGS_CUSTOM)) ? TCP_WRITE_FLAG_COPY : 0)
#endif
