# Protocole ESP / Nextion (FlowIO)

## Objet

Ce document décrit le contrat actuel entre `FlowIO` et un écran Nextion piloté
par `HMIModule` / `NextionDriver`.

Le protocole est volontairement mixte :
- `ESP -> Nextion` : commandes Nextion natives (`.txt`, `.val`, `page ...`)
- `Nextion -> ESP` : protocole binaire compact `# <len> <opcode> <payload...>`
- compatibilité conservée : événements touch Nextion (`0x65`)

Le port série utilisé côté FlowIO est `Serial2` sur `RX16 / TX17` à `115200`.

## Principes

- Les valeurs métier affichées (`pH`, `ORP`, températures, heure, date) sont
  formatées côté ESP puis envoyées en `.txt`.
- Les états discrets et indicateurs compacts sont envoyés en `.val`.
- L'écran Nextion n'est pas la source de vérité : il émet des intentions ou des
  commandes UI, puis `FlowIO` rafraîchit l'affichage à partir du `DataStore`,
  du `ConfigStore` et des états runtime.
- Les objets Nextion utilisés par l'ESP doivent être considérés comme faisant
  partie du contrat de firmware HMI.

## Objets Nextion pilotés par l'ESP

### Page Home

Objets texte :
- `tWaterTemp.txt`
- `tAirTemp.txt`
- `tpH.txt`
- `tORP.txt`
- `tTime.txt`
- `tDate.txt`

Objets numériques :
- `vapHPercent.val`
- `vaOrpPercent.val`
- `globals.vaStates.val`
- `globals.vaAlarms.val`

Sémantique :
- `tWaterTemp.txt` : exemple `27.4°C`
- `tAirTemp.txt` : exemple `21.8°C`
- `tpH.txt` : exemple `7.18`
- `tORP.txt` : exemple `650`
- `tTime.txt` : heure locale au format `HH:MM`, exemple `14:37`
- `tDate.txt` : date locale au format `jour Mois`, exemple `15 Avril`
- `vapHPercent.val` : jauge `0..280`, `140` à la consigne pH
- `vaOrpPercent.val` : jauge `0..280`, `140` à la consigne ORP
- `globals.vaStates.val` : bitmap d'état compact
- `globals.vaAlarms.val` : bitmap d'alarmes compact

### Bitmap `globals.vaStates`

Bits actuellement utilisés :
- bit `0` : filtration ON
- bit `1` : pompe pH ON
- bit `2` : pompe ORP ON
- bit `3` : mode auto global
- bit `4` : mode auto pH
- bit `5` : mode auto ORP
- bit `6` : mode hiver
- bit `7` : WiFi connecté
- bit `8` : MQTT connecté
- bit `9` : robot ON
- bit `10` : éclairage ON
- bit `11` : chauffage ON
- bit `12` : remplissage ON

### Bitmap `globals.vaAlarms`

Bits actuellement utilisés :
- bit `0` : niveau eau bas
- bit `1` : bidon pH bas
- bit `2` : bidon chlore bas
- bit `3` : alarme temps max pompe pH
- bit `4` : alarme temps max pompe ORP
- bit `5` : alarme PSI

### Page de configuration

Quand le menu config est compilé, le driver rend sur la page `pageCfgMenu`
avec les objets suivants :

- `tPath`
- `bHome`
- `bBack`
- `bValid`
- `bPrev`
- `bNext`
- `nPage`
- `nPages`
- `tL0` .. `tL5`
- `tV0` .. `tV5`
- `bR0` .. `bR5`

Sémantique :
- `tPath` : breadcrumb courant
- `tLx` : libellé de ligne
- `tVx` : valeur de ligne
- `bRx` : zone tactile de ligne

## Protocole Nextion -> ESP

## RTC Nextion

L'écran Nextion expose une RTC locale utilisée comme source de secours pour
FlowIO lorsque l'heure NTP n'est pas disponible au boot.

Registres RTC Nextion utilisés :
- `rtc0` : année, par exemple `2026`
- `rtc1` : mois `1..12`
- `rtc2` : jour `1..31`
- `rtc3` : heure `0..23`
- `rtc4` : minute `0..59`
- `rtc5` : seconde `0..59`

Séquence au boot côté ESP :
- si l'heure ESP est déjà synchronisée NTP, l'ESP ne lit pas la RTC Nextion ;
- si WiFi ou NTP est désactivé, l'ESP lit immédiatement `rtc0..rtc5` ;
- si WiFi et NTP sont actifs mais que l'heure n'est toujours pas synchronisée
  après `30s`, l'ESP lit `rtc0..rtc5` ;
- si la lecture RTC est valide, `TimeService` est réglé depuis cette heure et
  les fonctions métier peuvent utiliser le scheduler normalement ;
- tant que l'heure provient de la RTC Nextion, elle est marquée comme source
  externe et n'est pas utilisée pour réécrire immédiatement la RTC Nextion.

Synchronisation ESP -> Nextion :
- dès que l'ESP obtient une vraie synchronisation NTP, il écrit `rtc0..rtc5`
  pour remettre la RTC Nextion à l'heure ;
- ensuite, tant que l'heure ESP reste synchronisée NTP, l'ESP réécrit la RTC
  Nextion une fois par jour local ;
- les valeurs écrites sont en heure locale, pas en UTC.

Commandes Nextion utilisées par l'ESP :

```text
get rtc0
get rtc1
get rtc2
get rtc3
get rtc4
get rtc5
rtc0=<year>
rtc1=<month>
rtc2=<day>
rtc3=<hour>
rtc4=<minute>
rtc5=<second>
```

## Format binaire

Format :

```text
0x23 <len> <opcode> <payload...>
```

