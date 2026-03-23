# Services Core et invocation

Toutes les interfaces sont sous `src/Core/Services/` et agrégées par `src/Core/Services/Services.h`.

## Utilisation standard

Exemple d'accès à un service dans `init()`:

```cpp
void MyModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    const IOServiceV2* io = services.get<IOServiceV2>("io");
    const TimeService* time = services.get<TimeService>("time");
    // vérifier null avant usage
}
```

Exemple d'exposition d'un service:

```cpp
class MyModule : public Module {
private:
    bool doThing_(int value);

    MyService svc_{
        ServiceBinding::bind<&MyModule::doThing_>,
        this
    };
};

void MyModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    services.add("myservice", &svc_);
}
```

Pattern recommandé:

- déclarer le service comme membre `svc_` du module
- binder directement des méthodes d'instance via `ServiceBinding::bind<&MyModule::method_>`
- utiliser `ServiceBinding::bind_or<&MyModule::method_, fallback>` seulement si la valeur par défaut doit être explicite
- si la signature du contrat ne correspond pas exactement à la méthode métier, ajouter une petite méthode d'adaptation `xxxSvc_()` au lieu d'un wrapper statique `void* ctx`

## Service IDs utilisés dans le firmware

- `loghub` -> `LogHubService`
- `logsinks` -> `LogSinkRegistryService`
- `eventbus` -> `EventBusService`
- `config` -> `ConfigStoreService`
- `datastore` -> `DataStoreService`
- `cmd` -> `CommandService`
- `hmi` -> `HmiService`
- `alarms` -> `AlarmService`
- `wifi` -> `WifiService`
- `time` -> `TimeService`
- `time.scheduler` -> `TimeSchedulerService`
- `mqtt` -> `MqttService`
- `ha` -> `HAService`
- `io` -> `IOServiceV2`
- `pooldev` -> `PoolDeviceService`

## Contrats principaux

### `CommandService`
- `registerHandler(cmd, fn, userCtx)`
- `execute(cmd, json, args, reply, replyLen)`
- Utilisé par MQTT (`cmd`) et console logique interne.

### `HmiService`
- navigation menu config (`openConfigHome`, `openConfigModule`)
- synchronisation affichage (`requestRefresh`)
- snapshot JSON menu (`buildConfigMenuJson`)

### `ConfigStoreService`
- `applyJson(json)`
- `toJson(out)`
- `toJsonModule(module, out, outLen, truncated)`
- `listModules(out, max)`

### `DataStoreService`
- Expose `DataStore* store`
- accès lecture/écriture via helpers runtime (`WifiRuntime.h`, `IORuntime.h`, etc.)

### `EventBusService`
- Expose `EventBus* bus`
- abonnement via `subscribe(EventId, cb, user)`
- publication via `post(EventId, payload, len)`

### `AlarmService`
- enregistrement d'alarme (`registerAlarm`)
- ack (`ack`, `ackAll`)
- état (`isActive`, `isAcked`, `activeCount`, `highestSeverity`)
- snapshots (`buildSnapshot`, `listIds`, `buildAlarmState`)

### `WifiService`
- état (`state`)
- connectivité (`isConnected`)
- IP string (`getIP`)

### `TimeService`
- état sync (`state`, `isSynced`)
- epoch (`epoch`)
- format local (`formatLocalTime`)

### `TimeSchedulerService`
- gestion slots (`setSlot/getSlot/clearSlot/clearAll`)
- observation (`usedCount/activeMask/isActive`)

### `MqttService`
- enqueue job (`enqueue(producerId, messageId, prio, flags)`)
- enregistrement producteur (`registerProducer(producer)`)
- format topic (`formatTopic(suffix, out, outLen)`)
- état transport (`isConnected`)

Le service MQTT est **job-based**:
- le service ne prend plus directement `topic/payload` en entrée
- chaque producteur conserve son mapping local `messageId -> build topic/payload`
- le cœur MQTT construit/publie au dernier moment via `MqttBuildContext`

### `HAService`
- enregistrement entités discovery:
  - `addSensor`
  - `addBinarySensor`
  - `addSwitch`
  - `addNumber`
  - `addButton`
- pour `addSensor`, l'entrée peut porter `hasEntityName` pour publier `has_entity_name=true` côté discovery
- refresh: `requestRefresh`

### `IOServiceV2`
- découverte endpoints: `count`, `idAt`, `meta`
- I/O digital: `readDigital`, `writeDigital`
- I/O analog: `readAnalog`
- tick/cycle: `tick`, `lastCycle`

### `PoolDeviceService`
- inventory: `count`, `meta`
- états: `readActualOn`
- commandes: `writeDesired`, `refillTank`

## Bonnes pratiques

- Toujours vérifier la présence du service (`nullptr`) en init.
- Ne pas conserver de pointeurs internes non valides: les services sont stables après init.
- Pour les chemins critiques (loop), éviter parsing JSON ou allocations dynamiques répétées.
