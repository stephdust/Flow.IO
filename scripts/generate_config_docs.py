"""
Generate frontend config documentation index for Supervisor WebInterface.

Primary source:
- Inline comments just above each ConfigVariable declaration:
    // CFGDOC: {"label":"...", "help":"...", "unit":"..."}

Secondary source:
- Automatic fallback (humanized json key + generic help)
- Optional overrides file for legacy/manual patching:
    scripts/config_docs.fr.overrides.json

Output:
- data/webinterface/cfgdocs.fr.json
"""

import bisect
import json
import os
import re
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

Import = type("Import", (), {})

try:
    Import("env")  # type: ignore
except Exception:
    env = None


def _get_project_dir() -> Path:
    if env is not None:
        try:
            return Path(env.get("PROJECT_DIR"))
        except Exception:
            pass
    return Path(os.getcwd())


DECL_RE = re.compile(
    r"ConfigVariable\s*<(?P<template>[^>]+)>\s+"
    r"(?P<var>[A-Za-z_][A-Za-z0-9_]*)\s*(?P<array>\[[^\]]+\])?\s*"
    r"\{(?P<body>.*?)\};",
    re.S,
)

META_RE = re.compile(
    r',\s*"(?P<json>[^"]+)"\s*,\s*"(?P<module>[^"]+)"\s*,\s*ConfigType::(?P<type>[A-Za-z0-9_]+)',
    re.S,
)

CFGDOC_RE = re.compile(r"^\s*//\s*CFGDOC:\s*(\{.*\})\s*$")

JSON_ASSIGN_RE = re.compile(
    r"(?P<var>[A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]+\])?\s*\.jsonName\s*=\s*\"(?P<json>[^\"]+)\""
)

MODULE_ASSIGN_RE = re.compile(
    r"(?P<var>[A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]+\])?\s*\.moduleName\s*=\s*\"(?P<module>[^\"]+)\""
)

MODULE_ASSIGN_SYM_RE = re.compile(
    r"(?P<var>[A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]+\])?\s*\.moduleName\s*=\s*(?P<sym>[A-Za-z_][A-Za-z0-9_]*)\s*;"
)

MODULE_CONST_RE = re.compile(
    r"(?:static\s+)?constexpr\s+const\s+char\s*\*\s*(?P<sym>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*\"(?P<val>[^\"]+)\"\s*;"
)


TOKEN_MAP = {
    "id": "ID",
    "io": "IO",
    "ph": "pH",
    "orp": "ORP",
    "psi": "PSI",
    "mqtt": "MQTT",
    "mdns": "mDNS",
    "ntp": "NTP",
    "ssid": "SSID",
    "ms": "ms",
    "min": "min",
    "max": "max",
}

ANALOG_KEY_RE = re.compile(r"^a(?P<idx>[0-9]+)_(?P<field>name|source|channel|binding_port|c0|c1|prec|min|max)$")
DIGITAL_INPUT_KEY_RE = re.compile(r"^i(?P<idx>[0-9]+)_(?P<field>name|binding_port|active_high|pull_mode|edge_mode|c0|prec)$")
DIGITAL_OUTPUT_KEY_RE = re.compile(r"^d(?P<idx>[0-9]+)_(?P<field>name|pin|binding_port|active_high|initial_on|momentary|pulse_ms)$")
ANALOG_MODULE_RE = re.compile(r"^io/input/a(?P<idx>[0-9]+)$")
PORT_ENUM_RE = re.compile(r"^\s*(?P<name>Port[A-Za-z0-9_]+)\s*=\s*(?P<value>\d+)\s*,\s*$", re.M)
BINDING_ENTRY_RE = re.compile(
    r"\{\s*(?P<port>Port[A-Za-z0-9_]+)\s*,\s*(?P<kind>IO_PORT_KIND_[A-Z0-9_]+)\s*,\s*(?P<param0>[^,]+)\s*,\s*(?P<param1>[^}]+)\}",
    re.S,
)
DIGITAL_INPUT_ROLE_DEFAULT_RE = re.compile(
    r"\{\s*DomainRole::[A-Za-z0-9_]+\s*,\s*(?P<port>PortDigitalIn[0-9]+)\s*,\s*(?P<mode>IO_DIGITAL_INPUT_[A-Z_]+)\s*,",
    re.S,
)
BOARD_IO_POINT_RE = re.compile(
    r'\{\s*"(?P<name>[^"]+)"\s*,\s*IoCapability::(?P<cap>[A-Za-z0-9_]+)\s*,\s*BoardSignal::(?P<signal>[A-Za-z0-9_]+)\s*,\s*(?P<pin>-?\d+)\s*,',
    re.S,
)
BOARD_PIN_REF_RE = re.compile(r"BoardProfiles::kFlowIOBoardRev1IoPoints\[(?P<idx>\d+)\]\.pin")

FLOWIO_BINDING_SET_ANALOG = "flowio_binding_port_analog"
FLOWIO_BINDING_SET_DIGITAL_INPUT = "flowio_binding_port_digital_input"
FLOWIO_BINDING_SET_DIGITAL_OUTPUT = "flowio_binding_port_digital_output"
FLOWIO_EDGE_MODE_SET = "flowio_edge_mode"


def _io_slot_token(idx: int) -> str:
    return f"{idx:02d}"


def _humanize_json_name(json_name: str) -> str:
    s = json_name.strip()
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    parts = [p for p in s.split("_") if p]
    if not parts:
        return json_name
    out = []
    for idx, p in enumerate(parts):
        key = p.lower()
        mapped = TOKEN_MAP.get(key)
        if mapped is not None:
            out.append(mapped)
        elif idx == 0:
            out.append(p.capitalize())
        else:
            out.append(p.lower())
    return " ".join(out)


