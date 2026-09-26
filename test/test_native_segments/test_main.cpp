// Tests unitaires (hote) du journal segmente de la sonde (SegmentStore) : ajout
// sans recopie, accuse, purge, retention comptee, retransmission bornee et
// ordonnee, redemarrage, numerotation repartie en arriere, oldest_seq fournissable.
#include <unity.h>
#include "sync/segment_store.h"

using namespace mhs;

static StoredRecord mk(uint32_t seq) {
    StoredRecord r{};
    r.seq = seq;
    r.sensor_ts = 1790000000u + seq * 300;
    r.t = 20.0f + (float)(seq % 10);
    return r;
}

void setUp() {}
void tearDown() {}

// --- Ajout : un segment par tranche de N mesures consecutives ----------------
void test_native_append_creates_segments() {
    MemorySegmentFs fs;
    SegmentStore st;
    TEST_ASSERT_TRUE(st.begin(&fs, 4, 30, 0, 0));
    for (uint32_t s = 1; s <= 10; s++) TEST_ASSERT_TRUE(st.append(mk(s)));
    TEST_ASSERT_EQUAL_UINT32(3, st.segmentCount());       // 1-4, 5-8, 9-10
    TEST_ASSERT_EQUAL_UINT32(10, st.count());
    TEST_ASSERT_EQUAL_UINT32(10, st.unsyncedCount());
    TEST_ASSERT_EQUAL_UINT32(1, st.oldestSeq());
    TEST_ASSERT_EQUAL_UINT32(10, st.newestSeq());
    TEST_ASSERT_EQUAL_UINT32(4 * 32, fs.size(1));         // 4 mesures de 32 o
}

// --- Accuse : purge des segments pleins et accuses, jamais du dernier --------
void test_native_ack_purges_synced_segments() {
    MemorySegmentFs fs;
    SegmentStore st;
    st.begin(&fs, 4, 30, 0, 0);
    for (uint32_t s = 1; s <= 10; s++) st.append(mk(s));
    TEST_ASSERT_TRUE(st.markSyncedUpTo(6));
    TEST_ASSERT_EQUAL_UINT32(2, st.segmentCount());        // 1-4 supprime
    TEST_ASSERT_EQUAL_UINT32(4, st.unsyncedCount());       // 7..10
    TEST_ASSERT_TRUE(st.markSyncedUpTo(10));
    TEST_ASSERT_EQUAL_UINT32(1, st.segmentCount());        // le dernier reste (ajouts)
    TEST_ASSERT_EQUAL_UINT32(0, st.unsyncedCount());
    TEST_ASSERT_FALSE(st.markSyncedUpTo(10));              // pas d'avance
    TEST_ASSERT_FALSE(st.markSyncedUpTo(99));              // au-dela de nos mesures : refuse
    TEST_ASSERT_EQUAL_UINT32(10, st.ackWatermark());
}

