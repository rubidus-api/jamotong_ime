#include "tsf_property_guids.h"

#include <stdint.h>
#include <stdio.h>

typedef struct PortableGuid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} PortableGuid;

static int CheckLangId(void) {
    static const PortableGuid actual = LAB_GUID_PROP_LANGID_INITIALIZER;
    static const uint8_t expected_data4[8] =
        { 0xb6, 0x03, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 };

    if (actual.data1 != UINT32_C(0x3280ce20) ||
        actual.data2 != UINT16_C(0x8032) ||
        actual.data3 != UINT16_C(0x11d2)) {
        return 0;
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (actual.data4[i] != expected_data4[i]) return 0;
    }
    return 1;
}

static int CheckReading(void) {
    static const PortableGuid actual = LAB_GUID_PROP_READING_INITIALIZER;
    static const uint8_t expected_data4[8] =
        { 0xbf, 0x46, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 };

    if (actual.data1 != UINT32_C(0x5463f7c0) ||
        actual.data2 != UINT16_C(0x8e31) ||
        actual.data3 != UINT16_C(0x11d2)) {
        return 0;
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (actual.data4[i] != expected_data4[i]) return 0;
    }
    return 1;
}

int main(void) {
    if (!CheckLangId()) {
        fputs("FAIL: GUID_PROP_LANGID bytes\n", stderr);
        return 1;
    }
    if (!CheckReading()) {
        fputs("FAIL: GUID_PROP_READING bytes\n", stderr);
        return 1;
    }
    puts("TSF property GUID tests: 2 passed");
    return 0;
}