def _default_help(module_name: str, json_name: str, type_name: str) -> str:
    return f"Parametre '{json_name}' pour la branche '{module_name}' (type {type_name})."


def _default_series_help(var_name: str, json_name: str, type_name: str) -> str:
    return f"Parametre '{json_name}' issu de la serie '{var_name}' (type {type_name})."


def _binding_enum_set_for_doc(module_name: str, json_name: str) -> Optional[str]:
    if json_name == "binding_port":
        if module_name.startswith("io/input/a"):
            return FLOWIO_BINDING_SET_ANALOG
        if module_name.startswith("io/input/i"):
            return FLOWIO_BINDING_SET_DIGITAL_INPUT
        if module_name.startswith("io/output/d"):
            return FLOWIO_BINDING_SET_DIGITAL_OUTPUT
    if json_name == "edge_mode" and module_name.startswith("io/input/i"):
        return FLOWIO_EDGE_MODE_SET
    return None


def _apply_doc_extras(item: dict, module_name: str, json_name: str) -> None:
    enum_set = _binding_enum_set_for_doc(module_name, json_name)
    if enum_set:
        item["enum_set"] = enum_set


def _load_flowio_board_points(src_root: Path) -> List[dict]:
    path = src_root / "Board" / "FlowIOBoardRev1.h"
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8", errors="ignore")
    points: List[dict] = []
    for m in BOARD_IO_POINT_RE.finditer(text):
        points.append({
            "name": m.group("name").strip(),
            "cap": m.group("cap").strip(),
            "signal": m.group("signal").strip(),
            "pin": int(m.group("pin")),
        })
    return points


def _binding_kind_group(kind: str) -> Optional[str]:
    if kind in (
        "IO_PORT_KIND_ADS_INTERNAL_SINGLE",
        "IO_PORT_KIND_ADS_EXTERNAL_DIFF",
        "IO_PORT_KIND_DS18_WATER",
        "IO_PORT_KIND_DS18_AIR",
        "IO_PORT_KIND_INA226",
        "IO_PORT_KIND_SHT40",
        "IO_PORT_KIND_BMP280",
        "IO_PORT_KIND_BME680",
    ):
        return FLOWIO_BINDING_SET_ANALOG
    if kind == "IO_PORT_KIND_GPIO_INPUT":
        return FLOWIO_BINDING_SET_DIGITAL_INPUT
    if kind in ("IO_PORT_KIND_GPIO_OUTPUT", "IO_PORT_KIND_PCF8574_OUTPUT"):
        return FLOWIO_BINDING_SET_DIGITAL_OUTPUT
    return None


def _digital_input_mode_suffix(port_name: str, mode_by_port: Dict[str, str]) -> str:
    mode = mode_by_port.get(port_name, "").strip()
    if mode == "IO_DIGITAL_INPUT_COUNTER":
        return " | compteur"
    if mode == "IO_DIGITAL_INPUT_STATE":
        return " | etat"
    return ""


def _binding_label(kind: str,
                   port_name: str,
                   param0: str,
                   board_points: List[dict],
                   digital_input_mode_by_port: Optional[Dict[str, str]] = None) -> str:
    port_display = f"{port_name}"
    digital_input_mode_by_port = digital_input_mode_by_port or {}
    input_mode_suffix = ""
    if kind == "IO_PORT_KIND_GPIO_INPUT":
        input_mode_suffix = _digital_input_mode_suffix(port_name, digital_input_mode_by_port)

    board_ref = BOARD_PIN_REF_RE.search(param0)
    if board_ref:
        idx = int(board_ref.group("idx"))
        if 0 <= idx < len(board_points):
            point = board_points[idx]
            return f"{port_display} | {point['name']} | GPIO{point['pin']}{input_mode_suffix}"

    raw = param0.strip()
    if kind == "IO_PORT_KIND_ADS_INTERNAL_SINGLE":
        return f"{port_display} | ADS1115 interne | canal {raw}"
    if kind == "IO_PORT_KIND_ADS_EXTERNAL_DIFF":
        return f"{port_display} | ADS1115 externe | paire diff {raw}"
    if kind == "IO_PORT_KIND_DS18_WATER":
        return f"{port_display} | DS18B20 | bus eau"
    if kind == "IO_PORT_KIND_DS18_AIR":
        return f"{port_display} | DS18B20 | bus air"
    if kind == "IO_PORT_KIND_GPIO_INPUT":
        return f"{port_display} | entree GPIO | GPIO{raw}{input_mode_suffix}"
    if kind == "IO_PORT_KIND_GPIO_OUTPUT":
        return f"{port_display} | sortie GPIO | GPIO{raw}"
    if kind == "IO_PORT_KIND_PCF8574_OUTPUT":
        return f"{port_display} | sortie PCF8574 | bit {raw}"
    if kind == "IO_PORT_KIND_INA226":
        return f"{port_display} | INA226 | canal {raw}"
    if kind == "IO_PORT_KIND_SHT40":
        return f"{port_display} | SHT40 | canal {raw}"
    if kind == "IO_PORT_KIND_BMP280":
        return f"{port_display} | BMP280 | canal {raw}"
    if kind == "IO_PORT_KIND_BME680":
        return f"{port_display} | BME680 | canal {raw}"
    return f"{port_display} | {kind} | {raw}"


