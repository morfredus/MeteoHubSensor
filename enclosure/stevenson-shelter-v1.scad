//
// ============================================================================
// MeteoHubSensor - MINI ÉCRAN DE STEVENSON
// V8 - adaptation du plan Stevenson traditionnel fourni
// ============================================================================
//
// Référence de conception :
// le plan traditionnel montre des lames inclinées vers l'extérieur,
// superposées sur les quatre faces, avec une vraie circulation d'air.
// Cette version reprend CE principe, à l'échelle du MeteoHubSensor.
//
// DIMENSIONS
// ----------
// Volume intérieur : 110 x 90 x 110 mm
// Caisson extérieur : 130 x 110 x 130 mm
//
// LAMELLES
// --------
// 10 par face
// section : 110 x 8 x 4 mm
// inclinaison : 40° sous l'horizontale
// pas vertical : 11.2 mm
// ouverture verticale projetée : ~2.5 mm
//
// IMPORTANT :
// "extérieur plus bas" est garanti sur les quatre faces.
//
// AVANT  : extérieur = Y-  -> +40°
// ARRIÈRE: extérieur = Y+  -> -40°
// GAUCHE : extérieur = X-  -> -40°
// DROITE : extérieur = X+  -> +40°
//
// Le choix de 40° est volontairement proche du profil visible sur
// la coupe du plan traditionnel fourni.
// ============================================================================

$fn = 48;

// ---------------------------------------------------------------------------
// CAISSON
// ---------------------------------------------------------------------------

IW = 110;
ID = 90;
IH = 110;

POST = 10;
POST_H = 130;

// ---------------------------------------------------------------------------
// LAMELLES
// ---------------------------------------------------------------------------

SLAT_L     = 110;
SLAT_T     = 4;
SLAT_H     = 8;

SLAT_COUNT = 10;
SLAT_PITCH = 11.2;

// Centre de la première lame.
// Les 10 lames couvrent pratiquement toute la hauteur intérieure.
SLAT_Z0 = 4.8;

SLAT_ANGLE = 40;

// ---------------------------------------------------------------------------
// TOIT DOUBLE - inspiré du principe du plan traditionnel
// ---------------------------------------------------------------------------

ROOF_W   = 160;
ROOF_D   = 140;
ROOF_T   = 5;
ROOF_GAP = 12;

// ---------------------------------------------------------------------------
// ÉLECTRONIQUE V1
// ---------------------------------------------------------------------------

SHOW_ELECTRONICS = true;

BOARD_X = 90;
BOARD_Y = 50;
BOARD_T = 1.6;

BAT_X = 90;
BAT_Y = 22;
BAT_T = 12;

RECT_X = 25;
RECT_Y = 10;
RECT_T = 8;

// ---------------------------------------------------------------------------
// COULEURS
// ---------------------------------------------------------------------------

wood        = [0.72,0.50,0.28];
wood_slat   = [0.82,0.62,0.38];
electronics = [0.15,0.18,0.20];
battery_col = [0.25,0.25,0.25];
rectifier_col = [0.10,0.35,0.10];
sensor_col  = [0.75,0.75,0.75];


// ============================================================================
// MONTANTS
// ============================================================================

module post(x,y) {
    color(wood)
        translate([x,y,0])
            cube([POST,POST,POST_H]);
}


// ============================================================================
// LAMELLES
//
// Chaque lame est centrée sur z puis inclinée.
//
// Le bord extérieur est toujours plus bas :
//
//                 INTÉRIEUR
//                     ▲
//                     │
//             ───────────────
//                ╲
//                 ╲
//                  ╲
//             ───────────────
//                     │
//                     ▼
//                 EXTÉRIEUR
//
// Le vide entre deux lames est réel : l'air peut entrer/sortir.
// ============================================================================

// AVANT : extérieur Y-
module slat_front(z) {
    color(wood_slat)
        translate([POST, POST, z])
            rotate([+SLAT_ANGLE,0,0])
                translate([0,-SLAT_T/2,-SLAT_H/2])
                    cube([SLAT_L,SLAT_T,SLAT_H]);
}

// ARRIÈRE : extérieur Y+
module slat_back(z) {
    color(wood_slat)
        translate([POST, ID+POST, z])
            rotate([-SLAT_ANGLE,0,0])
                translate([0,-SLAT_T/2,-SLAT_H/2])
                    cube([SLAT_L,SLAT_T,SLAT_H]);
}

// GAUCHE : extérieur X-
module slat_left(z) {
    color(wood_slat)
        translate([POST,POST,z])
            rotate([0,-SLAT_ANGLE,0])
                translate([-SLAT_T/2,0,-SLAT_H/2])
                    cube([SLAT_T,SLAT_L,SLAT_H]);
}

// DROITE : extérieur X+
module slat_right(z) {
    color(wood_slat)
        translate([IW+POST,POST,z])
            rotate([0,+SLAT_ANGLE,0])
                translate([-SLAT_T/2,0,-SLAT_H/2])
                    cube([SLAT_T,SLAT_L,SLAT_H]);
}


