# HMIModule (`moduleId: hmi`)

## Rôle

Couche d'orchestration HMI locale:
- lit/édite la configuration via `ConfigStoreService`
- maintient un menu de configuration paginé (6 lignes/page)
- délègue le rendu et les entrées au driver matériel interactif (`IHmiDriver`)
- pilote en option des sorties d'affichage annexes (LEDs façade, émetteur RF433 Venice)

En V1, le driver interactif embarqué est `NextionDriver`.
Une sortie déportée `TfaVeniceRf433Sink` peut aussi émettre la température d'eau
vers un récepteur TFA Venice compatible.
Le menu Nextion de configuration est activé dans le build FlowIO, mais il ne
rend `pageCfgMenu` qu'après une commande d'ouverture explicite depuis l'écran.
Le modèle menu est stateless côté RAM longue durée : il relit la page courante
depuis le `ConfigStore` et applique les changements simples immédiatement.

## Dépendances

- `loghub`
- `config`
- `eventbus`
- `datastore`
- `io`
- `alarms`
- `command`
- `time`
- `wifi`

## Service exposé

- `hmi` -> `HmiService`
  - `requestRefresh()`
  - `openConfigHome()`
  - `openConfigModule(module)`
  - `buildConfigMenuJson(out)`

## Fonctionnement menu config

- Mode browse: liste hiérarchique des topics immédiats via `listModules()`
- Mode édition: clés/valeurs de la branche sélectionnée via `toJsonModule()`
- Pagination: 6 lignes
- Actions UI: `Home`, `Back`, `Valider`, `Prev`, `Next`
- En mode browse, `Prev/Next` paginent les `tL0..tL5` et `tV0..tV5` sont masqués.
- En mode édition, `Prev/Next` paginent les attributs `tL0..tL5` / `tV0..tV5`.
- Les changements simples (`Switch`, `Select`, `Slider`, `Text`) sont appliqués immédiatement via un patch JSON ciblé `applyJson()`.
- Les valeurs de la page d'édition courante sont rafraîchies toutes les `5s`; la page complète n'est pas rerendue en continu.
- `Valider` est réservé pour une évolution ultérieure; le modèle léger actuel ne conserve pas de cache `dirty`.
- FlowIO ne change pas la page Nextion; le `Preinitialize Event` de `pageCfgMenu` doit envoyer `printh 23 02 50 0A` pour activer le rendu du menu côté FlowIO.
- Le `Page Exit Event` Nextion de `pageCfgMenu` doit envoyer `printh 23 02 51 06` pour désactiver le rendu actif du menu côté FlowIO.

Le modèle de menu est volontairement stateless côté RAM longue durée:
- pas de cache persistant des modules;
- pas de cache persistant des lignes de configuration;
- une page est reconstruite à la demande depuis `ConfigStore`.

Le chemin courant est exposé sous forme breadcrumb:
`flow > cfg > <module>`

## Typage des champs

Types de widget supportés:
- `Text`
- `Switch`
- `Select`
- `Slider`

Par défaut, le type est déduit depuis JSON (bool/int/float/texte).
Des hints peuvent forcer le widget et les contraintes (bornes/options).

## Driver Nextion (V1)

- Transport: mappé via `src/Board/BoardSerialMap.h`
  - mode normal: logs -> `Serial`, Nextion -> `Serial2` (RX16/TX17)
  - inversion: définir `FLOW_SWAP_LOG_HMI_SERIAL=1` au build pour
    logs -> `Serial2` (RX16/TX17), Nextion -> `Serial`
  - note: en mode inversé, UART0 reste utilisé par les messages ROM boot ESP32
- Rendu page config sur objets Nextion conventionnels:
  - `tPath`, `tL0..tL5`, `tV0..tV5`
  - `bHome`, `bBack`, `bValid`, `bPrev`, `bNext`, `bR0..bR5`
  - `nPage`, `nPages`
- Entrées:
  - protocole binaire custom `# <len> <opcode> <payload...>`
- Ouverture menu:
  - bouton paramètres côté Home: `page pageCfgMenu`
  - `Preinitialize Event` de `pageCfgMenu`: `printh 23 02 50 0A`
  - `Page Exit Event` de `pageCfgMenu`: `printh 23 02 51 06`
- Navigation:
  - mode browse: seuls les topics immédiats sont visibles dans `tL0..tL5`; `tV0..tV5` sont masqués
  - clic `tLN`: `printh 23 02 52 0N` pour entrer dans la branche
  - clic `bRN`: `printh 23 02 55 0N` pour éditer les attributs de la branche
  - mode édition: `tL0..tL5` affichent les clés, `tV0..tV5` affichent les valeurs et servent de dual-state button pour les booléens; refresh valeurs toutes les `5s`
  - valeur non-switch: FlowIO désactive `tVN` avec `tsw tVN,0`, force `tVN.val=0`, puis met `tVN.txt`
  - valeur switch: FlowIO active `tVN` avec `tsw tVN,1`, met `tVN.val=0/1`, puis `tVN.txt` à `OFF/ON`
  - changement switch `tVN`: `printh 23 02 53 0N`
  - fermeture recommandée: `bBack` à la racine + changement de page local Nextion
- RTC:
  - lit `rtc0..rtc5` au boot si l'heure NTP n'est pas disponible
  - écrit `rtc0..rtc5` quand l'ESP dispose d'une heure NTP fiable
- Référence de protocole et objets:
  - voir `docs/integration/nextion-esp-protocol.md`

## Événements internes

Sur `EventId::ConfigChanged`, si le module affiché est impacté, l'écran est resynchronisé automatiquement.

## Config HMI

Trois branches de config dédiées sont exposées:

- `hmi/nextion`
  - `enabled`: active/désactive l'écriture vers l'écran Nextion
- `hmi/leds`
  - `enabled`: active/désactive les écritures de masque logique vers `StatusLedsService`
- `hmi/venice`
  - `enabled`: active/désactive l'émetteur RF433 Venice
  - `tx_gpio`: GPIO TX du module 433 MHz
  - la période, le canal et l'identifiant capteur restent fixés aux valeurs par défaut du driver

Quand `hmi/leds/enabled=false`, le `HMIModule` cesse simplement d'écrire le
masque LEDs. Cela laisse le PCF8574 disponible pour d'autres usages côté
`IOModule`.