def _build_flowio_binding_enum_sets(src_root: Path) -> Dict[str, List[dict]]:
    path = src_root / "Profiles" / "FlowIO" / "FlowIOIoLayout.h"
    if not path.exists():
        return {}

    text = path.read_text(encoding="utf-8", errors="ignore")
    board_points = _load_flowio_board_points(src_root)

    port_ids: Dict[str, int] = {}
    for m in PORT_ENUM_RE.finditer(text):
        port_ids[m.group("name").strip()] = int(m.group("value"))

    digital_input_mode_by_port: Dict[str, str] = {}
    for m in DIGITAL_INPUT_ROLE_DEFAULT_RE.finditer(text):
        digital_input_mode_by_port[m.group("port").strip()] = m.group("mode").strip()

    enum_sets: Dict[str, List[dict]] = {
        FLOWIO_BINDING_SET_ANALOG: [],
        FLOWIO_BINDING_SET_DIGITAL_INPUT: [],
        FLOWIO_BINDING_SET_DIGITAL_OUTPUT: [],
        FLOWIO_EDGE_MODE_SET: [
            {"value": 0, "label": "0 | Front descendant"},
            {"value": 1, "label": "1 | Front montant"},
            {"value": 2, "label": "2 | Deux fronts"},
        ],
    }

    for m in BINDING_ENTRY_RE.finditer(text):
        port_name = m.group("port").strip()
        kind = m.group("kind").strip()
        param0 = m.group("param0").strip()
        group = _binding_kind_group(kind)
        port_id = port_ids.get(port_name)
        if group is None or port_id is None:
            continue
        enum_sets[group].append({
            "value": port_id,
            "label": f"{port_id} | {_binding_label(kind, port_name, param0, board_points, digital_input_mode_by_port)}",
        })

    for key in list(enum_sets.keys()):
        enum_sets[key] = sorted(enum_sets[key], key=lambda item: int(item["value"]))
    return enum_sets


