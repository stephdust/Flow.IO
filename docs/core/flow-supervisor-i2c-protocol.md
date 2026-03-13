# Protocole Flow.IO <-> Supervisor

Cette page documente le protocole d'échange utilisé entre le firmware `FlowIO` et le firmware `Supervisor` pour la configuration distante, la lecture d'état runtime et les actions système.

Le protocole repose sur:
- un transport I2C point à point
- un framing binaire minimal
- des payloads courts binaires ou JSON selon l'opération
- un séquencement requête/réponse initié uniquement par le Supervisor

## Vue d'ensemble

Rôles:
- `Supervisor` porte le module [`i2ccfg.client`](../../src/Modules/Network/I2CCfgClientModule/I2CCfgClientModule.h)
- `Flow.IO` porte le module [`i2ccfg.server`](../../src/Modules/Network/I2CCfgServerModule/I2CCfgServerModule.h)

Rôles I2C:
- `Supervisor`: maître I2C
- `Flow.IO`: esclave I2C

Bus utilisé:
- contrôleur I2C `1` des deux côtés (`Wire1` sur ESP32)

Configuration par défaut:
- adresse I2C serveur Flow.IO: `0x42`
- fréquence: `100000 Hz`
- GPIO par défaut Flow.IO: SDA `12`, SCL `14`
- GPIO par défaut Supervisor: SDA `21`, SCL `22`

Références source:
- framing et opcodes: [`include/Core/I2cCfgProtocol.h`](../../include/Core/I2cCfgProtocol.h)
- transport I2C: [`src/Core/I2cLink.cpp`](../../src/Core/I2cLink.cpp)
- client Supervisor: [`src/Modules/Network/I2CCfgClientModule/I2CCfgClientModule.cpp`](../../src/Modules/Network/I2CCfgClientModule/I2CCfgClientModule.cpp)
- serveur Flow.IO: [`src/Modules/Network/I2CCfgServerModule/I2CCfgServerModule.cpp`](../../src/Modules/Network/I2CCfgServerModule/I2CCfgServerModule.cpp)

## Principe d'échange

Le Supervisor envoie toujours une trame de requête complète, puis lit immédiatement une trame de réponse.

Cycle nominal:
1. le maître écrit la requête I2C
2. le serveur esclave reçoit la trame dans `onReceive`
3. le serveur prépare une réponse interne
4. le maître attend environ `4.5 ms`
5. le maître effectue une lecture I2C
6. l'esclave renvoie la réponse préparée dans `onRequest`

Le helper [`I2cLink::transfer`](../../src/Core/I2cLink.cpp) applique déjà:
- une exclusion mutuelle sur le bus
- un délai de traitement esclave (`4500 us`)
- une petite relance de lecture si la première lecture ne renvoie rien

## Format des trames

Version actuelle:
- `Version = 1`

Magic bytes:
- requête: `0xA5`
- réponse: `0x5A`

Tailles:
- taille max payload: `96` octets
- header requête: `5` octets
- header réponse: `6` octets
- taille max trame requête: `101` octets
- taille max trame réponse: `102` octets

### Requête

Format:

```text
+--------+---------+------+-------+-------------+---------...
| magic  | version | op   | seq   | payload_len | payload
+--------+---------+------+-------+-------------+---------...
| 0xA5   | 1       | u8   | u8    | u8          | 0..96 B
+--------+---------+------+-------+-------------+---------...
```

Champs:
- `magic`: signature de trame requête
- `version`: version du protocole
- `op`: opcode de l'opération
- `seq`: numéro de séquence recopié par le serveur
- `payload_len`: taille utile du payload

### Réponse

Format:

```text
+--------+---------+------+-------+--------+-------------+---------...
| magic  | version | op   | seq   | status | payload_len | payload
+--------+---------+------+-------+--------+-------------+---------...
| 0x5A   | 1       | u8   | u8    | u8     | u8          | 0..96 B
+--------+---------+------+-------+--------+-------------+---------...
```

Champs:
- `magic`: signature de trame réponse
- `version`: version du protocole
- `op`: recopie de l'opcode demandé
- `seq`: recopie du numéro de séquence demandé
- `status`: code résultat applicatif
- `payload_len`: taille utile du payload réponse

