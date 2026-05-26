# HAModule (`moduleId: ha`)

## Rôle

Publication Home Assistant MQTT Discovery:
- registre d'entités statiques (sensor, binary_sensor, switch, number, button)
- publication discovery retainée
- refresh automatique sur changements runtime pertinents
- support du champ discovery `has_entity_name` sur les sensors (piloté par l'entité appelante)

Type: module actif (event-driven par notification task).

## Dépendances

- `eventbus`
- `config`
- `datastore`
- `mqtt`

## Affinité / cadence

- core: 0
- task: `ha`
- loop bloquant sur notification (`ulTaskNotifyTake`)

## Services exposés

- `ha` -> `HAService`
  - `addSensor`, `addBinarySensor`, `addSwitch`, `addNumber`, `addButton`
  - `requestRefresh`

## Services consommés

- `eventbus`
- `datastore`
- `mqtt`

## Capacités statiques

Capacités compile-time actuelles dans `src/Modules/Network/HAModule/HAModule.h`:

| Type d'entité | Capacité |
|---|---:|
| sensors | 40 |
| binary sensors | 6 |
| switches | 14 |
| numbers | 14 |
| buttons | 24 |
| cleanups discovery | 9 |

## Config / NVS

Module config: `ha` (`moduleId = ConfigModuleId::Ha`, branche locale `1`):
- `enabled`
- `vendor`
- `device_id`
- `disc_prefix`
- `model`

## DataStore

Écritures via `HARuntime.h`:
- `HaPublished`
- `HaVendor`
- `HaDeviceId`

## EventBus

Abonnement:
- `DataChanged`

Réactions:
- sur `WifiReady`/`MqttReady` -> tentative/pending de publication auto-discovery

## MQTT

- producteur MQTT enregistré (`producerId` dédié HA) sur le cœur TX unifié
- jobs HA en mode IDs (`producerId + messageId`), sans publication directe module -> broker
- build topic/payload discovery à la demande via `MqttBuildContext` (buffer central MQTT)
- construit topics discovery:
  - `<discoveryPrefix>/<component>/<nodeTopicId>/<objectId>/config`
- préfixe `object_id` configurable par profil:
  - défaut `fioNN_*`
  - profil `Micronova`: `pioNN_*`
  - `NN` dérive du `deviceId` MQTT effectif (`mqtt.topicDeviceId` / `mq_tid` si défini, sinon fallback auto)
- payloads incluent:
  - device metadata
  - `device.identifiers[0] = <vendor>-<mqttDeviceIdEffectif>` (donc suit `mqtt.topicDeviceId` / `mq_tid` si défini)
  - `availability` basée sur topic `status`
- capteurs diagnostic natifs publiés:
  - `alarms_pack` (`rt/alarms/p`)
  - `uptime` (`rt/system/state`, conversion en minutes depuis `upt_ms`)
  - `heap_free_bytes` (`rt/system/state`, conversion en `ko`)
  - `heap_min_free_bytes` (`rt/system/state`, conversion en `ko`)
  - `heap_fragmentation` (`rt/system/state`, valeur `%`)

## Comportement refresh

- `add*` met à jour la table d'entités et demande refresh
- `requestRefresh` remet `published=false` et notifie la task
- publication effective seulement si MQTT connecté et `mqttReady(DataStore)==true`

## Mode one-shot

Le build peut définir `FLOW_HA_ONESHOT_DISCOVERY=1` pour publier l'auto-discovery une seule fois au démarrage.
Ce mode est utilisé par les profils `FlowIO` et `Micronova`:
- les tables d'entités HA sont allouées dynamiquement au lieu d'être conservées en `.bss`
- le producteur MQTT de configuration HA n'est pas instancié
- après publication retained de toutes les entités discovery, les tables sont libérées et la tâche `ha` appelle `vTaskDelete(nullptr)`
- le service HA reste présent mais refuse les nouveaux enregistrements après teardown, afin d'éviter des pointeurs pendants dans les services/callbacks existants
- pour diagnostiquer la séquence de boot one-shot, le build `FlowIO` peut activer `FLOW_HA_BOOT_TRACE=1` (logs de jalons alloc/enqueue/publish/release)