def _auto_doc_hint(module_name: str, json_name: str) -> Optional[dict]:
    module_name = module_name.strip("/")
    json_name = json_name.strip()

    if module_name == "io":
        mapping = {
            "enabled": ("Module IO actif", "Active ou désactive la couche IO (entrées/sorties).", None),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name == "io/drivers/bus":
        mapping = {
            "sda": ("Broche bus SDA", "Broche SDA du bus I2C utilisé par le module IO.", None),
            "scl": ("Broche bus SCL", "Broche SCL du bus I2C utilisé par le module IO.", None),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name == "io/drivers/ds18b20":
        mapping = {
            "poll_ms": ("Période lecture DS18B20 (ms)", "Intervalle entre deux lectures des sondes DS18B20.", "ms"),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name == "io/drivers/gpio":
        mapping = {
            "poll_ms": ("Période lecture digitales (ms)", "Intervalle de rafraîchissement des entrées digitales.", "ms"),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name == "io/drivers/ads1115":
        mapping = {
            "poll_ms": ("Période lecture ADS (ms)", "Intervalle entre deux acquisitions des entrées analogiques ADS.", "ms"),
            "gain": ("Gain ADS", "Gain de conversion ADS (pleine échelle).", None),
            "rate": ("Fréquence ADS", "Fréquence d'échantillonnage ADS.", None),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name == "io/drivers/ads1115_int":
        mapping = {
            "address": ("Adresse ADS interne", "Adresse I2C du convertisseur ADS1115 interne.", None),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2], "display_format": "hex"}

    if module_name == "io/drivers/ads1115_ext":
        mapping = {
            "address": ("Adresse ADS externe", "Adresse I2C du convertisseur ADS1115 externe.", None),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2], "display_format": "hex"}

    if module_name == "io/drivers/pcf857x":
        mapping = {
            "enabled": ("PCF actif", "Active l'extension PCF857x pour E/S supplémentaires.", None),
            "address": ("Adresse PCF", "Adresse I2C du composant PCF857x.", None),
            "mask_default": ("Masque par défaut PCF", "État par défaut du masque de sorties PCF.", None),
            "active_low": ("PCF actif à 0", "Force une logique active-bas sur les sorties PCF.", None),
        }
        hit = mapping.get(json_name)
        if hit:
            out = {"label": hit[0], "help": hit[1], "unit": hit[2]}
            if json_name == "address":
                out["display_format"] = "hex"
            return out

    if module_name == "io/drivers/sht40":
        mapping = {
            "enabled": ("SHT40 actif", "Active le driver SHT40.", None),
            "address": ("Adresse SHT40", "Adresse I2C du capteur SHT40.", None),
            "poll_ms": ("Période lecture SHT40 (ms)", "Intervalle entre deux acquisitions SHT40.", "ms"),
        }
        hit = mapping.get(json_name)
        if hit:
            out = {"label": hit[0], "help": hit[1], "unit": hit[2]}
            if json_name == "address":
                out["display_format"] = "hex"
            return out

    if module_name == "io/drivers/bmp280":
        mapping = {
            "enabled": ("BMP280 actif", "Active le driver BMP280.", None),
            "address": ("Adresse BMP280", "Adresse I2C du capteur BMP280.", None),
            "poll_ms": ("Période lecture BMP280 (ms)", "Intervalle entre deux acquisitions BMP280.", "ms"),
        }
        hit = mapping.get(json_name)
        if hit:
            out = {"label": hit[0], "help": hit[1], "unit": hit[2]}
            if json_name == "address":
                out["display_format"] = "hex"
            return out

    if module_name == "io/drivers/bme680":
        mapping = {
            "enabled": ("BME680 actif", "Active le driver BME680.", None),
            "address": ("Adresse BME680", "Adresse I2C du capteur BME680.", None),
            "poll_ms": ("Période lecture BME680 (ms)", "Intervalle entre deux acquisitions BME680.", "ms"),
        }
        hit = mapping.get(json_name)
        if hit:
            out = {"label": hit[0], "help": hit[1], "unit": hit[2]}
            if json_name == "address":
                out["display_format"] = "hex"
            return out

    if module_name == "io/drivers/ina226":
        mapping = {
            "enabled": ("INA226 actif", "Active le driver INA226.", None),
            "address": ("Adresse INA226", "Adresse I2C du capteur INA226.", None),
            "poll_ms": ("Période lecture INA226 (ms)", "Intervalle entre deux acquisitions INA226.", "ms"),
            "shunt_ohms": ("Shunt INA226 (Ohm)", "Valeur de la résistance shunt utilisée pour la calibration INA226.", "Ohm"),
        }
        hit = mapping.get(json_name)
        if hit:
            out = {"label": hit[0], "help": hit[1], "unit": hit[2]}
            if json_name == "address":
                out["display_format"] = "hex"
            return out

    if module_name == "elink/client":
        mapping = {
            "enabled": ("Client eLink actif", "Active le client eLink côté Supervisor pour dialoguer avec Flow.IO.", None),
            "sda": ("GPIO SDA eLink", "GPIO utilisé pour la ligne SDA du bus eLink Supervisor -> Flow.IO.", None),
            "scl": ("GPIO SCL eLink", "GPIO utilisé pour la ligne SCL du bus eLink Supervisor -> Flow.IO.", None),
            "freq_hz": ("Fréquence eLink", "Fréquence du bus eLink en hertz.", "Hz"),
            "target_addr": ("Adresse cible Flow.IO", "Adresse I2C du serveur eLink sur Flow.IO.", None),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name == "elink/server":
        mapping = {
            "enabled": ("Serveur eLink actif", "Active le serveur eLink côté Flow.IO.", None),
            "sda": ("GPIO SDA eLink", "GPIO utilisé pour la ligne SDA du bus eLink Flow.IO <-> Supervisor.", None),
            "scl": ("GPIO SCL eLink", "GPIO utilisé pour la ligne SCL du bus eLink Flow.IO <-> Supervisor.", None),
            "freq_hz": ("Fréquence eLink", "Fréquence du bus eLink en hertz.", "Hz"),
            "address": ("Adresse locale Flow.IO", "Adresse I2C locale du serveur eLink sur Flow.IO.", None),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name == "io/debug":
        mapping = {
            "trace_enabled": ("Trace IO active", "Active l'émission periodique des traces IO.", None),
            "trace_period_ms": ("Période trace IO (ms)", "Intervalle entre deux traces debug IO.", "ms"),
        }
        hit = mapping.get(json_name)
        if hit:
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    if module_name.startswith("io/input/a"):
        module_match = ANALOG_MODULE_RE.match(module_name)
        idx = None
        idx_num = None
        field = None
        if module_match:
            idx = module_match.group("idx")
            idx_num = int(idx)
            if json_name == "binding_port":
                field = "binding_port"
            else:
                m = ANALOG_KEY_RE.match(json_name)
                if m and m.group("idx") == idx:
                    field = m.group("field")
        if idx is not None and idx_num is not None and field is not None:
            label_idx = _io_slot_token(idx_num)
            labels = {
                "name": f"Nom entrée A{label_idx}",
                "source": f"Source entrée A{label_idx}",
                "channel": f"Canal entrée A{label_idx}",
                "binding_port": f"Port physique A{label_idx}",
                "c0": f"Coefficient C0 A{label_idx}",
                "c1": f"Offset C1 A{label_idx}",
                "prec": f"Precision A{label_idx}",
                "min": f"Seuil mini A{label_idx}",
                "max": f"Seuil maxi A{label_idx}",
            }
            helps = {
                "name": f"Nom lisible de l'entrée analogique A{label_idx}.",
                "source": f"Source ADC de A{label_idx} (0=ADS interne, 1=ADS externe).",
                "channel": f"Canal ADC utilisé pour A{label_idx}.",
                "binding_port": f"Identifiant du port physique utilise par A{label_idx}. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut.",
                "c0": f"Coefficient multiplicateur de calibration pour A{label_idx}. Il ajuste l'echelle de la mesure brute avant publication. Formule: value = c0 * input + c1.",
                "c1": f"Offset de calibration pour A{label_idx}. Il ajoute un decalage a la mesure convertie pour corriger le zero ou appliquer une translation. Formule: value = c0 * input + c1.",
                "prec": f"Nombre de décimales conservées pour A{label_idx}.",
                "min": f"Valeur minimale valide pour A{label_idx} (avant invalidation).",
                "max": f"Valeur maximale valide pour A{label_idx} (avant invalidation).",
            }
            unit = "digits" if field == "prec" else None
            return {"label": labels[field], "help": helps[field], "unit": unit}

    if module_name.startswith("io/input/i"):
        m = DIGITAL_INPUT_KEY_RE.match(json_name)
        if m:
            idx_num = int(m.group("idx"))
            idx = _io_slot_token(idx_num)
            field = m.group("field")
            labels = {
                "name": f"Nom entrée I{idx}",
                "binding_port": f"Port physique I{idx}",
                "active_high": f"Actif a 1 I{idx}",
                "pull_mode": f"Pull entrée I{idx}",
                "edge_mode": f"Mode de front I{idx}",
                "c0": f"Coefficient C0 I{idx}",
                "prec": f"Precision I{idx}",
            }
            helps = {
                "name": f"Nom lisible de l'entrée digitale I{idx}.",
                "binding_port": f"Identifiant du port physique utilise par I{idx}. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut.",
                "active_high": f"Si active, l'entrée I{idx} est consideree active a l'etat haut.",
                "pull_mode": f"Mode de resistance interne applique a l'entrée I{idx}.",
                "edge_mode": f"Front a compter pour I{idx} en mode compteur (0=descendant, 1=montant, 2=les deux).",
                "c0": f"Coefficient multiplicateur applique au nombre d'impulsions de I{idx} en mode compteur. Il convertit les impulsions brutes en valeur cumulee publiee. Formule: value = c0 * impulsions.",
                "prec": f"Nombre de decimales conservees pour la valeur calculee de I{idx} en mode compteur.",
            }
            unit = "digits" if field == "prec" else None
            return {"label": labels[field], "help": helps[field], "unit": unit}

    if module_name.startswith("io/output/d"):
        m = DIGITAL_OUTPUT_KEY_RE.match(json_name)
        if m:
            idx_num = int(m.group("idx"))
            idx = _io_slot_token(idx_num)
            field = m.group("field")
            labels = {
                "name": f"Nom sortie D{idx}",
                "pin": f"GPIO sortie D{idx}",
                "binding_port": f"Port physique D{idx}",
                "active_high": f"Actif a 1 D{idx}",
                "initial_on": f"État initial ON D{idx}",
                "momentary": f"Mode impulsion D{idx}",
                "pulse_ms": f"Durée impulsion D{idx} (ms)",
            }
            helps = {
                "name": f"Nom lisible de la sortie digitale D{idx}.",
                "pin": f"GPIO pilotée par la sortie D{idx}.",
                "binding_port": f"Identifiant du port physique pilote par D{idx}. La valeur reference un binding compile-time autorise, pas un numero de GPIO brut.",
                "active_high": f"Si vrai, D{idx} est active à l'état haut; sinon active-bas.",
                "initial_on": f"État appliqué au démarrage pour la sortie D{idx}.",
                "momentary": f"Active un fonctionnement impulsionnel pour D{idx}.",
                "pulse_ms": f"Durée de l'impulsion automatique de D{idx}.",
            }
            unit = "ms" if field == "pulse_ms" else None
            return {"label": labels[field], "help": helps[field], "unit": unit}

    if module_name == "poollogic":
        mapping = {
            "enabled": ("PoolLogic actif", "Active ou désactive les automatismes PoolLogic."),
            "auto_mode": ("Mode auto global", "Active le pilotage automatique de la logique piscine."),
            "winter_mode": ("Mode hiver force", "Force le fonctionnement en mode hiver (anti-gel)."),
            "ph_auto_mode": ("Mode auto pH", "Active la régulation automatique du pH."),
            "orp_auto_mode": ("Mode auto ORP", "Active la régulation automatique ORP/chlore."),
            "elec_mode": ("Electrolyse active", "Autorise l'usage de l'electrolyseur."),
            "elec_run": ("Electrolyse en service", "Autorise la commande de marche de l'electrolyseur."),
            "wat_temp_lo_th": ("Seuil eau basse (C)", "Seuil bas de température d'eau pour la logique de filtration.", "C"),
            "wat_temp_setpt": ("Consigne eau (C)", "Consigne de température d'eau pour le calcul de filtration.", "C"),
            "filtr_start_min": ("Début filtration min", "Heure minimale autorisée pour démarrer la filtration."),
            "filtr_stop_max": ("Arrêt filtration max", "Heure maximale autorisée pour arrêter la filtration."),
            "filtr_start_clc": ("Début filtration calculé", "Heure de début de filtration calculée automatiquement."),
            "filtr_stop_clc": ("Arrêt filtration calculé", "Heure de fin de filtration calculée automatiquement."),
            "ph_io_id": ("IO capteur pH", "Identifiant IO de la mesure pH."),
            "orp_io_id": ("IO capteur ORP", "Identifiant IO de la mesure ORP."),
            "psi_io_id": ("IO pression", "Identifiant IO du capteur de pression."),
            "wat_temp_io_id": ("IO température eau", "Identifiant IO de la sonde température eau."),
            "air_temp_io_id": ("IO température air", "Identifiant IO de la sonde température air."),
            "pool_lvl_io_id": ("IO niveau bassin", "Identifiant IO de la mesure de niveau bassin."),
            "psi_low_th": ("Seuil pression basse", "Seuil de pression basse pour detection d'anomalie.", "bar"),
            "psi_high_th": ("Seuil pression haute", "Seuil de pression haute pour detection d'anomalie.", "bar"),
            "winter_start_t": ("Seuil entrée hiver (C)", "Temperature d'eau/air pour basculer en logique hiver.", "C"),
            "freeze_hold_t": ("Seuil maintien hors gel (C)", "Temperature de maintien pour la protection hors gel.", "C"),
            "secure_elec_t": ("Seuil sécurité electrolyse (C)", "Temperature minimale autorisée pour l'electrolyse.", "C"),
            "ph_setpoint": ("Consigne pH", "Valeur cible de pH pour la régulation."),
            "orp_setpoint": ("Consigne ORP", "Valeur cible ORP pour la régulation chlore.", "mV"),
            "ph_kp": ("pH Kp", "Gain proportionnel du régulateur pH."),
            "ph_ki": ("pH Ki", "Gain intégral du régulateur pH."),
            "ph_kd": ("pH Kd", "Gain dérivé du régulateur pH."),
            "orp_kp": ("ORP Kp", "Gain proportionnel du régulateur ORP."),
            "orp_ki": ("ORP Ki", "Gain intégral du régulateur ORP."),
            "orp_kd": ("ORP Kd", "Gain dérivé du régulateur ORP."),
            "ph_window_ms": ("Fenêtre pH (ms)", "Fenêtre temporelle PWM appliquée a la pompe pH.", "ms"),
            "orp_window_ms": ("Fenêtre ORP (ms)", "Fenêtre temporelle PWM appliquée a la pompe ORP.", "ms"),
            "pid_min_on_ms": ("Temps min ON PID (ms)", "Durée minimale ON appliquée aux sorties PID.", "ms"),
            "pid_sample_ms": ("Période échantillonnage PID (ms)", "Intervalle de calcul des régulateurs PID.", "ms"),
            "psi_start_dly_s": ("Délai démarrage pression (s)", "Temps d'attente avant vérification pression après démarrage.", "s"),
            "dly_pid_min": ("Délai PID après filtration (min)", "Délai avant activation des PID après début filtration.", "min"),
            "dly_electro_min": ("Délai electrolyse (min)", "Délai avant autorisation de l'electrolyse après filtration.", "min"),
            "robot_delay_min": ("Délai robot (min)", "Délai avant lancement automatique du robot.", "min"),
            "robot_dur_min": ("Durée robot (min)", "Durée de fonctionnement du robot.", "min"),
            "fill_min_on_s": ("Temps mini remplissage (s)", "Durée minimale de marche de la pompe de remplissage.", "s"),
            "filtr_slot": ("Slot filtration", "Numéro de slot PDM pilote pour la filtration."),
            "swg_slot": ("Slot electrolyse", "Numéro de slot PDM associé à l'electrolyseur."),
            "robot_slot": ("Slot robot", "Numéro de slot PDM associé au robot."),
            "fill_slot": ("Slot remplissage", "Numéro de slot PDM associé au remplissage."),
        }
        hit = mapping.get(json_name)
        if hit:
            if len(hit) == 2:
                return {"label": hit[0], "help": hit[1], "unit": None}
            return {"label": hit[0], "help": hit[1], "unit": hit[2]}

    return None


def _line_starts(text: str) -> List[int]:
    starts = [0]
    for i, c in enumerate(text):
        if c == "\n":
            starts.append(i + 1)
    return starts


def _index_to_line(starts: List[int], idx: int) -> int:
    # 1-based line number
    return bisect.bisect_right(starts, idx)


def _parse_cfgdoc_payload(payload: str) -> Optional[dict]:
    try:
        data = json.loads(payload)
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    out = {}
    label = data.get("label")
    help_txt = data.get("help")
    unit = data.get("unit")
    display_format = data.get("display_format")
    if isinstance(label, str) and label.strip():
        out["label"] = label.strip()
    if isinstance(help_txt, str) and help_txt.strip():
        out["help"] = help_txt.strip()
    if isinstance(unit, str) and unit.strip():
        out["unit"] = unit.strip()
    if isinstance(display_format, str) and display_format.strip():
        out["display_format"] = display_format.strip()
    return out


def _cfgdoc_before_decl(text: str, starts: List[int], start_idx: int) -> Optional[dict]:
    lines = text.splitlines()
    line = _index_to_line(starts, start_idx)
    i = line - 2  # previous line, 0-based index
    while i >= 0:
        s = lines[i].strip()
        if s == "":
            i -= 1
            continue
        m = CFGDOC_RE.match(lines[i])
        if not m:
            return None
        return _parse_cfgdoc_payload(m.group(1))
    return None


def _load_overrides(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise RuntimeError(f"Invalid overrides JSON: {path}: {exc}") from exc

    if not isinstance(data, dict):
        raise RuntimeError(f"Overrides root must be object: {path}")

    normalized = {}
    for key, val in data.items():
        if not isinstance(key, str) or not isinstance(val, dict):
            continue
        item = {}
        if isinstance(val.get("type"), str) and val["type"].strip():
            item["type"] = val["type"].strip()
        if isinstance(val.get("label"), str) and val["label"].strip():
            item["label"] = val["label"].strip()
        if isinstance(val.get("help"), str) and val["help"].strip():
            item["help"] = val["help"].strip()
        if isinstance(val.get("unit"), str) and val["unit"].strip():
            item["unit"] = val["unit"].strip()
        if isinstance(val.get("display_format"), str) and val["display_format"].strip():
            item["display_format"] = val["display_format"].strip()
        normalized[key.strip("/")] = item
    return normalized


def _upsert_entry(entries: Dict[str, dict], key: str, item: dict, priority: int) -> None:
    key = key.strip("/")
    if not key:
        return
    existing = entries.get(key)
    if existing is not None and existing.get("__prio", 0) > priority:
        return
    item["__prio"] = priority
    entries[key] = item


def _scan_declarations(src_root: Path) -> Tuple[List[dict], List[dict]]:
    decls: List[dict] = []
    missing_cfgdoc: List[dict] = []

    for path in sorted(src_root.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp", ".cxx"):
            continue
        if "Generated" in path.parts:
            continue

        text = path.read_text(encoding="utf-8", errors="ignore")
        starts = _line_starts(text)
        rel = path.relative_to(src_root).as_posix()

        for m in DECL_RE.finditer(text):
            body = m.group("body")
            meta = META_RE.search(body)
            cfgdoc = _cfgdoc_before_decl(text, starts, m.start())
            decl_line = _index_to_line(starts, m.start())
            template = m.group("template").strip()
            type_guess = template.split(",", 1)[0].strip()
            var_name = m.group("var").strip()

            info = {
                "file": rel,
                "line": decl_line,
                "var": var_name,
                "type_guess": type_guess,
                "cfgdoc": cfgdoc,
                "json": None,
                "module": None,
                "type": type_guess,
            }
            if meta:
                info["json"] = meta.group("json").strip()
                info["module"] = meta.group("module").strip().strip("/")
                info["type"] = meta.group("type").strip()
            decls.append(info)

            if cfgdoc is None:
                missing_cfgdoc.append({"file": rel, "line": decl_line, "var": var_name})

    return decls, missing_cfgdoc


DeclKey = Tuple[str, str]  # (file stem, var name)


def _scan_json_assignments(src_root: Path) -> Tuple[Dict[DeclKey, Set[str]], Dict[DeclKey, Set[str]]]:
    var_json: Dict[DeclKey, Set[str]] = {}
    var_module: Dict[DeclKey, Set[str]] = {}
    for path in sorted(src_root.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp", ".cxx"):
            continue
        if "Generated" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        stem = path.stem
        module_consts: Dict[str, str] = {}
        for m in MODULE_CONST_RE.finditer(text):
            sym = m.group("sym").strip()
            val = m.group("val").strip().strip("/")
            if sym and val:
                module_consts[sym] = val

        for m in JSON_ASSIGN_RE.finditer(text):
            var = m.group("var").strip()
            name = m.group("json").strip()
            if not var or not name:
                continue
            var_json.setdefault((stem, var), set()).add(name)

        for m in MODULE_ASSIGN_RE.finditer(text):
            var = m.group("var").strip()
            mod = m.group("module").strip().strip("/")
            if not var or not mod:
                continue
            var_module.setdefault((stem, var), set()).add(mod)

        for m in MODULE_ASSIGN_SYM_RE.finditer(text):
            var = m.group("var").strip()
            sym = m.group("sym").strip()
            mod = module_consts.get(sym, "").strip().strip("/")
            if not var or not mod:
                continue
            var_module.setdefault((stem, var), set()).add(mod)

    return var_json, var_module


def _build_entries(src_root: Path) -> Tuple[Dict[str, dict], dict]:
    entries: Dict[str, dict] = {}
    decls, missing_cfgdoc = _scan_declarations(src_root)
    var_json_assign, var_mod_assign = _scan_json_assignments(src_root)

    decl_with_cfgdoc = 0
    var_cfgdocs: Dict[str, dict] = {}

    # Pass 1: direct declarations with json/module in initializer.
    for d in decls:
        cfgdoc = d["cfgdoc"]
        if cfgdoc is not None:
            decl_with_cfgdoc += 1
            var_cfgdocs[d["var"]] = {
                "label": cfgdoc.get("label"),
                "help": cfgdoc.get("help"),
                "unit": cfgdoc.get("unit"),
                "display_format": cfgdoc.get("display_format"),
                "type": d["type"],
            }

        decl_key: DeclKey = (Path(d["file"]).stem, d["var"])
        module_name = d["module"]
        json_name = d["json"]
        type_name = d["type"]
        if not module_name or not json_name:
            continue

        auto_doc = _auto_doc_hint(module_name, json_name) if not cfgdoc else None
        label = cfgdoc.get("label") if cfgdoc else (auto_doc.get("label") if auto_doc else _humanize_json_name(json_name))
        help_txt = cfgdoc.get("help") if cfgdoc else (auto_doc.get("help") if auto_doc else _default_help(module_name, json_name, type_name))
        item = {
            "module": module_name,
            "name": json_name,
            "type": type_name,
            "label": label,
            "help": help_txt,
            "var": d["var"],
            "source": "cfgdoc" if cfgdoc else ("auto-hint" if auto_doc else "auto"),
        }
        if cfgdoc and cfgdoc.get("unit"):
            item["unit"] = cfgdoc["unit"]
        elif auto_doc and auto_doc.get("unit"):
            item["unit"] = auto_doc["unit"]
        if cfgdoc and cfgdoc.get("display_format"):
            item["display_format"] = cfgdoc["display_format"]
        elif auto_doc and auto_doc.get("display_format"):
            item["display_format"] = auto_doc["display_format"]
        _apply_doc_extras(item, module_name, json_name)
        prio = 30 if cfgdoc else 20
        _upsert_entry(entries, f"{module_name}/{json_name}", item, prio)

        # Some modules reassign moduleName at runtime (e.g. poollogic/* branches).
        # Emit those alias keys too so UI lookups on actual branches find docs.
        for reassigned_module in sorted(var_mod_assign.get(decl_key, set())):
            alias_item = dict(item)
            alias_item["module"] = reassigned_module
            if cfgdoc:
                alias_item["source"] = "cfgdoc-alias"
            elif auto_doc:
                alias_item["source"] = "auto-hint-alias"
            else:
                alias_item["source"] = "auto-alias"
            _apply_doc_extras(alias_item, reassigned_module, json_name)
            _upsert_entry(entries, f"{reassigned_module}/{json_name}", alias_item, prio + 1)

    # Pass 2: dynamic series declarations with CFGDOC.
    for d in decls:
        cfgdoc = d["cfgdoc"]
        if cfgdoc is None:
            continue
        if d["json"] and d["module"]:
            continue
        var_name = d["var"]
        decl_key: DeclKey = (Path(d["file"]).stem, var_name)
        fields = sorted(var_json_assign.get(decl_key, set()))
        if not fields:
            label = cfgdoc.get("label") or var_name
            help_txt = cfgdoc.get("help") or _default_series_help(var_name, var_name, d["type"])
            item = {
                "module": "@var",
                "name": var_name,
                "type": d["type"],
                "label": label,
                "help": help_txt,
                "var": var_name,
                "source": "cfgdoc-var",
            }
            if cfgdoc.get("unit"):
                item["unit"] = cfgdoc["unit"]
            if cfgdoc.get("display_format"):
                item["display_format"] = cfgdoc["display_format"]
            _upsert_entry(entries, f"@var/{var_name}", item, 12)
            continue
        modules = sorted(var_mod_assign.get(decl_key, set()))
        for field in fields:
            label = cfgdoc.get("label") or var_name
            help_txt = cfgdoc.get("help") or _default_series_help(var_name, field, d["type"])
            base_item = {
                "module": "*",
                "name": field,
                "type": d["type"],
                "label": label,
                "help": help_txt,
                "var": var_name,
                "source": "cfgdoc-series",
            }
            if cfgdoc.get("unit"):
                base_item["unit"] = cfgdoc["unit"]
            if cfgdoc.get("display_format"):
                base_item["display_format"] = cfgdoc["display_format"]
            _apply_doc_extras(base_item, "*", field)

            # If module literal was assigned, prefer full key docs.
            if modules:
                for mod in modules:
                    full_item = dict(base_item)
                    full_item["module"] = mod
                    _apply_doc_extras(full_item, mod, field)
                    _upsert_entry(entries, f"{mod}/{field}", full_item, 25)
            # Always add wildcard fallback for UI lookup by key only.
            _upsert_entry(entries, f"*/{field}", dict(base_item), 15)

    # Pass 3: synthesize analog input docs for macro-generated and heap-backed slots.
    analog_fields = (
        ("a{idx}_name", "CharArray"),
        ("binding_port", "UInt16"),
        ("a{idx}_c0", "Float"),
        ("a{idx}_c1", "Float"),
        ("a{idx}_prec", "Int32"),
    )
    for idx in range(15):
        slot = _io_slot_token(idx)
        module_name = f"io/input/a{slot}"
        for json_tpl, type_name in analog_fields:
            json_name = json_tpl.format(idx=slot)
            auto_doc = _auto_doc_hint(module_name, json_name)
            item = {
                "module": module_name,
                "name": json_name,
                "type": type_name,
                "label": auto_doc.get("label") if auto_doc else _humanize_json_name(json_name),
                "help": auto_doc.get("help") if auto_doc else _default_help(module_name, json_name, type_name),
                "var": f"a{slot}",
                "source": "synthetic-analog",
            }
            if auto_doc and auto_doc.get("unit"):
                item["unit"] = auto_doc["unit"]
            if auto_doc and auto_doc.get("display_format"):
                item["display_format"] = auto_doc["display_format"]
            _apply_doc_extras(item, module_name, json_name)
            _upsert_entry(entries, f"{module_name}/{json_name}", item, 10)

    stats = {
        "decl_total": len(decls),
        "decl_with_cfgdoc": decl_with_cfgdoc,
        "missing_cfgdoc_count": len(missing_cfgdoc),
        "missing_cfgdoc_examples": missing_cfgdoc[:20],
    }
    return entries, stats


def _merge_overrides(entries: Dict[str, dict], overrides: dict) -> int:
    applied = 0
    for key, val in overrides.items():
        key = key.strip("/")
        base = entries.get(key)
        if base is None:
            if "/" not in key:
                continue
            module, name = key.rsplit("/", 1)
            base = {
                "module": module,
                "name": name,
                "source": "override",
                "__prio": 0,
            }
            type_hint = val.get("type")
            display_format = val.get("display_format")
            if isinstance(type_hint, str) and type_hint.strip():
                base["type"] = type_hint.strip()
            if isinstance(display_format, str) and display_format.strip():
                base["display_format"] = display_format.strip()
            entries[key] = base
        if str(base.get("source", "")).startswith("cfgdoc"):
            continue
        label = val.get("label")
        help_txt = val.get("help")
        type_hint = val.get("type")
        unit = val.get("unit")
        display_format = val.get("display_format")
        if isinstance(label, str) and label.strip():
            base["label"] = label.strip()
        if isinstance(help_txt, str) and help_txt.strip():
            base["help"] = help_txt.strip()
        if isinstance(type_hint, str) and type_hint.strip():
            base["type"] = type_hint.strip()
        if isinstance(unit, str) and unit.strip():
            base["unit"] = unit.strip()
        if isinstance(display_format, str) and display_format.strip():
            base["display_format"] = display_format.strip()
        base["source"] = "override"
        base["__prio"] = max(base.get("__prio", 0), 40)
        applied += 1
    return applied


def _strip_internal(entries: Dict[str, dict]) -> Dict[str, dict]:
    out = {}
    for k, v in entries.items():
        item = dict(v)
        item.pop("__prio", None)
        out[k] = item
    return out


def _write_output(
    path: Path,
    entries: Dict[str, dict],
    stats: dict,
    overrides_total: int,
    overrides_applied: int,
    enum_sets: Dict[str, List[dict]],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    docs = dict(sorted(_strip_internal(entries).items(), key=lambda kv: kv[0]))
    payload = {
        "_meta": {
            "generated": True,
            "total": len(docs),
            "decl_total": stats["decl_total"],
            "decl_with_cfgdoc": stats["decl_with_cfgdoc"],
            "missing_cfgdoc_count": stats["missing_cfgdoc_count"],
            "manual_overrides_total": overrides_total,
            "manual_overrides_applied": overrides_applied,
        },
        "meta": {
            "enum_sets": enum_sets,
        },
        "docs": docs,
    }
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    project_dir = _get_project_dir()
    src_root = project_dir / "src"
    overrides_path = project_dir / "scripts" / "config_docs.fr.overrides.json"
    # Keep SPIFFS object path short enough for ESP32 SPIFFS limits.
    out_path = project_dir / "data" / "webinterface" / "cfgdocs.fr.json"
    legacy_path = project_dir / "data" / "webinterface" / "config-docs.fr.json"
    strict = os.getenv("FLOW_CFGDOC_STRICT", "0").strip().lower() in ("1", "true", "yes", "on")

    entries, stats = _build_entries(src_root)
    enum_sets = _build_flowio_binding_enum_sets(src_root)
    overrides = _load_overrides(overrides_path)
    applied = _merge_overrides(entries, overrides)
    _write_output(out_path, entries, stats, len(overrides), applied, enum_sets)
    if legacy_path.exists():
        legacy_path.unlink()
        print(f"[generate_config_docs] removed legacy file {legacy_path}")

    if strict and stats["missing_cfgdoc_count"] > 0:
        examples = ", ".join(
            [f"{e['file']}:{e['line']}({e['var']})" for e in stats["missing_cfgdoc_examples"][:8]]
        )
        raise RuntimeError(
            f"CFGDOC strict mode failed: missing comments for {stats['missing_cfgdoc_count']} ConfigVariable "
            f"(examples: {examples})"
        )

    print(
        f"[generate_config_docs] wrote {out_path} "
        f"(entries={len(entries)} cfgdoc={stats['decl_with_cfgdoc']}/{stats['decl_total']} "
        f"manual={applied}/{len(overrides)})"
    )


main()