## Codes statut

Valeurs définies dans [`include/Core/I2cCfgProtocol.h`](../../include/Core/I2cCfgProtocol.h):

| Valeur | Nom | Signification |
|---|---|---|
| `0` | `StatusOk` | opération acceptée et réponse valide |
| `1` | `StatusBadRequest` | payload invalide, opcode mal formé, domaine/action inconnus |
| `2` | `StatusNotReady` | service Flow.IO indisponible |
| `3` | `StatusRange` | index, offset ou module hors plage |
| `4` | `StatusOverflow` | tampon patch trop grand |
| `5` | `StatusFailed` | échec interne côté serveur |

Important:
- `status != ok` ne veut pas forcément dire erreur de transport I2C
- le client distingue les erreurs de transport des erreurs protocole/applicatives

## Politique de robustesse côté client

Le client Supervisor:
- incrémente un compteur `seq` sur 8 bits à chaque transaction
- retente jusqu'à `3` fois certaines transactions si la réponse est absente ou invalide
- rejette une réponse si `magic`, `version`, `op` ou `seq` ne correspondent pas
- rejette une réponse si `payload_len` dépasse `96`

En cas d'échec de transport ou de protocole, le service haut niveau retourne généralement un JSON d'erreur via [`writeErrorJson`](../../include/Core/ErrorCodes.h).

## Catalogue des opérations

### `OpPing` (`0x01`)

But:
- valider le lien
- vérifier que le serveur répond
- récupérer la version protocole et l'adresse serveur

Payload requête:
- aucun

Payload réponse en succès:

| Octet | Contenu |
|---|---|
| `0` | version protocole |
| `1` | adresse I2C serveur |

Exemple:
- réponse `[1, 0x42]`

### `OpListCount` (`0x10`)

But:
- connaître le nombre total de modules de configuration exposés par Flow.IO

Payload requête:
- aucun

Payload réponse:
- 1 octet: nombre de modules

### `OpListItem` (`0x11`)

But:
- lire le nom d'un module de configuration par index

Payload requête:

| Octet | Contenu |
|---|---|
| `0` | index du module |

Payload réponse:
- nom ASCII du module, non nul-terminé dans la trame

Exemples de modules:
- `wifi`
- `mqtt`
- `system`
- `i2c/cfg/server`

### `OpListChildrenCount` (`0x12`)

But:
- lister les enfants directs d'un préfixe de branche de configuration

Payload requête:
- préfixe ASCII facultatif, sans terminator

Normalisation côté serveur:
- suppression des `/` en tête
- suppression des `/` en fin

Payload réponse:

| Octet | Contenu |
|---|---|
| `0` | nombre d'enfants directs |
| `1` | `1` si le préfixe correspond exactement à un module existant, sinon `0` |

Usage typique:
- navigation arborescente de la configuration côté Supervisor

### `OpListChildrenItem` (`0x13`)

But:
- récupérer un enfant direct précis d'un préfixe

Payload requête:

| Octet | Contenu |
|---|---|
| `0` | index enfant demandé |
| `1..n` | préfixe ASCII facultatif |

Payload réponse:
- nom ASCII de l'enfant demandé

### `OpGetModuleBegin` (`0x20`)

But:
- démarrer l'export JSON complet d'un module

Payload requête:
- nom ASCII du module

Traitement côté Flow.IO:
- sérialisation dans `moduleJson_`
- tampon serveur de taille `Limits::JsonCfgBuf` soit actuellement `1024` octets
- cas particulier: `wifi` peut exporter le mot de passe en clair pour le chemin de synchronisation/debug utilisé ici

Payload réponse:

| Octets | Contenu |
|---|---|
| `0..1` | longueur totale JSON, little-endian |
| `2` | flags, bit `0x02` si JSON tronqué |

Notes:
- la réponse ne contient pas encore le JSON lui-même
- elle annonce seulement la taille totale à relire ensuite

### `OpGetModuleChunk` (`0x21`)

But:
- lire le JSON module par morceaux

