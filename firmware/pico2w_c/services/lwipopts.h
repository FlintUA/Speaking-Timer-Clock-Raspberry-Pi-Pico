#pragma once
#ifdef CLOCK_ENABLE_PHONE_WEB
// Leave room for two status responses and their TCP copies.
#define MEM_SIZE 16384
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
