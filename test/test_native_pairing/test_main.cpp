// Tests unitaires (hote) de la logique d'appairage sonde -> hub : forme des
// trames, filtrage des reponses, hub unique / ambigu, record NVS. Sans radio.
#include <unity.h>
#include <cstring>
#include "pairing/pairing_logic.h"

using namespace mhpair;

static const uint8_t SENSOR[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
static const uint8_t HUB_A[6]  = {0x20, 0x6E, 0xF1, 0x85, 0x58, 0x68};
static const uint8_t HUB_A_AP[6] = {0x20, 0x6E, 0xF1, 0x85, 0x58, 0x69};
static const uint8_t HUB_B[6]  = {0x24, 0x0A, 0xC4, 0x11, 0x22, 0x33};

static MeteoPairFrame response(uint32_t nonce, const uint8_t* sta, uint8_t ch,
                               const char* name = "meteohub") {
    MeteoPairFrame f;
    memset(&f, 0, sizeof(f));
    f.type = PAIR_RESPONSE;
    f.nonce = nonce;
    memcpy(f.sta_mac, sta, 6);
    memcpy(f.ap_mac, HUB_A_AP, 6);
    f.channel = ch;
    strncpy(f.name, name, sizeof(f.name) - 1);
    sealPairFrame(f);
    return f;
}

static bool roundtrip(const MeteoPairFrame& f, MeteoPairFrame* out) {
    return isValidPairFrame(reinterpret_cast<const uint8_t*>(&f), sizeof(f), out);
}

void setUp() {}
void tearDown() {}

// --- Trames : une demande scellee est reconnue, une trame abimee non ----------
void test_native_frame_roundtrip() {
    MeteoPairFrame req = makeRequest(1234, 1, SENSOR);
    MeteoPairFrame out;
    TEST_ASSERT_TRUE(roundtrip(req, &out));
    TEST_ASSERT_EQUAL_UINT8(PAIR_REQUEST, out.type);
    TEST_ASSERT_EQUAL_UINT32(1234, out.nonce);

    MeteoPairFrame bad = req;
    bad.nonce ^= 1;                                  // CRC desormais faux
    TEST_ASSERT_FALSE(roundtrip(bad, nullptr));
    TEST_ASSERT_FALSE(isValidPairFrame(reinterpret_cast<const uint8_t*>(&req),
                                       sizeof(req) - 1, nullptr)); // taille
    MeteoPacket data{};                              // trame de mesure : pas une trame d'appairage
    TEST_ASSERT_FALSE(isValidPairFrame(reinterpret_cast<const uint8_t*>(&data),
                                       sizeof(data), nullptr));
}

// --- Aucun hub : rien a enregistrer -----------------------------------------
void test_native_no_hub() {
    ResponseCollector c(42);
    TEST_ASSERT_EQUAL(Outcome::NONE, (int)c.outcome());
}

// --- Un seul hub, entendu plusieurs fois : UNIQUE, record valide ------------
void test_native_single_hub() {
    ResponseCollector c(42);
    TEST_ASSERT_TRUE(c.offer(response(42, HUB_A, 6), HUB_A));
    TEST_ASSERT_TRUE(c.offer(response(42, HUB_A, 6), HUB_A)); // demande repetee
    TEST_ASSERT_EQUAL((int)Outcome::UNIQUE, (int)c.outcome());
    const PairRecord& r = c.unique();
    TEST_ASSERT_TRUE(isValidRecord(r));
    TEST_ASSERT_EQUAL_MEMORY(HUB_A, r.sta_mac, 6);
    TEST_ASSERT_EQUAL_MEMORY(HUB_A_AP, r.ap_mac, 6);
    TEST_ASSERT_EQUAL_UINT8(6, r.channel);
    TEST_ASSERT_EQUAL_STRING("meteohub", r.name);
}

// --- Fuite sur un canal voisin : le canal ANNONCE fait foi -------------------
void test_native_announced_channel_wins() {
    ResponseCollector c(7);
    c.offer(response(7, HUB_A, 11), HUB_A);
    c.offer(response(7, HUB_A, 11), HUB_A);
    TEST_ASSERT_EQUAL_UINT8(11, c.unique().channel);
    TEST_ASSERT_TRUE(isValidRecord(c.unique()));
}

// --- Deux hubs differents : AMBIGU, on refuse --------------------------------
void test_native_two_hubs_ambiguous() {
    ResponseCollector c(42);
    c.offer(response(42, HUB_A, 1), HUB_A);
    c.offer(response(42, HUB_B, 11), HUB_B);
    TEST_ASSERT_EQUAL((int)Outcome::AMBIGUOUS, (int)c.outcome());
    TEST_ASSERT_EQUAL(2, c.hubCount());
}

// --- Reponses a rejeter -------------------------------------------------------
void test_native_rejects_bad_responses() {
    ResponseCollector c(42);
    TEST_ASSERT_FALSE(c.offer(response(41, HUB_A, 6), HUB_A));   // ancien nonce
    TEST_ASSERT_FALSE(c.offer(response(42, HUB_A, 6), HUB_B));   // MAC source != annoncee
    TEST_ASSERT_FALSE(c.offer(response(42, HUB_A, 0), HUB_A));   // canal invalide
    TEST_ASSERT_FALSE(c.offer(makeRequest(42, 1, SENSOR), SENSOR)); // pas une reponse
    const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(c.offer(response(42, bcast, 6), bcast));   // MAC non unicast
    TEST_ASSERT_EQUAL((int)Outcome::NONE, (int)c.outcome());
}

// --- Confirmation : porte le seq de reprise ----------------------------------
void test_native_confirm_carries_base_seq() {
    MeteoPairFrame f = makeConfirm(9, 1, SENSOR, 12345);
    MeteoPairFrame out;
    TEST_ASSERT_TRUE(roundtrip(f, &out));
    TEST_ASSERT_EQUAL_UINT8(PAIR_CONFIRM, out.type);
    TEST_ASSERT_EQUAL_UINT32(12345, out.base_seq);
    TEST_ASSERT_EQUAL_MEMORY(SENSOR, out.sta_mac, 6);
}

// --- Record NVS : une valeur absente ou corrompue est refusee ----------------
void test_native_record_validation() {
    PairRecord empty{};
    TEST_ASSERT_FALSE(isValidRecord(empty));          // NVS vide
    ResponseCollector c(1);
    c.offer(response(1, HUB_A, 6), HUB_A);
    PairRecord r = c.unique();
    TEST_ASSERT_TRUE(isValidRecord(r));
    r.sta_mac[5] ^= 0xFF;                              // corruption
    TEST_ASSERT_FALSE(isValidRecord(r));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_native_frame_roundtrip);
    RUN_TEST(test_native_no_hub);
    RUN_TEST(test_native_single_hub);
    RUN_TEST(test_native_announced_channel_wins);
    RUN_TEST(test_native_two_hubs_ambiguous);
    RUN_TEST(test_native_rejects_bad_responses);
    RUN_TEST(test_native_confirm_carries_base_seq);
    RUN_TEST(test_native_record_validation);
    return UNITY_END();
}