// --- Retransmission : bornee, ordonnee, depuis le filigrane -------------------
void test_native_select_retransmit() {
    MemorySegmentFs fs;
    SegmentStore st;
    st.begin(&fs, 4, 30, 0, 0);
    for (uint32_t s = 1; s <= 10; s++) st.append(mk(s));
    st.markSyncedUpTo(2);
    StoredRecord out[16];
    uint32_t n = st.selectRetransmit(0, 5, out);
    TEST_ASSERT_EQUAL_UINT32(5, n);
    for (uint32_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_UINT32(3 + i, out[i].seq);
    n = st.selectRetransmit(9, 16, out);                   // fromSeq au-dessus du filigrane
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_UINT32(9, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(10, out[1].seq);
}

// --- Retention depassee : pertes COMPTEES, jamais silencieuses ---------------
void test_native_retention_counts_drops() {
    MemorySegmentFs fs;
    SegmentStore st;
    st.begin(&fs, 2, 3, 0, 0);                             // 3 segments de 2 = 6 mesures
    for (uint32_t s = 1; s <= 6; s++) st.append(mk(s));
    TEST_ASSERT_EQUAL_UINT32(0, st.droppedPending());
    st.append(mk(7));                                      // 4e segment -> le 1er part
    TEST_ASSERT_EQUAL_UINT32(2, st.droppedPending());      // seq 1 et 2 perdues
    TEST_ASSERT_EQUAL_UINT32(2, st.lastDroppedSeq());
    TEST_ASSERT_EQUAL_UINT32(3, st.oldestSeq());
}

// --- Redemarrage : l'etat se relit depuis les fichiers + filigrane NVS -------
void test_native_survives_reboot() {
    MemorySegmentFs fs;
    {
        SegmentStore st;
        st.begin(&fs, 4, 30, 0, 0);
        for (uint32_t s = 1; s <= 7; s++) st.append(mk(s));
        st.markSyncedUpTo(5);
    }
    SegmentStore st;
    TEST_ASSERT_TRUE(st.begin(&fs, 4, 30, /*watermark NVS*/ 5, 0));
    TEST_ASSERT_EQUAL_UINT32(7, st.newestSeq());
    TEST_ASSERT_EQUAL_UINT32(2, st.unsyncedCount());       // 6 et 7
    TEST_ASSERT_TRUE(st.append(mk(8)));                    // reprend dans le segment 5-8
    TEST_ASSERT_EQUAL_UINT32(1, st.segmentCount());
}

// --- Numerotation repartie en arriere (NVS effacee) : on repart a vide -------
void test_native_counter_restart_wipes() {
    MemorySegmentFs fs;
    SegmentStore st;
    st.begin(&fs, 4, 30, 0, 0);
    for (uint32_t s = 100; s <= 105; s++) st.append(mk(s));
    st.markSyncedUpTo(103);
    TEST_ASSERT_TRUE(st.append(mk(1)));
    TEST_ASSERT_TRUE(st.takeRestarted());
    TEST_ASSERT_FALSE(st.takeRestarted());
    TEST_ASSERT_EQUAL_UINT32(0, st.ackWatermark());
    TEST_ASSERT_EQUAL_UINT32(1, st.oldestSeq());
    TEST_ASSERT_EQUAL_UINT32(1, st.count());
}

// --- oldest_seq annonce au hub = plus ancienne mesure EN ATTENTE --------------
void test_native_oldest_providable() {
    MemorySegmentFs fs;
    SegmentStore st;
    st.begin(&fs, 4, 30, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(0, st.oldestProvidableSeq());  // vide
    for (uint32_t s = 1; s <= 6; s++) st.append(mk(s));
    TEST_ASSERT_EQUAL_UINT32(1, st.oldestProvidableSeq());
    st.markSyncedUpTo(5);                                   // 5 accuse, encore stocke
    TEST_ASSERT_EQUAL_UINT32(5, st.oldestSeq());            // segment 5-6 garde
    TEST_ASSERT_EQUAL_UINT32(6, st.oldestProvidableSeq());  // mais 6 seul est fournissable
    st.markSyncedUpTo(6);
    TEST_ASSERT_EQUAL_UINT32(0, st.oldestProvidableSeq());  // rien en attente
}

// --- Filigrane incoherent herite : repare -------------------------------------
void test_native_repair_watermark() {
    MemorySegmentFs fs;
    {
        SegmentStore st;
        st.begin(&fs, 4, 30, 0, 0);
        for (uint32_t s = 1; s <= 3; s++) st.append(mk(s));
    }
    SegmentStore st;
    st.begin(&fs, 4, 30, /*watermark NVS aberrant*/ 945, 0);
    TEST_ASSERT_TRUE(st.repairWatermark());
    TEST_ASSERT_EQUAL_UINT32(0, st.ackWatermark());
    TEST_ASSERT_EQUAL_UINT32(3, st.unsyncedCount());
}

// --- Echec d'ecriture : pas de fausse mesure, segment scelle ------------------
void test_native_append_failure() {
    MemorySegmentFs fs;
    SegmentStore st;
    st.begin(&fs, 4, 30, 0, 0);
    st.append(mk(1));
    fs.failAppend = true;
    TEST_ASSERT_FALSE(st.append(mk(2)));
    TEST_ASSERT_EQUAL_UINT32(1, st.count());
    fs.failAppend = false;
    TEST_ASSERT_TRUE(st.append(mk(3)));                    // nouveau segment (1 scelle, trou 2)
    TEST_ASSERT_EQUAL_UINT32(2, st.segmentCount());
    StoredRecord out[4];
    TEST_ASSERT_EQUAL_UINT32(2, st.selectRetransmit(0, 4, out));
    TEST_ASSERT_EQUAL_UINT32(1, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(3, out[1].seq);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_native_append_creates_segments);
    RUN_TEST(test_native_ack_purges_synced_segments);
    RUN_TEST(test_native_select_retransmit);
    RUN_TEST(test_native_retention_counts_drops);
    RUN_TEST(test_native_survives_reboot);
    RUN_TEST(test_native_counter_restart_wipes);
    RUN_TEST(test_native_oldest_providable);
    RUN_TEST(test_native_repair_watermark);
    RUN_TEST(test_native_append_failure);
    return UNITY_END();
}