Payload requête:

| Octets | Contenu |
|---|---|
| `0..1` | offset, little-endian |
| `2` | taille demandée (`want`) |

Comportement:
- si `want == 0` ou `want > 96`, le serveur borne à `96`
- le dernier chunk invalide ensuite le buffer serveur

Payload réponse:
- sous-chaîne brute du JSON demandé

### `OpGetRuntimeStatusBegin` (`0x22`)

But:
- démarrer la lecture d'un snapshot runtime ciblé sur un seul domaine

Payload requête:

| Octet | Contenu |
|---|---|
| `0` | identifiant de domaine |

Domaine disponibles:

| Valeur | Domaine | Contenu |
|---|---|---|
| `1` | `system` | firmware, uptime, heap |
| `2` | `wifi` | état Wi-Fi, IP, RSSI |
| `3` | `mqtt` | état MQTT, compteurs d'erreurs |
| `4` | `i2c` | état du lien Supervisor/Flow.IO |
| `5` | `pool` | drapeaux de mode piscine |
| `6` | `alarm` | alarmes actives |

Traitement côté Flow.IO:
- construction dans `statusJson_`
- tampon serveur de taille fixe `448` octets

Payload réponse:

| Octets | Contenu |
|---|---|
| `0..1` | longueur totale JSON, little-endian |
| `2` | flags, bit `0x02` si JSON tronqué |

### `OpGetRuntimeStatusChunk` (`0x23`)

But:
- lire le JSON runtime d'un domaine par morceaux

Payload requête:

| Octets | Contenu |
|---|---|
| `0..1` | offset, little-endian |
| `2` | taille demandée |

Payload réponse:
- sous-chaîne brute du JSON du domaine

Important:
- depuis la refactorisation par domaine, on ne construit plus un unique gros JSON status côté Flow.IO
- l'agrégation multi-domaines se fait désormais côté Supervisor si besoin

### `OpPatchBegin` (`0x30`)

But:
- annoncer l'envoi d'un patch JSON de configuration

Payload requête:

| Octets | Contenu |
|---|---|
| `0..1` | taille totale du patch, little-endian |

Contraintes:
- taille `> 0`
- taille `< sizeof(patchBuf_)`
- taille `<= Limits::JsonConfigApplyBuf`, soit actuellement `4096` octets

Réponse:
- pas de payload utile en succès

### `OpPatchWrite` (`0x31`)

But:
- envoyer un fragment du patch JSON

Payload requête:

| Octets | Contenu |
|---|---|
| `0..1` | offset d'écriture, little-endian |
| `2..n` | données JSON |

Contraintes:
- l'offset doit correspondre exactement à la position attendue
- la taille utile maximale par trame vaut `94` octets, car `2` octets sont pris par l'offset

Réponse:
- pas de payload utile en succès

### `OpPatchCommit` (`0x32`)

But:
- demander l'application du patch déjà transféré

Préconditions:
- un `PatchBegin` valide a été reçu
- tous les octets ont été envoyés

Traitement côté Flow.IO:
- terminaison `\0` du buffer patch
- appel à `cfgSvc_->applyJson(...)`

Payload réponse:
- JSON d'acquittement, par exemple:

```json
{"ok":true,"where":"i2c/cfg/apply"}
```

ou en erreur:

```json
{"ok":false,"err":{"code":"CfgApplyFailed","where":"i2c/cfg/apply","retryable":false}}
```

### `OpSystemAction` (`0x40`)

But:
- demander une action système distante sur Flow.IO

Payload requête:

| Octet | Action |
|---|---|
| `1` | reboot |
| `2` | factory reset |

Réponse succès:

```json
{"ok":true,"queued":true,"action":"reboot"}
```

ou:

```json
{"ok":true,"queued":true,"action":"factory_reset"}
```

Important:
- l'action est mise en file côté serveur
- l'exécution réelle est asynchrone dans une task dédiée

## JSON transportés

Le protocole est mixte:
- framing binaire compact pour les en-têtes, tailles, offsets et statuts
- charge utile JSON pour les données de config, snapshots runtime et acquittements applicatifs

