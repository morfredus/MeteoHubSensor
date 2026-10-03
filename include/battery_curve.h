#pragma once
// Courbe de charge d'un accu Li-ion 3,7 V : tension (V) -> pourcentage (0-100).
//
// Pourquoi une courbe et pas une droite : un Li-ion se decharge en "S". Entre 3,7 V et
// 3,9 V il reste l'essentiel de la capacite ; une droite 2,6-4,2 V affichait 69 % a 3,7 V
// alors qu'il reste ~15 %. Les paliers ci-dessous sont ceux d'une cellule Li-ion
// standard (tension a vide, 20 degres C), interpoles lineairement entre deux points.
//
// Limites assumees : table generique, pas mesuree sur l'accu 3000 mAh de la sonde ;
// la tension lue juste apres un reveil est proche de la tension a vide (le pont ne
// tire que 20 uA) mais fleche de quelques dizaines de mV sous charge d'emission.
// Pour l'affiner : decharge complete a faible courant en relevant (V, % consomme).
#include <cstddef>
#include <cstdint>

namespace mhbatt {

struct CurvePoint { float volts; uint8_t percent; };

// Ordre DECROISSANT de tension (la recherche en depend).
constexpr CurvePoint kLiIonCurve[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80},
    {3.98f,  75}, {3.95f, 70}, {3.91f, 65}, {3.87f, 60}, {3.85f, 55},
    {3.84f,  50}, {3.82f, 45}, {3.80f, 40}, {3.79f, 35}, {3.77f, 30},
    {3.75f,  25}, {3.73f, 20}, {3.71f, 15}, {3.69f, 10}, {3.61f,  5},
    {3.27f,   0},
};
constexpr size_t kLiIonCurveSize = sizeof(kLiIonCurve) / sizeof(kLiIonCurve[0]);

inline uint8_t liIonPercent(float volts) {
    if (volts >= kLiIonCurve[0].volts) return kLiIonCurve[0].percent;
    if (volts <= kLiIonCurve[kLiIonCurveSize - 1].volts) return 0;
    for (size_t i = 1; i < kLiIonCurveSize; ++i) {
        if (volts >= kLiIonCurve[i].volts) {
            const CurvePoint& hi = kLiIonCurve[i - 1];
            const CurvePoint& lo = kLiIonCurve[i];
            const float t = (volts - lo.volts) / (hi.volts - lo.volts);
            return static_cast<uint8_t>(lo.percent + t * (hi.percent - lo.percent) + 0.5f);
        }
    }
    return 0;
}

}  // namespace mhbatt
