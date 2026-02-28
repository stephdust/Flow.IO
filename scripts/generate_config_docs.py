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

ANALOG_KEY_RE = re.compile(r"^a(?P<idx>[0-9]+)_(?P<field>name|source|channel|c0|c1|prec|min|max)$")
DIGITAL_KEY_RE = re.compile(r"^d(?P<idx>[0-9]+)_(?P<field>name|pin|active_high|initial_on|momentary|pulse_ms)$")


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


def _auto_doc_hint(module_name: str, json_name: str) -> Optional[dict]:
    module_name = module_name.strip("/")
    json_name = json_name.strip()

    if module_name == "io":
        mapping = {
            "enabled": ("Module IO actif", "Active ou désactive la couche IO (entrées/sorties).", None),
            "i2c_sda": ("Broche I2C SDA", "Broche SDA du bus I2C utilisé par le module IO.", None),
            "i2c_scl": ("Broche I2C SCL", "Broche SCL du bus I2C utilisé par le module IO.", None),
            "ads_poll_ms": ("Période lecture ADS (ms)", "Intervalle entre deux acquisitions des entrées analogiques ADS.", "ms"),
            "ds_poll_ms": ("Période lecture DS18B20 (ms)", "Intervalle entre deux lectures des sondes DS18B20.", "ms"),
            "digital_poll_ms": ("Période lecture digitales (ms)", "Intervalle de rafraîchissement des entrées digitales.", "ms"),
            "ads_int_addr": ("Adresse ADS interne", "Adresse I2C du convertisseur ADS interne.", None),
            "ads_ext_addr": ("Adresse ADS externe", "Adresse I2C du convertisseur ADS externe.", None),
            "ads_gain": ("Gain ADS", "Gain de conversion ADS (pleine échelle).", None),
            "ads_rate": ("Fréquence ADS", "Fréquence d'échantillonnage ADS.", None),
            "pcf_enabled": ("PCF actif", "Active l'extension PCF857x pour E/S supplémentaires.", None),
            "pcf_address": ("Adresse PCF", "Adresse I2C du composant PCF857x.", None),
            "pcf_mask_def": ("Masque par défaut PCF", "État par défaut du masque de sorties PCF.", None),
            "pcf_active_low": ("PCF actif à 0", "Force une logique active-bas sur les sorties PCF.", None),
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
        m = ANALOG_KEY_RE.match(json_name)
        if m:
            idx = m.group("idx")
            field = m.group("field")
            labels = {
                "name": f"Nom entrée A{idx}",
                "source": f"Source entrée A{idx}",
                "channel": f"Canal entrée A{idx}",
                "c0": f"Coefficient C0 A{idx}",
                "c1": f"Coefficient C1 A{idx}",
                "prec": f"Precision A{idx}",
                "min": f"Seuil mini A{idx}",
                "max": f"Seuil maxi A{idx}",
            }
            helps = {
                "name": f"Nom lisible de l'entrée analogique A{idx}.",
                "source": f"Source ADC de A{idx} (0=ADS interne, 1=ADS externe).",
                "channel": f"Canal ADC utilisé pour A{idx}.",
                "c0": f"Coefficient C0 pour la calibration de A{idx}.",
                "c1": f"Coefficient C1 pour la calibration de A{idx}.",
                "prec": f"Nombre de décimales conservées pour A{idx}.",
                "min": f"Valeur minimale valide pour A{idx} (avant invalidation).",
                "max": f"Valeur maximale valide pour A{idx} (avant invalidation).",
            }
            unit = "digits" if field == "prec" else None
            return {"label": labels[field], "help": helps[field], "unit": unit}

    if module_name.startswith("io/output/d"):
        m = DIGITAL_KEY_RE.match(json_name)
        if m:
            idx = m.group("idx")
            field = m.group("field")
            labels = {
                "name": f"Nom sortie D{idx}",
                "pin": f"GPIO sortie D{idx}",
                "active_high": f"Actif a 1 D{idx}",
                "initial_on": f"État initial ON D{idx}",
                "momentary": f"Mode impulsion D{idx}",
                "pulse_ms": f"Durée impulsion D{idx} (ms)",
            }
            helps = {
                "name": f"Nom lisible de la sortie digitale D{idx}.",
                "pin": f"GPIO pilotée par la sortie D{idx}.",
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
    if isinstance(label, str) and label.strip():
        out["label"] = label.strip()
    if isinstance(help_txt, str) and help_txt.strip():
        out["help"] = help_txt.strip()
    if isinstance(unit, str) and unit.strip():
        out["unit"] = unit.strip()
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
        if isinstance(val.get("label"), str) and val["label"].strip():
            item["label"] = val["label"].strip()
        if isinstance(val.get("help"), str) and val["help"].strip():
            item["help"] = val["help"].strip()
        if isinstance(val.get("unit"), str) and val["unit"].strip():
            item["unit"] = val["unit"].strip()
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

            # If module literal was assigned, prefer full key docs.
            if modules:
                for mod in modules:
                    full_item = dict(base_item)
                    full_item["module"] = mod
                    _upsert_entry(entries, f"{mod}/{field}", full_item, 25)
            # Always add wildcard fallback for UI lookup by key only.
            _upsert_entry(entries, f"*/{field}", dict(base_item), 15)

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
        if key not in entries:
            continue
        base = entries[key]
        if str(base.get("source", "")).startswith("cfgdoc"):
            continue
        label = val.get("label")
        help_txt = val.get("help")
        unit = val.get("unit")
        if isinstance(label, str) and label.strip():
            base["label"] = label.strip()
        if isinstance(help_txt, str) and help_txt.strip():
            base["help"] = help_txt.strip()
        if isinstance(unit, str) and unit.strip():
            base["unit"] = unit.strip()
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


def _write_output(path: Path, entries: Dict[str, dict], stats: dict, overrides_total: int, overrides_applied: int) -> None:
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
    overrides = _load_overrides(overrides_path)
    applied = _merge_overrides(entries, overrides)
    _write_output(out_path, entries, stats, len(overrides), applied)
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