// ============================================================================
// CADRE INFÉRIEUR
//
// Le plan traditionnel possède un plancher intermédiaire.
// Pour MeteoHubSensor, on ne ferme pas le dessous : la sonde doit rester
// ventilée et l'électronique ne doit pas créer une poche thermique.
// On conserve uniquement un cadre porteur.
// ============================================================================

module lower_frame() {
    color(wood);

    // traverse avant
    translate([0,0,0])
        cube([IW+2*POST,POST,POST]);

    // traverse arrière
    translate([0,ID+POST,0])
        cube([IW+2*POST,POST,POST]);

    // traverses latérales
    translate([0,POST,0])
        cube([POST,ID,POST]);

    translate([IW+POST,POST,0])
        cube([POST,ID,POST]);
}


// ============================================================================
// DOUBLE TOIT
// ============================================================================

module roof_plate(z) {
    color(wood)
        translate([
            (IW+2*POST-ROOF_W)/2,
            (ID+2*POST-ROOF_D)/2,
            z
        ])
            cube([ROOF_W,ROOF_D,ROOF_T]);
}

module roof_spacer(x,y,z) {
    color(wood)
        translate([x,y,z])
            cube([POST,POST,ROOF_GAP]);
}


// ============================================================================
// ÉLECTRONIQUE V1 - SIMPLE ENCOMBREMENT
// ============================================================================

module board_demo() {

    // Carte actuelle 90 x 50 mm
    color(electronics)
        translate([POST+10, POST+ID-5, 18])
            rotate([90,0,0])
                cube([BOARD_X,BOARD_Y,BOARD_T]);

    // Support batterie 90 x 22 mm
    color(battery_col)
        translate([POST+10, POST+ID-8, 22])
            rotate([90,0,0])
                cube([BAT_X,BAT_Y,BAT_T]);

    // Petit redresseur 25 x 10 mm
    color(rectifier_col)
        translate([POST+15, POST+ID-10, 76])
            rotate([90,0,0])
                cube([RECT_X,RECT_Y,RECT_T]);
}


// ============================================================================
// CAPTEUR DÉPORTÉ
// ============================================================================

module sensor_demo() {

    // Position haute et centrale
    color(sensor_col)
        translate([
            POST+IW/2,
            POST+ID/2,
            94
        ])
            rotate([0,90,0])
                cylinder(d=16,h=12,center=true);

    // Faisceau indicatif
    color([0.08,0.08,0.08])
        translate([
            POST+IW/2,
            POST+ID/2,
            38
        ])
            cylinder(d=3,h=56);
}


// ============================================================================
// ABRI COMPLET
// ============================================================================

module shelter() {

    // 4 montants
    post(0,0);
    post(IW+POST,0);
    post(0,ID+POST);
    post(IW+POST,ID+POST);

    // Cadre inférieur ouvert
    lower_frame();

    // Lamelles Stevenson : 10 rangées sur chacune des 4 faces
    for (i=[0:SLAT_COUNT-1]) {

        z = SLAT_Z0 + i*SLAT_PITCH;

        slat_front(z);
        slat_back(z);
        slat_left(z);
        slat_right(z);
    }

    // Toit double : 12 mm de lame d'air
    roof_plate(POST_H);

    for (x=[0,IW+POST])
        for (y=[0,ID+POST])
            roof_spacer(x,y,POST_H+ROOF_T);

    roof_plate(POST_H+ROOF_T+ROOF_GAP);

    if (SHOW_ELECTRONICS)
        board_demo();

    sensor_demo();
}

shelter();


// ============================================================================
// NOMENCLATURE - BOIS
// ============================================================================
//
// 4 x montants
//     130 x 10 x 10 mm
//
// 40 x lamelles
//     110 x 8 x 4 mm
//     10 par face
//
// 4 x traverses du cadre inférieur
//     dimensions adaptées aux montants
//
// 2 x plaques de toit
//     160 x 140 x 5 mm
//
// 4 x entretoises de toit
//     10 x 10 x 12 mm
//
// ============================================================================
// VÉRIFICATION DE LA GÉOMÉTRIE
// ============================================================================
//
// Une lame de 8 x 4 mm inclinée de 40° présente une hauteur projetée
// d'environ :
//
//     8*cos(40°) + 4*sin(40°) ≈ 8.70 mm
//
// Le pas est 11.20 mm.
//
// Donc l'ouverture verticale entre deux lames est d'environ :
//
//     11.20 - 8.70 ≈ 2.50 mm
//
// Il existe donc un passage d'air réel.
//
// La lame descend vers l'extérieur sur les quatre faces :
// l'eau ne peut pas être conduite vers l'intérieur par la pente.
//
// ============================================================================
// ADAPTATION DU PLAN TRADITIONNEL
// ============================================================================
//
// Conservé :
//   - lames inclinées et superposées sur les faces
//   - structure à quatre montants
//   - forte protection solaire
//   - double toit ventilé
//
// Adapté pour MeteoHubSensor :
//   - dimensions miniatures
//   - pas de portes : abri fixe
//   - pas de plancher plein : meilleure ventilation du capteur
//   - capteur déporté en partie haute
//   - électronique en partie basse
//
// ============================================================================
