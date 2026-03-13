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
- appui long bouton (3s par défaut): reset `wifi.ssid`/`wifi.pass`, notification provisioning, reboot

## Brochage (valeurs par défaut)

- `FLOW_SUPERVISOR_TFT_BL=14`
- `FLOW_SUPERVISOR_TFT_CS=15`
- `FLOW_SUPERVISOR_TFT_DC=4`
- `FLOW_SUPERVISOR_TFT_RST=5`
- `FLOW_SUPERVISOR_TFT_MOSI=18`
- `FLOW_SUPERVISOR_TFT_SCLK=19`
- `FLOW_SUPERVISOR_PIR_PIN=27`
- `FLOW_SUPERVISOR_WIFI_RESET_PIN=23`

