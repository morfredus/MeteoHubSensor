// Tests hote de la decision OTA cote sonde et de la trame OtaOffer. Sans radio.
#include <unity.h>
#include <cstring>
#include "ota/ota_logic.h"

using namespace mhota;

static OtaOffer offer(uint32_t fw, uint32_t size = 900000) {
    OtaOffer o;
    memset(&o, 0, sizeof(o));
    o.node_id = 1;
    o.fw_version = fw;
    o.size = size;
    strcpy(o.md5, "0123456789abcdef0123456789abcdef");
    sealOtaOffer(o);
    return o;
}

void setUp() {}
void tearDown() {}

void test_frame_roundtrip() {
    OtaOffer o = offer(encodeFwVersion("0.33.0")), back;
    TEST_ASSERT_TRUE(isValidOtaOffer(reinterpret_cast<uint8_t*>(&o), sizeof(o), &back));
    TEST_ASSERT_EQUAL_UINT32(o.fw_version, back.fw_version);
    reinterpret_cast<uint8_t*>(&o)[10] ^= 0xFF;  // corruption
    TEST_ASSERT_FALSE(isValidOtaOffer(reinterpret_cast<uint8_t*>(&o), sizeof(o), nullptr));
    TEST_ASSERT_FALSE(isValidOtaOffer(reinterpret_cast<uint8_t*>(&o), sizeof(o) - 1, nullptr));
}

void test_accept_new_version() {
    TEST_ASSERT_EQUAL((int)Verdict::ACCEPT,
        (int)decide(offer(0x002100), 0x002000, true, 3.9f, 0, 0));
}

void test_same_version_refused() {
    TEST_ASSERT_EQUAL((int)Verdict::SAME_VERSION,
        (int)decide(offer(0x002000), 0x002000, true, 3.9f, 0, 0));
}

void test_low_battery_refused_but_unmeasured_allowed() {
    TEST_ASSERT_EQUAL((int)Verdict::LOW_BATTERY,
        (int)decide(offer(0x002100), 0x002000, true, 3.3f, 0, 0));
    TEST_ASSERT_EQUAL((int)Verdict::ACCEPT,
        (int)decide(offer(0x002100), 0x002000, false, 0.0f, 0, 0));
}

void test_try_cap_per_target() {
    TEST_ASSERT_EQUAL((int)Verdict::TOO_MANY_TRIES,
        (int)decide(offer(0x002100), 0x002000, true, 3.9f, 0x002100, MAX_TRIES_PER_TARGET));
    // une AUTRE version repart de zero
    TEST_ASSERT_EQUAL((int)Verdict::ACCEPT,
        (int)decide(offer(0x002200), 0x002000, true, 3.9f, 0x002100, MAX_TRIES_PER_TARGET));
}

void test_bad_offer() {
    TEST_ASSERT_EQUAL((int)Verdict::BAD_OFFER, (int)decide(offer(0), 0x002000, true, 3.9f, 0, 0));
    TEST_ASSERT_EQUAL((int)Verdict::BAD_OFFER,
        (int)decide(offer(0x002100, MAX_IMAGE_SIZE + 1), 0x002000, true, 3.9f, 0, 0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_frame_roundtrip);
    RUN_TEST(test_accept_new_version);
    RUN_TEST(test_same_version_refused);
    RUN_TEST(test_low_battery_refused_but_unmeasured_allowed);
    RUN_TEST(test_try_cap_per_target);
    RUN_TEST(test_bad_offer);
    return UNITY_END();
}
