# Architecture Profils, App, Board et Domain

Cette note décrit l'architecture cible introduite pour Flow.IO et Supervisor, la manière de gérer les profils produits, et les règles à suivre quand on adapte un module existant ou quand on ajoute un nouveau produit.

L'objectif de cette architecture est de conserver une seule source de vérité par responsabilité, sans gonfler la RAM ni retomber dans un `main.cpp` monolithique.

## Vue d'ensemble

Les responsabilités sont maintenant séparées en 5 couches:

1. `src/App/`
Contient le socle de boot commun, la sélection du profil compilé, et le contexte applicatif partagé.

2. `src/Board/`
Contient la vérité hardware: UART, I2C, 1-Wire, GPIO, capacités d'IO et canaux logiques de carte.

3. `src/Domain/`
Contient la vérité métier: rôles métier, presets d'équipements, capteurs, defaults, hooks de configuration et règles associées.

4. `src/Profiles/`
Contient la composition produit: choix de la carte, choix du domaine, identité produit et instanciation des modules propres au firmware.

5. `platformio.ini`
Contient la sélection de compilation par environnement: macros de build, modules compilés, profils inclus, scripts de pré-build et version produit stable.

## Principe fondamental

Une information ne doit vivre que dans une seule couche:

- une pin, un UART ou un bus I2C appartiennent à `Board`
- un rôle comme `FiltrationPump` ou `WaterTemp` appartient à `Domain`
- le choix de la carte et du domaine pour un firmware appartient à `Profiles`
- le choix de compiler ou non un module appartient à `platformio.ini`
- le bootstrap commun et le contexte d'exécution appartiennent à `App`

Le design évite volontairement:

- les catalogues globaux qui instancient tous les modules du monde
- les enums hardware pollués par du métier piscine
- les `if/else` métier dispersés dans les `main_*`
- l'allocation heap structurelle introduite uniquement pour le refactor

## Gestion des profils

### Profil compilé

La sélection du profil se fait par macro de build:

- `FLOW_PROFILE_FLOWIO`
- `FLOW_PROFILE_SUPERVISOR`

Ces macros sont exposées dans [platformio.ini](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/platformio.ini) et traduites en helpers dans [BuildFlags.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/BuildFlags.h).

Le bootstrap commun s'appuie ensuite sur ces helpers dans [Bootstrap.cpp](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/Bootstrap.cpp):

- résolution du profil actif compilé
- installation de `profile`, `board`, `domain` et `identity` dans `AppContext`
- appel du `setup()` spécifique au profil
- appel du `loop()` spécifique au profil

### Structure d'un profil

Un profil produit vit dans `src/Profiles/<NomProfil>/` et contient typiquement:

- `<NomProfil>Profile.h`
- `<NomProfil>Profile.cpp`
- `<NomProfil>ModuleInstances.cpp`
- `<NomProfil>Bootstrap.cpp`

Le rôle de chaque fichier:

- `Profile.cpp`: décrit le `FirmwareProfile`
- `ModuleInstances.cpp`: contient uniquement les instances statiques de modules de ce profil
- `Bootstrap.cpp`: contient l'ordre d'enregistrement des modules et le câblage local

Le contrat `FirmwareProfile` est défini dans [FirmwareProfile.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/FirmwareProfile.h):

- `name`
- `board`
- `domain`
- `identity`
- `supervisorRuntime` pour les options runtime produit spécifiques au Supervisor
- `setup`
- `loop`

### Ajouter un nouveau profil produit

Pour un troisième produit, la marche à suivre recommandée est:

1. ajouter un environnement dans [platformio.ini](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/platformio.ini)
2. définir une nouvelle macro de profil
3. créer `src/Profiles/NouveauProduit/`
4. choisir un `BoardSpec`
5. choisir un `DomainSpec`
6. n'instancier que les modules réellement utilisés
7. exclure les autres familles de modules via `build_src_filter`

Le point important est qu'un nouveau profil ne doit pas faire grossir les autres firmwares.

## Fonctionnement de `src/App/`

### Rôle

`src/App/` contient ce qui est commun à tous les firmwares compilés.

