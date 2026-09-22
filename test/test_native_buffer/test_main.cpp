// Tests unitaires (hote) du buffer de securite de la sonde : MeasurementStore.
// Couvre la logique pure de synchronisation, sans LittleFS ni radio :
// ajout, rotation, filigrane cumulatif, retransmission bornee, persistance,
// cas-limite buffer plein, absence de doublon/de perte de PENDING.
#include <unity.h>
#include <vector>
#include <set>
#include "sync/measurement_store.h"

using namespace mhs;

// Unity appelle setUp/tearDown autour de chaque test (rien a preparer ici).
void setUp() {}
void tearDown() {}

static StoredRecord mk(uint32_t seq, uint32_t ts) {
    StoredRecord r{};
    r.seq = seq;
    r.sensor_ts = ts;
    r.t = 20.0f + seq;
    r.h = 50.0f;
    r.p = 1013.0f;
    return r;
}

static uint32_t bytesFor(uint32_t cap) {
    return sizeof(StoreHeader) + cap * sizeof(StoredRecord);
}

// --- 1. Ajout simple + relecture chronologique -----------------------------
void test_native_append_and_order() {
    MemoryBlobStore mem(bytesFor(8));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 8));
    for (uint32_t s = 1; s <= 5; s++) TEST_ASSERT_TRUE(store.append(mk(s, s * 300)));
    TEST_ASSERT_EQUAL_UINT32(5, store.count());
    StoredRecord r;
    for (uint32_t i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(store.recordAt(i, r));
        TEST_ASSERT_EQUAL_UINT32(i + 1, r.seq);
    }
}

// --- 8/9. Remplissage puis rotation : le ring ecrase le plus ancien --------
void test_native_ring_overwrite() {
    MemoryBlobStore mem(bytesFor(4));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 4));
    for (uint32_t s = 1; s <= 6; s++) TEST_ASSERT_TRUE(store.append(mk(s, s * 300)));
    TEST_ASSERT_EQUAL_UINT32(4, store.count());
    StoredRecord r;
    TEST_ASSERT_TRUE(store.recordAt(0, r));
    TEST_ASSERT_EQUAL_UINT32(3, r.seq); // 1,2 ecrases
    TEST_ASSERT_TRUE(store.recordAt(3, r));
    TEST_ASSERT_EQUAL_UINT32(6, r.seq);
}

// --- Filigrane cumulatif : marque tout seq <= ack --------------------------
void test_native_mark_synced_cumulative() {
    MemoryBlobStore mem(bytesFor(8));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 8));
    for (uint32_t s = 1; s <= 6; s++) store.append(mk(s, s * 300));
    TEST_ASSERT_EQUAL_UINT32(6, store.unsyncedCount());
    TEST_ASSERT_TRUE(store.markSyncedUpTo(4));
    TEST_ASSERT_EQUAL_UINT32(2, store.unsyncedCount());
    TEST_ASSERT_EQUAL_UINT32(5, store.oldestUnsyncedSeq());
    TEST_ASSERT_FALSE(store.markSyncedUpTo(4)); // idempotent, pas de recul
    TEST_ASSERT_FALSE(store.markSyncedUpTo(2)); // jamais de recul du filigrane
}

// --- 4/7. Retransmission bornee et ordonnee (recuperation apres retour hub) -
void test_native_select_retransmit_bounded() {
    MemoryBlobStore mem(bytesFor(16));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 16));
    for (uint32_t s = 1; s <= 10; s++) store.append(mk(s, s * 300));
    store.markSyncedUpTo(3);
    StoredRecord out[4];
    uint32_t n = store.selectRetransmit(0, 4, out);
    TEST_ASSERT_EQUAL_UINT32(4, n);
    TEST_ASSERT_EQUAL_UINT32(4, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(7, out[3].seq); // borne : 8..10 au prochain reveil
}

// --- Retransmission a partir d'un seq demande par le hub (want_from) --------
void test_native_select_from_hint() {
    MemoryBlobStore mem(bytesFor(16));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 16));
    for (uint32_t s = 1; s <= 10; s++) store.append(mk(s, s * 300));
    StoredRecord out[8];
    uint32_t n = store.selectRetransmit(7, 8, out);
    TEST_ASSERT_EQUAL_UINT32(4, n);
    TEST_ASSERT_EQUAL_UINT32(7, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(10, out[3].seq);
}

// --- 10. Absence de doublon dans une selection -----------------------------
void test_native_no_duplicates_in_selection() {
    MemoryBlobStore mem(bytesFor(32));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 32));
    for (uint32_t s = 1; s <= 20; s++) store.append(mk(s, s * 300));
    StoredRecord out[20];
    uint32_t n = store.selectRetransmit(0, 20, out);
    TEST_ASSERT_EQUAL_UINT32(20, n);
    std::set<uint32_t> seen;
    for (uint32_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(seen.insert(out[i].seq).second); // aucun seq en double
        if (i) TEST_ASSERT_TRUE(out[i].seq > out[i - 1].seq); // strictement croissant
    }
}

