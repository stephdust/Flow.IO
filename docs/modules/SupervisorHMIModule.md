# SupervisorHMIModule (`moduleId: hmi.supervisor`)

## Rôle

Interface HMI locale du firmware Supervisor:
- rendu statut réseau / lien Flow / firmware update sur TFT ST7789
- gestion rétroéclairage automatique avec capteur PIR
- reset WiFi local sur appui long bouton poussoir

## Dépendances

- `loghub`
- `config`
- `wifi`
- `wifiprov`
- `fwupdate`
- `i2ccfg.client`

## Comportement

- écran rafraîchi périodiquement sans blocage
- extinction backlight après timeout d'inactivité PIR
- backlight forcé ON pendant update firmware
- appui long bouton (3s par défaut dans le profil Supervisor): reset `wifi.ssid`/`wifi.pass`, notification provisioning, reboot

## Brochage et timings

Les valeurs viennent maintenant de la single source of truth Supervisor:

- hardware: `src/Board/SupervisorBoardRev1.h`
- timings runtime: `src/Profiles/Supervisor/SupervisorProfile.cpp`

Valeurs actuelles:

- TFT backlight: `14`
- TFT CS: `15`
- TFT DC: `4`
- TFT RST: `5`
- TFT MOSI: `19`
- TFT SCLK: `18`
- PIR: `36`
- bouton reset WiFi: `23`
- timeout extinction backlight: `10000 ms`
- appui long reset WiFi: `3000 ms`