Fichiers clés:

- [BuildFlags.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/BuildFlags.h)
- [FeatureFlags.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/FeatureFlags.h)
- [AppContext.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/AppContext.h)
- [FirmwareProfile.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/FirmwareProfile.h)
- [Bootstrap.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/Bootstrap.h)
- [Bootstrap.cpp](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/Bootstrap.cpp)

### `AppContext`

[AppContext.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/App/AppContext.h) contient le contexte partagé de boot:

- `Preferences`
- `ConfigStore`
- `ModuleManager`
- `ServiceRegistry`
- pointeurs vers `FirmwareProfile`, `BoardSpec`, `DomainSpec`, `ProductIdentity`

Ce contexte est volontairement compact et sans allocation dynamique structurelle supplémentaire.

### Quand modifier `App`

On touche `src/App/` seulement si la modification concerne tous les firmwares, par exemple:

- ajout d'une nouvelle macro de profil ou de feature
- ajout d'un nouveau service commun à tous les boots
- évolution du contrat `FirmwareProfile`
- extension du contexte partagé `AppContext`

On ne doit pas y mettre:

- du wiring de carte
- des defaults métier piscine
- des instances de modules spécifiques à un produit

## Fonctionnement de `src/Board/`

### Rôle

`src/Board/` est la vérité hardware.

Fichiers clés:

- [BoardTypes.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/BoardTypes.h)
- [BoardSpec.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/BoardSpec.h)
- [BoardCatalog.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/BoardCatalog.h)
- [BoardCatalog.cpp](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/BoardCatalog.cpp)
- [FlowIOBoardRev1.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/FlowIOBoardRev1.h)
- [SupervisorBoardRev1.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/SupervisorBoardRev1.h)
- [BoardSerialMap.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/BoardSerialMap.h)

### Contrats principaux

[BoardTypes.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/BoardTypes.h) définit:

- `IoCapability`
- `BoardSignal`
- `UartSpec`
- `I2cBusSpec`
- `OneWireBusSpec`
- `IoPointSpec`
- `BoardSpec`
- `SupervisorBoardSpec` pour les périphériques locaux Supervisor qui ne relèvent pas du domaine métier

Le sens attendu:

- `IoCapability` décrit ce qu'un point physique sait faire
- `BoardSignal` décrit un canal logique de la carte
- `IoPointSpec` relie un pin réel à un signal logique et à une capacité physique

Exemple correct:

- `BoardSignal::Relay1`
- `IoCapability::DigitalOut`

Exemple interdit:

- `BoardSignal::PhPump`
- `BoardSignal::Filtration`

Ces rôles métier doivent rester dans `Domain`.

### `BoardSerialMap`

[BoardSerialMap.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/BoardSerialMap.h) est encore un utilitaire de transition pour les ports série log/hmi. Il vit maintenant dans `src/Board/` parce que c'est bien une contrainte hardware, mais à terme il peut être absorbé dans `BoardSpec` si on veut décrire complètement l'affectation des UART.

### Cas Supervisor

Pour Supervisor, la vérité hardware locale vit maintenant dans [SupervisorBoardRev1.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Board/SupervisorBoardRev1.h):

- UART panel Nextion
- pins de boot/update du Flow.IO cible
- pin reboot Nextion
- géométrie et brochage TFT ST7789
- PIR et bouton reset WiFi

Les timings produit qui ne sont pas strictement du hardware, comme `pirTimeoutMs` ou `wifiResetHoldMs`, vivent dans le profil Supervisor via [SupervisorProfile.cpp](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Profiles/Supervisor/SupervisorProfile.cpp).

### Quand modifier `Board`

On modifie `src/Board/` si la question posée est:

- quelle pin est utilisée
- quel bus I2C ou 1-Wire existe
- quel UART est disponible
- quel signal logique de carte est exposé
- quel type physique d'IO est supporté

On ne doit pas modifier `Board` si la question est:

- quel relais commande la filtration
- quelle sonde correspond à la température d'eau
- quel device a un tank de 20 litres

Ces informations appartiennent à `Domain` ou `Profiles`.

