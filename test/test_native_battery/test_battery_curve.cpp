// Test hote de la courbe Li-ion : bornes, repere nominal, monotonie.
#include <unity.h>
#include "battery_curve.h"

void setUp() {}
void tearDown() {}

void test_bornes() {
    TEST_ASSERT_EQUAL_UINT8(100, mhbatt::liIonPercent(4.20f));
    TEST_ASSERT_EQUAL_UINT8(100, mhbatt::liIonPercent(4.30f));
    TEST_ASSERT_EQUAL_UINT8(0, mhbatt::liIonPercent(3.27f));
    TEST_ASSERT_EQUAL_UINT8(0, mhbatt::liIonPercent(2.50f));
}

void test_reperes() {
    TEST_ASSERT_EQUAL_UINT8(15, mhbatt::liIonPercent(3.71f));  // palier exact
    TEST_ASSERT_EQUAL_UINT8(50, mhbatt::liIonPercent(3.84f));
    // Le releve reel : 3,859 V = environ 56 %, et non 78 % comme l'ancienne droite.
    const uint8_t p = mhbatt::liIonPercent(3.859f);
    TEST_ASSERT_TRUE(p >= 55 && p <= 58);
}

void test_monotone() {
    uint8_t prev = 0;
    for (float v = 3.0f; v <= 4.3f; v += 0.005f) {
        const uint8_t p = mhbatt::liIonPercent(v);
        TEST_ASSERT_TRUE(p >= prev);  // jamais de baisse quand la tension monte
        prev = p;
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bornes);
    RUN_TEST(test_reperes);
    RUN_TEST(test_monotone);
    return UNITY_END();
}