// --- 2/3. Persistance : le buffer survit a un "reboot" ---------------------
void test_native_survives_reboot() {
    std::vector<uint8_t> disk(bytesFor(8), 0xFF);
    {
        MemoryBlobStore mem(&disk);
        MeasurementStore store;
        TEST_ASSERT_TRUE(store.begin(&mem, 8));
        for (uint32_t s = 1; s <= 5; s++) store.append(mk(s, s * 300));
        store.markSyncedUpTo(2);
    }
    {
        MemoryBlobStore mem(&disk);
        MeasurementStore store;
        TEST_ASSERT_TRUE(store.begin(&mem, 8));
        TEST_ASSERT_EQUAL_UINT32(5, store.count());
        TEST_ASSERT_EQUAL_UINT32(2, store.ackWatermark());   // filigrane conserve
        TEST_ASSERT_EQUAL_UINT32(3, store.oldestUnsyncedSeq());
    }
}

// --- 11. Cas-limite : buffer plein d'unacked -> drop explicite du plus ancien
void test_native_full_of_pending_drops_oldest_explicitly() {
    MemoryBlobStore mem(bytesFor(4));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 4));
    for (uint32_t s = 1; s <= 4; s++) store.append(mk(s, s * 300)); // plein, tout PENDING
    TEST_ASSERT_EQUAL_UINT32(0, store.droppedPending());

    TEST_ASSERT_TRUE(store.append(mk(5, 1500))); // ecrase seq=1 (PENDING)
    TEST_ASSERT_EQUAL_UINT32(1, store.droppedPending()); // compte, pas silencieux
    TEST_ASSERT_EQUAL_UINT32(1, store.lastDroppedSeq());

    // La mesure fraiche est bien la, la plus ancienne restante est seq=2.
    TEST_ASSERT_EQUAL_UINT32(2, store.oldestUnsyncedSeq());
    StoredRecord r;
    TEST_ASSERT_TRUE(store.recordAt(3, r));
    TEST_ASSERT_EQUAL_UINT32(5, r.seq);
}

// --- Ecraser une SYNCED ancienne ne compte PAS comme drop de PENDING -------
void test_native_full_overwrites_synced_without_flag() {
    MemoryBlobStore mem(bytesFor(4));
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, 4));
    for (uint32_t s = 1; s <= 4; s++) store.append(mk(s, s * 300));
    store.markSyncedUpTo(4); // tout SYNCED
    TEST_ASSERT_TRUE(store.append(mk(5, 1500))); // ecrase seq=1 (SYNCED)
    TEST_ASSERT_EQUAL_UINT32(0, store.droppedPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.lastDroppedSeq());
}

// --- Geometrie differente au remontage -> reformat propre ------------------
void test_native_reformat_on_geometry_change() {
    std::vector<uint8_t> disk(bytesFor(8), 0xFF);
    {
        MemoryBlobStore mem(&disk);
        MeasurementStore store;
        TEST_ASSERT_TRUE(store.begin(&mem, 8));
        for (uint32_t s = 1; s <= 5; s++) store.append(mk(s, s * 300));
    }
    {
        MemoryBlobStore mem(&disk);
        MeasurementStore store;
        TEST_ASSERT_TRUE(store.begin(&mem, 4));
        TEST_ASSERT_EQUAL_UINT32(0, store.count());
    }
}

// --- Capacite « 30 jours » : geometrie coherente avec la partition ---------
void test_native_capacity_30_days_geometry() {
    // 8640 mesures * 32 o + en-tete = ~270 Ko, bien sous la partition spiffs
    // de 1 441 792 o. On verifie juste que la geometrie se monte proprement.
    const uint32_t cap = 8640;
    std::vector<uint8_t> disk(bytesFor(cap), 0xFF);
    MemoryBlobStore mem(&disk);
    MeasurementStore store;
    TEST_ASSERT_TRUE(store.begin(&mem, cap));
    TEST_ASSERT_EQUAL_UINT32(cap, store.capacity());
    TEST_ASSERT_TRUE(bytesFor(cap) < 1441792u);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_native_append_and_order);
    RUN_TEST(test_native_ring_overwrite);
    RUN_TEST(test_native_mark_synced_cumulative);
    RUN_TEST(test_native_select_retransmit_bounded);
    RUN_TEST(test_native_select_from_hint);
    RUN_TEST(test_native_no_duplicates_in_selection);
    RUN_TEST(test_native_survives_reboot);
    RUN_TEST(test_native_full_of_pending_drops_oldest_explicitly);
    RUN_TEST(test_native_full_overwrites_synced_without_flag);
    RUN_TEST(test_native_reformat_on_geometry_change);
    RUN_TEST(test_native_capacity_30_days_geometry);
    return UNITY_END();
}