## Fonctionnement de `src/Domain/`

### Rôle

`src/Domain/` est la vérité métier.

Fichiers clés:

- [DomainTypes.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/DomainTypes.h)
- [DomainSpec.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/DomainSpec.h)
- [DomainCatalog.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/DomainCatalog.h)
- [DomainCatalog.cpp](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/DomainCatalog.cpp)
- [Calibration.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/Calibration.h)
- `Pool/*`
- `Supervisor/*`

### Contrats principaux

[DomainTypes.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/DomainTypes.h) définit:

- `DomainRole`
- `DomainIoBinding`
- `PoolDevicePreset`
- `DomainSensorPreset`
- `PoolLogicDefaultsSpec`

[DomainSpec.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/DomainSpec.h) définit un domaine complet:

- nom du domaine
- bindings `BoardSignal -> DomainRole`
- presets d'équipements
- presets de capteurs
- defaults métier
- hook de configuration

### Domaines existants

Le domaine `Pool` contient aujourd'hui:

- les rôles métier piscine
- les defaults de logique piscine
- les presets d'équipements et capteurs
- les bindings nécessaires aux modules `IOModule`, `PoolDeviceModule` et `PoolLogicModule`

Le domaine `Supervisor` contient:

- les rôles métier propres au superviseur
- ses defaults et presets spécifiques

### `Calibration`

[Calibration.h](/Users/christophebelmont/Documents/GitHub.nosync/Flow.io/src/Domain/Calibration.h) reste dans `Domain` car ces constantes sont des règles métier de conversion capteur, pas de la topologie carte.

### Quand modifier `Domain`

On modifie `src/Domain/` si la question posée est:

- quel signal de carte correspond à quel rôle métier
- quels devices métier existent
- quelles valeurs par défaut doivent être appliquées
- quels seuils, gains, timers ou capacités sont utilisés par la logique métier
- quels capteurs doivent apparaître côté runtime ou Home Assistant

Tout nombre métier récurrent doit finir dans `Domain`, pas dans un bootstrap ou un module générique.

## Mapping `BoardSignal -> DomainRole`

Le mapping est volontairement au centre de la séparation des responsabilités.

Le flux correct est:

1. la carte expose `BoardSignal::Relay1`
2. le domaine dit que `Relay1 -> FiltrationPump`
3. le profil choisit carte + domaine
4. le bootstrap configure les modules à partir de ce couple

Ce flux évite:

- les dépendances directes à des slots hardcodés dans `main`
- les `if/else` basés sur `POOL_IO_SLOT_*`
- les modules qui devinent le métier à partir d'un GPIO

## Fonctionnement de `platformio.ini`

`platformio.ini` est la source de vérité de compilation, pas de métier.

Il doit contenir:

- les macros de profil compilé
- les versions stables des firmwares
- les `build_src_filter`
- les dépendances de librairies par environnement
- les scripts de pré-build

Règles à respecter:

- un module inutilisé par un profil doit idéalement être exclu du build
- un profil Wokwi doit aussi sélectionner ses scripts et assets Wokwi
- les macros de feature doivent refléter le périmètre réellement compilé

## Adapter un module existant

Quand on adapte un module, il faut d'abord identifier ce qu'il consomme réellement: hardware, métier, composition produit ou simple service Core.

### Cas 1: le module dépend seulement du hardware

Exemples:

- choix du port série
- choix d'un bus I2C
- accès à un point physique d'IO

Dans ce cas, le module doit dépendre de:

- `BoardSpec`
- `BoardSignal`
- `IoCapability`
- éventuellement d'un helper `boardFind...()`

Il ne doit pas importer directement des rôles métier comme `FiltrationPump` s'il ne fait qu'accéder au matériel.

### Cas 2: le module dépend du métier

Exemples:

- un device piscine avec débit et capacité de bidon
- une logique de filtration
- des seuils de température ou de pression

Dans ce cas, le module doit dépendre de:

- `DomainSpec`
- `DomainRole`
- les presets/defaults du sous-domaine concerné

Il ne doit pas connaître les pins ou les GPIO réels.