Où :
- `0x23` est le caractère `#`
- `len` est le nombre d'octets qui suivent
- `opcode` est le type de message
- `payload` dépend de l'opcode

Exemple :

```text
23 03 60 01 01
```

Décodage :
- `23` : début de trame
- `03` : trois octets suivent
- `60` : opcode `HOME_ACTION`
- `01` : action `FILTRATION_SET`
- `01` : valeur `true`

## Opcodes supportés

### `0x50` : `PAGE`

Payload :
- `page_id` (`uint8`)

Usage :
- utilisé par Nextion pour informer l'ESP de la page actuellement affichée
- permet au driver de savoir si la page config est déjà visible

Exemple Nextion :

```text
printh 23 02 50 0A
```

Exemple :
- `0A` = page config

### `0x51` : `NAV`

Payload :
- `nav_id` (`uint8`)

Valeurs supportées :
- `0x01` : `HOME`
- `0x02` : `BACK`
- `0x03` : `VALIDATE`
- `0x04` : `NEXT_PAGE`
- `0x05` : `PREV_PAGE`

Exemple Nextion :

```text
printh 23 02 51 04
```

### `0x52` : `ROW_ACTIVATE`

Payload :
- `row` (`uint8`, `0..5`)

Exemple :

```text
printh 23 02 52 03
```

Active la ligne `3`.

### `0x53` : `ROW_TOGGLE`

Payload :
- `row` (`uint8`, `0..5`)

Exemple :

```text
printh 23 02 53 01
```

Toggle la ligne `1`.

### `0x54` : `ROW_CYCLE`

Payload :
- `row` (`uint8`, `0..5`)
- `dir` (`uint8`)

Valeurs `dir` :
- `0x00` ou `0xFF` : précédent
- toute autre valeur : suivant

Exemple :

```text
printh 23 03 54 02 01
```

Fait avancer la ligne `2`.

### `0x60` : `HOME_ACTION`

Payload :
- `action_id` (`uint8`)
- `value` (`uint8`)

Actions supportées :
- `0x01` : `FILTRATION_SET`
- `0x02` : `AUTO_MODE_SET`
- `0x03` : `SYNC_REQUEST`

Valeur :
- `0x00` : faux / OFF
- `0x01` : vrai / ON

Exemples :

Filtration ON :

```text
printh 23 03 60 01 01
```

Filtration OFF :

```text
printh 23 03 60 01 00
```

Mode auto ON :

```text
printh 23 03 60 02 01
```

Demande de resynchronisation Home :

```text
printh 23 03 60 03 01
```

## Mapping vers les commandes ESP

Les actions Home sont routées via `CommandService` :

- `FILTRATION_SET`
  - commande : `poollogic.filtration.write`
  - payload envoyé par l'ESP :

```json
{"value":true}
```

- `AUTO_MODE_SET`
  - commande : `poollogic.auto_mode.set`
  - payload envoyé par l'ESP :

```json
{"value":false}
```

- `SYNC_REQUEST`
  - ne passe pas par `CommandService`
  - demande simplement à `HMIModule` de republier toutes les données Home

Il n'y a pas d'ACK synchrone formel vers Nextion dans cette V1.
Le retour attendu est le rafraîchissement de l'état réel après exécution.

## Compatibilité conservée

En plus du protocole binaire ci-dessus, `NextionDriver` accepte encore les
événements touch Nextion standard `0x65`.

Format Nextion `Send Component ID` :

```text
65 <page_id> <component_id> <event>
```

Le driver ne traite que l'événement `press` (`event=01`) pour éviter un double
toggle si le composant émet aussi le release.

Sur la page config (`page_id=0A`), les component IDs restent réservés à la
navigation menu existante. Sur les autres pages, les IDs suivants sont traités
comme des toggles Home :

- component ID `16` : toggle mode auto global
- component ID `18` : toggle ORP auto
- component ID `19` : toggle pH auto
- component ID `20` : toggle robot
- component ID `21` : toggle hivernage
- component ID `22` : toggle éclairage
- component ID `39` : toggle pompe filtration

## Pages et identifiants recommandés

Identifiants recommandés côté Nextion :
- `0x00` : page Home
- `0x0A` : page `pageCfgMenu`

L'opcode `PAGE` peut être émis dans le `Preinitialize Event` de chaque page.

Exemples :

Home :

```text
printh 23 02 50 00
```

Config :

```text
printh 23 02 50 0A
```

## Intégration Nextion recommandée

### Home

Les widgets Home peuvent rester entièrement locaux pour le rendu, mais les
boutons d'action doivent envoyer des trames binaires.

Exemples :
- bouton filtration ON : `printh 23 03 60 01 01`
- bouton filtration OFF : `printh 23 03 60 01 00`
- bouton auto ON : `printh 23 03 60 02 01`
- bouton sync : `printh 23 03 60 03 01`

### Config

La page config peut s'appuyer sur :
- `NAV`
- `ROW_ACTIVATE`
- `ROW_TOGGLE`
- `ROW_CYCLE`

Exemples :
- `bHome` : `printh 23 02 51 01`
- `bBack` : `printh 23 02 51 02`
- `bValid` : `printh 23 02 51 03`
- `bNext` : `printh 23 02 51 04`
- `bPrev` : `printh 23 02 51 05`
- `bR0` : `printh 23 02 52 00`
- `bR1` : `printh 23 02 52 01`

## Limitations actuelles

- Pas de handshake de version d'affichage encore implémenté.
- Pas d'ACK binaire structuré renvoyé vers Nextion.
- `globals.vaStates` et `globals.vaAlarms` sont des contrats de firmware : toute
  modification d'ordre de bits doit être synchronisée entre ESP et Nextion.
