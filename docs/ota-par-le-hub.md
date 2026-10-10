# Mise a jour OTA de la sonde par le hub

Objectif : ne plus rentrer la sonde pour chaque version pendant la phase de developpement.

## Principe

ESP-NOW ne transporte que 250 octets par trame : le binaire ne passe pas par la. Le hub ne fait que
proposer, la sonde va chercher.

1. Un binaire de sonde est depose sur le hub (page Systeme, carte « Firmware de la sonde »). Le hub le garde sur sa
   carte SD et lit la version dans la balise `MHSFW=` embarquee par le firmware : rien a saisir.
2. A chaque cycle, la sonde emet sa trame et declare sa version (`fw_version`).
3. Si la version du hub diffère, la reponse `SyncControl` du hub porte le drapeau `SYNC_FLAG_OTA_OFFER`
   (bit 15 de `want_count`) et une trame `OtaOffer` suit (version, taille, MD5).
4. En fin de cycle (mesure deja stockee, envoyee, accusee), la sonde s'associe au SoftAP `MH-NOW` du hub, telecharge
   `http://<passerelle>/sensor-fw.bin`, verifie le MD5, flashe le slot OTA inactif puis redemarre.

Aucun identifiant Wi-Fi de la box n'intervient : seul le mot de passe du SoftAP du hub (constante partagee) est utilise.

## Premier flash

La premiere version capable d'OTA se flashe en USB. Le hub refuse un binaire sans balise `MHSFW=` (anterieur a la
0.33.1). Les suivantes se deposent sur le hub.

## Garde-fous

- Pas de mise a jour sous 3,5 V si la batterie est mesuree.
- 5 tentatives au maximum par version proposee (compteur en memoire RTC, remis a zero par un power-cycle).
- Taille plafonnee a la partition d'application (1,25 Mo).
- Un echec laisse le firmware courant intact ; la sonde retente au reveil suivant.

## Limites

- Pas de retour arriere automatique : un firmware qui demarre mais ne parle plus au hub exige un reflash USB.
- L'offre n'est pas authentifiee (meme niveau de confiance que la liaison ESP-NOW, non chiffree).
- Le hub a besoin de sa carte SD pour stocker le binaire (sa LittleFS est trop petite).
- Le hub efface le binaire de lui-meme des que la sonde declare la version stockee. Pour annuler une offre en
  attente, le retirer depuis la page Systeme.
- Non teste sur materiel au moment de l'ecriture : compilation et tests hote seulement.
