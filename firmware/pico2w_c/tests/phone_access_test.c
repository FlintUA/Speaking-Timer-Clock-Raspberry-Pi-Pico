#include "phone_access.h"
#include <assert.h>

#define TOKEN "0123456789abcdef0123456789abcdef"
#define OTHER "fedcba9876543210fedcba9876543210"

int main(void) {
    const char headers[] = "HTTP/1.1\r\nHoSt: 192.168.4.1\r\nOrigin: http://192.168.4.1";
    const char wrong[] = "HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: https://192.168.4.1";
    const char duplicate[] = "HTTP/1.1\r\nHost: 192.168.4.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1";
    assert(phone_access_same_origin(headers, sizeof(headers), "192.168.4.1"));
    assert(!phone_access_same_origin(headers, sizeof(headers) - 2u, "192.168.4.1"));
    assert(!phone_access_same_origin(headers, sizeof(headers), "192.168.2.116"));
    assert(!phone_access_same_origin(wrong, sizeof(wrong), "192.168.4.1"));
    assert(!phone_access_same_origin(duplicate, sizeof(duplicate), "192.168.4.1"));

    phone_access_t access = {0};
    assert(phone_access_pair(&access, "123456 " TOKEN, 1) == PHONE_DENIED);
    phone_access_begin(&access, 123456, 10);
    for (unsigned i = 0; i < 5; ++i)
        assert(phone_access_pair(&access, "000000 " TOKEN, 11 + i) == PHONE_DENIED);
    assert(phone_access_pair(&access, "123456 " TOKEN, 16) == PHONE_DENIED);
    phone_access_begin(&access, 123456, 20);
    assert(phone_access_pair(&access, "123456 " TOKEN, 20 + PHONE_CODE_MS) == PHONE_DENIED);
    phone_access_begin(&access, 42, 100);
    assert(phone_access_pair(&access, "000042 " TOKEN, 101) == PHONE_OK);
    assert(phone_access_pair(&access, "000042 " TOKEN, 102) == PHONE_OK);
    assert(phone_access_pair(&access, "000042 " OTHER, 103) == PHONE_DENIED);

    assert(phone_access_timer(&access, OTHER " 1 start 60", 104) == PHONE_DENIED);
    assert(phone_access_timer(&access, TOKEN " 1 start 0", 104) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 1 set 14401", 104) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 1 cancel 1", 104) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 4294967296 start 60", 104) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 1 start 60 extra", 104) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 1 start 60", 105) == PHONE_OK);
    assert(access.last.id == 1 && access.last.action == PHONE_TIMER_START && access.last.seconds == 60);
    assert(access.result == PHONE_TIMER_PENDING);
    assert(phone_access_timer(&access, TOKEN " 2 cancel 0", 106) == PHONE_BUSY);
    access.result = PHONE_TIMER_APPLIED;
    assert(phone_access_timer(&access, TOKEN " 1 start 60", 107) == PHONE_OK);
    assert(access.result == PHONE_TIMER_APPLIED); // A retry never queues it again.
    assert(phone_access_timer(&access, TOKEN " 1 start 61", 108) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 2 cancel 0", 109) == PHONE_OK);
    access.result = PHONE_TIMER_APPLIED;
    assert(phone_access_timer(&access, TOKEN " 1 start 60", 110) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 3 set 14400", 111) == PHONE_OK);
    assert(phone_access_timer(&access, TOKEN " 4 cancel 0", 111 + PHONE_SESSION_MS) == PHONE_DENIED);

    phone_access_begin(&access, 100000, 112 + PHONE_SESSION_MS);
    assert(!access.last.id && !access.token[0]);
    assert(phone_access_timer(&access, TOKEN " 1 cancel 0", 113 + PHONE_SESSION_MS) == PHONE_DENIED);
    return 0;
}