### Cas 3: le module assemble hardware + métier

Exemples:

- `IOModule`
- `PoolDeviceModule`
- bootstrap de profil

Dans ce cas, le module ou le bootstrap doit utiliser le mapping:

- `BoardSignal -> DomainRole`

et faire le pont entre:

- `BoardSpec` pour savoir comment parler au monde physique
- `DomainSpec` pour savoir quelle signification métier donner à ce signal

### Règle pratique pour modifier un module

Quand on modifie un module existant, suivre cette checklist:

1. identifier si le besoin est hardware, métier ou produit
2. supprimer les nombres magiques s'ils expriment une règle métier
3. supprimer les pins hardcodées si elles expriment une carte
4. remplacer les branchements directs par lecture des specs
5. vérifier que le module peut encore être exclu du build sans casser les autres profils

### Ce qu'un module ne doit pas faire

Un module générique ne doit pas:

- décider lui-même quel produit est compilé
- instancier des modules d'un autre profil
- importer un catalogue global de tous les domaines ou de toutes les cartes
- réintroduire une dépendance à un ancien `main_*` monolithique

## Quand modifier quoi

### Tu changes une pin ou un bus

Modifier:

- `src/Board/*`

Vérifier ensuite:

- les bootstraps de profils qui utilisent cette carte
- les modules qui lisent explicitement certains UART/bus

### Tu changes l'affectation d'un relais ou d'une sonde

Modifier:

- `src/Domain/*`

Vérifier ensuite:

- `IOModule`
- `PoolDeviceModule`
- `PoolLogicModule`
- le bootstrap du profil concerné

### Tu ajoutes un comportement produit

Modifier:

- `src/Profiles/<Produit>/*`
- éventuellement `platformio.ini`

Ne pas modifier:

- un autre profil
- un domaine sans raison
- la carte si le hardware ne change pas

### Tu ajoutes un module optionnel

Modifier:

- `src/Profiles/<Produit>/...ModuleInstances.cpp`
- `src/Profiles/<Produit>/...Bootstrap.cpp`
- `src/App/FeatureFlags.h` si la feature doit être exposée globalement
- `platformio.ini` pour l'inclusion/exclusion réelle au build

### Tu ajoutes un nouveau rôle métier

Modifier:

- `src/Domain/DomainTypes.h`
- le sous-domaine concerné dans `src/Domain/Pool/` ou `src/Domain/Supervisor/`
- les presets/bindings/defaults associés
- les modules qui exploitent explicitement ce rôle

### Tu ajoutes un nouveau signal de carte

Modifier:

- `src/Board/BoardTypes.h`
- la `BoardSpec` concernée
- éventuellement le domaine si ce signal reçoit un rôle métier

## Règles mémoire et robustesse

Pour rester compatible ESP32 et pression RAM:

- préférer `constexpr`, `static const`, tableaux fixes et enums compacts
- éviter `std::vector`, `std::map`, `std::string` dans l'architecture centrale
- éviter d'introduire de nouvelles structures heap pour le refactor
- garder les catalogues comme simples vues statiques sur des données immuables

Les profils et domaines doivent rester essentiellement déclaratifs.

## Héritage et migration

La migration n'est pas encore totalement terminée, mais la direction est désormais claire:

- `main.cpp` est unique
- le bootstrap commun vit dans `src/App/`
- la composition produit vit dans `src/Profiles/`
- la vérité hardware vit dans `src/Board/`
- la vérité métier vit dans `src/Domain/`

Les reliquats qui subsistent encore doivent être absorbés selon cette même logique, sans recréer de couche de compatibilité globale.

## Résumé opérationnel

Quand tu modifies le dépôt, pose-toi toujours ces 4 questions dans cet ordre:

1. est-ce une vérité hardware
2. est-ce une vérité métier
3. est-ce un choix produit
4. est-ce une décision de compilation

La réponse détermine presque toujours le bon répertoire:

- hardware -> `src/Board/`
- métier -> `src/Domain/`
- produit -> `src/Profiles/`
- boot commun / flags / contexte -> `src/App/`
- inclusion réelle au build -> `platformio.ini`