### Forme des erreurs JSON

Le format partagé est produit par [`include/Core/ErrorCodes.h`](../../include/Core/ErrorCodes.h):

```json
{
  "ok": false,
  "err": {
    "code": "Failed",
    "where": "flowcfg.runtime_status.chunk",
    "retryable": false
  }
}
```

Champs:
- `code`: erreur métier synthétique
- `where`: zone logique ayant échoué
- `retryable`: indique si un retry a du sens

### Exemples de snapshots runtime par domaine

`system`:

```json
{
  "ok": true,
  "fw": "FlowIO x.y.z",
  "upms": 123456,
  "heap": {
    "free": 101232,
    "min": 84320,
    "larg": 65536,
    "frag": 12
  }
}
```

`wifi`:

```json
{
  "ok": true,
  "wifi": {
    "rdy": true,
    "ip": "192.168.1.24",
    "rssi": -61,
    "hrss": true
  }
}
```

`mqtt`:

```json
{
  "ok": true,
  "mqtt": {
    "rdy": true,
    "rxdrp": 0,
    "prsf": 0,
    "hndf": 0,
    "ovr": 0
  }
}
```

`i2c`:

```json
{
  "ok": true,
  "i2c": {
    "ena": true,
    "sta": true,
    "adr": 66,
    "req": 123,
    "breq": 0,
    "seen": true,
    "ago": 42,
    "lnk": true
  }
}
```

## Séquences typiques

### Découverte des modules

1. `OpListCount`
2. pour chaque index: `OpListItem`

### Lecture d'un module JSON

1. `OpGetModuleBegin(module)`
2. boucle `OpGetModuleChunk(offset, want)` jusqu'à avoir lu `totalLen`

### Lecture d'un domaine runtime

1. `OpGetRuntimeStatusBegin(domain)`
2. boucle `OpGetRuntimeStatusChunk(offset, want)` jusqu'à avoir lu `totalLen`

### Application d'un patch JSON

1. `OpPatchBegin(totalLen)`
2. boucle `OpPatchWrite(offset, chunk)`
3. `OpPatchCommit`

### Action système distante

1. `OpSystemAction(actionId)`
2. réception d'un accusé "queued"
3. exécution différée côté Flow.IO

## Gestion mémoire et motivation du découpage par domaine

Le protocole a été ajusté pour limiter l'empreinte RAM sur les ESP32:
- un buffer JSON module dédié côté serveur
- un buffer JSON status dédié côté serveur, ramené à `448` octets
- lecture runtime désormais découpée par domaine
- réassemblage éventuel effectué côté Supervisor seulement si nécessaire

Objectif:
- éviter un gros buffer status monolithique permanent
- rendre l'ajout futur de nouveaux champs plus sûr
- garder un coût RAM borné sur Flow.IO

## Consommateurs actuels du protocole

Le service `flowcfg` exposé par `Supervisor` sert notamment à:
- l'écran HMI Supervisor
- l'interface web du Supervisor
- les commandes système distantes `flow.system.reboot` et `flow.system.factory_reset`

## Compatibilité et versionnement

Aujourd'hui:
- une seule version protocole est définie: `1`

Règles implicites de compatibilité:
- conserver les opcodes existants
- ne pas changer le sens des codes statut existants
- ajouter de nouveaux domaines ou de nouvelles opérations sans casser les anciennes
- toute évolution incompatible doit changer `Version`

## Limites et points d'attention

- le protocole n'inclut ni CRC applicatif ni authentification
- la robustesse repose sur I2C local, `magic`, `version`, `seq`, `op`, et la validation des longueurs
- un seul échange est actif à la fois par lien I2C
- les buffers JSON côté Flow.IO sont temporaires et invalidés après le dernier chunk servi
- le module `wifi` peut exposer des secrets en clair via l'export module dans ce chemin de synchronisation

## Résumé opérationnel

À retenir:
- transport: I2C maître/esclave
- framing: binaire compact
- données: JSON chunké
- initiateur unique: Supervisor
- configuration distante: oui
- lecture runtime: oui, désormais par domaine
- actions système distantes: oui

