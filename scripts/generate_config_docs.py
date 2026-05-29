#!/usr/bin/env python3
"""Generate cfgdocs/cfgmods payloads from module text manifests."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

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


def _merge_meta_dict(base: dict, overlay: dict) -> dict:
    out = dict(base or {})
    if not isinstance(overlay, dict):
        return out
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = _merge_meta_dict(out[key], value)
            continue
        if isinstance(value, list) and isinstance(out.get(key), list):
            merged: List[Any] = []
            seen = set()
            for source in (out.get(key, []), value):
                for item in source:
                    token = json.dumps(item, ensure_ascii=False, sort_keys=True)
                    if token in seen:
                        continue
                    seen.add(token)
                    merged.append(item)
            out[key] = merged
            continue
        out[key] = value
    return out


def _load_text_payload(path: Path) -> Optional[dict]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"[generate_config_docs] warning: invalid JSON in {path}: {exc}")
        return None
    if not isinstance(data, dict):
        print(f"[generate_config_docs] warning: ignored non-object payload in {path}")
        return None
    return data


def _text_manifest_files(src_root: Path, stem: str, locale: str = "fr") -> List[Path]:
    modules_root = src_root / "Modules"
    if not modules_root.exists():
        return []
    candidates = sorted(modules_root.rglob(f"text/{stem}*.json"))
    by_dir: Dict[Path, List[Path]] = {}
    for path in candidates:
        by_dir.setdefault(path.parent, []).append(path)

    selected: List[Path] = []
    target_name = f"{stem}.{locale}.json"
    for _, paths in sorted(by_dir.items(), key=lambda item: str(item[0])):
        picks = {path.name: path for path in paths}
        chosen = (
            picks.get(target_name)
            or picks.get(f"{stem}.json")
            or picks.get(f"{stem}.fr.json")
            or (sorted(paths)[0] if paths else None)
        )
        if chosen:
            selected.append(chosen)
    return selected


def _load_text_docs(src_root: Path, stem: str, locale: str = "fr") -> Tuple[Dict[str, dict], dict, List[Path]]:
    docs: Dict[str, dict] = {}
    meta: dict = {}
    loaded_files: List[Path] = []
    for path in _text_manifest_files(src_root, stem=stem, locale=locale):
        payload = _load_text_payload(path)
        if payload is None:
            continue
        loaded_files.append(path)
        payload_docs = payload.get("docs")
        if isinstance(payload_docs, dict):
            for raw_key, raw_val in payload_docs.items():
                if not isinstance(raw_key, str) or not isinstance(raw_val, dict):
                    continue
                docs[raw_key.strip("/")] = dict(raw_val)
        payload_meta = payload.get("meta")
        if not isinstance(payload_meta, dict):
            payload_meta = payload.get("_meta")
        if isinstance(payload_meta, dict):
            meta = _merge_meta_dict(meta, payload_meta)
    return docs, meta, loaded_files


def _load_text_translations(src_root: Path, locale: str = "fr") -> Tuple[Dict[str, str], List[Path]]:
    modules_root = src_root / "Modules"
    if not modules_root.exists():
        return {}, []
    out: Dict[str, str] = {}
    loaded_files: List[Path] = []
    for path in sorted(modules_root.rglob(f"text/i18n.{locale}.json")):
        payload = _load_text_payload(path)
        if payload is None:
            continue
        loaded_files.append(path)
        translations = payload.get("translations")
        source = translations if isinstance(translations, dict) else payload
        for raw_key, raw_val in source.items():
            if not isinstance(raw_key, str) or not isinstance(raw_val, str):
                continue
            key = raw_key.strip()
            if key:
                out[key] = raw_val
    return out, loaded_files


def _resolve_doc_i18n_fields(raw_doc: dict, translations: Dict[str, str]) -> dict:
    doc = dict(raw_doc or {})
    label_token = doc.get("label_t")
    help_token = doc.get("help_t")
    if isinstance(label_token, str) and label_token.strip():
        token = label_token.strip()
        doc["label"] = translations.get(token, token)
        doc["label_i18n"] = token
    if isinstance(help_token, str) and help_token.strip():
        token = help_token.strip()
        doc["help"] = translations.get(token, token)
        doc["help_i18n"] = token
    return doc


def _resolve_meta_i18n(node: Any, translations: Dict[str, str]) -> Any:
    if isinstance(node, list):
        return [_resolve_meta_i18n(item, translations) for item in node]
    if not isinstance(node, dict):
        return node
    out = {k: _resolve_meta_i18n(v, translations) for k, v in node.items()}
    label_token = out.get("label_t")
    help_token = out.get("help_t")
    if isinstance(label_token, str) and label_token.strip():
        token = label_token.strip()
        out["label"] = translations.get(token, token)
        out["label_i18n"] = token
    if isinstance(help_token, str) and help_token.strip():
        token = help_token.strip()
        out["help"] = translations.get(token, token)
        out["help_i18n"] = token
    return out


def _detect_pio_env() -> str:
    if env is not None:
        try:
            value = str(env.subst("$PIOENV") or "").strip()
            if value:
                return value
        except Exception:
            pass
    return str(os.getenv("PIOENV", "") or "").strip()


def _profile_from_pio_env(pio_env: str) -> str:
    name = str(pio_env or "").strip().lower()
    if "flowios3" in name or "flowio_s3" in name or "flowio-s3" in name:
        return "flowios3"
    if name.startswith("flowio") or "flowio" in name:
        return "flowio"
    return "generic"


def _profile_override_from_project_options() -> Optional[str]:
    from_env = str(os.getenv("FLOW_CFGDOC_PROFILE", "") or "").strip().lower()
    if from_env in ("flowio", "flowios3", "generic"):
        return from_env

    if env is None:
        return None
    try:
        value = str(env.GetProjectOption("custom_cfgdocs_profile") or "").strip().lower()
    except Exception:
        return None
    if value in ("flowio", "flowios3", "generic"):
        return value
    return None


def _to_int(value: Any) -> Optional[int]:
    try:
        return int(value)
    except Exception:
        return None


def _apply_profile_specific_io_enum_sets(meta: dict, profile: str) -> dict:
    if not isinstance(meta, dict):
        return meta
    enum_sets = meta.get("enum_sets")
    if not isinstance(enum_sets, dict):
        return meta

    def sanitize_enum_entry(entry: dict, label: str) -> dict:
        out = dict(entry or {})
        out["label"] = label
        # Keep this label stable regardless of cfgdoc i18n token overlays.
        out.pop("label_t", None)
        out.pop("label_i18n", None)
        return out

    # Digital input bindings: pin labels differ across FlowIO and FlowIOS3.
    din_key = "flowio_binding_port_digital_input"
    din_entries = enum_sets.get(din_key)
    if isinstance(din_entries, list):
        current = [item for item in din_entries if isinstance(item, dict)]
        din_labels_flowio = {
            200: "PortDigitalIn1 - digital_in1 (FlowIO GPIO34) [200]",
            201: "PortDigitalIn2 - digital_in2 (FlowIO GPIO36) [201]",
            202: "PortDigitalIn3 - digital_in3 (FlowIO GPIO39) [202]",
            203: "PortDigitalIn4 - digital_in4 (FlowIO GPIO35) [203]",
        }
        din_labels_flowios3 = {
            200: "PortDigitalIn1 - digital_in1 (FlowIOS3 GPIO4) [200]",
            201: "PortDigitalIn2 - digital_in2 (FlowIOS3 GPIO5) [201]",
            202: "PortDigitalIn3 - digital_in3 (FlowIOS3 GPIO6) [202]",
            203: "PortDigitalIn4 - digital_in4 (FlowIOS3 GPIO7) [203]",
            204: "PortDigitalIn5 - digital_in5 (FlowIOS3 GPIO8) [204]",
            205: "PortDigitalIn6 - digital_in6 (FlowIOS3 GPIO9) [205]",
            206: "PortDigitalIn7 - digital_in7 (FlowIOS3 GPIO10) [206]",
            207: "PortDigitalIn8 - digital_in8 (FlowIOS3 GPIO11) [207]",
        }

        selected_labels = None
        if profile == "flowio":
            selected_labels = din_labels_flowio
        elif profile == "flowios3":
            selected_labels = din_labels_flowios3

        if selected_labels is not None:
            filtered: List[dict] = []
            for entry in current:
                value = _to_int(entry.get("value"))
                if value is None or value not in selected_labels:
                    continue
                filtered.append(sanitize_enum_entry(entry, selected_labels[value]))
            enum_sets[din_key] = filtered

    # Digital output bindings: FlowIO uses PCF8574 ports, FlowIOS3 uses TCA9554 ports.
    dout_key = "flowio_binding_port_digital_output"
    dout_entries = enum_sets.get(dout_key)
    if isinstance(dout_entries, list):
        current = [item for item in dout_entries if isinstance(item, dict)]
        filtered: List[dict] = []
        for entry in current:
            value = _to_int(entry.get("value"))
            if value is None:
                continue
            keep = True
            # Micronova aux_output must not be exposed on FlowIO / FlowIOS3 profiles.
            if profile in ("flowio", "flowios3") and value == 1:
                keep = False
            if profile == "flowio":
                keep = keep and not (300 <= value <= 399)
            elif profile == "flowios3":
                keep = keep and not (400 <= value <= 499)
            if keep:
                filtered.append(dict(entry))

        if profile == "flowios3":
            dout_labels_flowios3 = {
                300: "EXIO1 - PortOut0 / TCA9554 bit 0 [300]",
                301: "EXIO2 - PortOut1 / TCA9554 bit 1 [301]",
                302: "EXIO3 - PortOut2 / TCA9554 bit 2 [302]",
                303: "EXIO4 - PortOut3 / TCA9554 bit 3 [303]",
                304: "EXIO5 - PortOut4 / TCA9554 bit 4 [304]",
                305: "EXIO6 - PortOut5 / TCA9554 bit 5 [305]",
                306: "EXIO7 - PortOut6 / TCA9554 bit 6 [306]",
                307: "EXIO8 - PortOut7 / TCA9554 bit 7 [307]",
            }
            relabeled: List[dict] = []
            for entry in filtered:
                value = _to_int(entry.get("value"))
                if value is not None and value in dout_labels_flowios3:
                    relabeled.append(sanitize_enum_entry(entry, dout_labels_flowios3[value]))
                else:
                    relabeled.append(dict(entry))
            filtered = relabeled

        # Ensure FlowIO exposes all 8 PCF bits (400..407) in UI bindings.
        if profile == "flowio":
            present_values = {_to_int(item.get("value")) for item in filtered}
            if 407 not in present_values:
                filtered.append(
                    {
                        "value": 407,
                        "label": "PortPCF0Bit7 - Sortie PCF8574 - Bit 7 [407]",
                    }
                )
        enum_sets[dout_key] = filtered

    # PoolLogic device slots: keep generic labels by default, but expose
    # FlowIOS3 wiring-specific mapping in UI for faster setup.
    slot_key = "poollogic_device_slot"
    slot_entries = enum_sets.get(slot_key)
    if profile == "flowios3" and isinstance(slot_entries, list):
        current = [item for item in slot_entries if isinstance(item, dict)]
        slot_labels_flowios3 = {
            0: "Filtration Pump -> d00 (EXIO1 / PortOut0) [0]",
            1: "pH Pump -> d01 (EXIO2 / PortOut1) [1]",
            2: "Chlorine Pump -> d02 (EXIO3 / PortOut2) [2]",
            3: "Robot -> d03 (EXIO4 / PortOut3) [3]",
            4: "Fill Pump -> d04 (EXIO5 / PortOut4) [4]",
            5: "Chlorine Generator -> d05 (EXIO6 / PortOut5) [5]",
            6: "Lights -> d06 (EXIO7 / PortOut6) [6]",
            7: "Water Heater -> d07 (EXIO8 / PortOut7) [7]",
        }
        relabeled: List[dict] = []
        for entry in current:
            value = _to_int(entry.get("value"))
            if value is None:
                continue
            label = slot_labels_flowios3.get(value)
            if label:
                relabeled.append(sanitize_enum_entry(entry, label))
            else:
                relabeled.append(dict(entry))
        enum_sets[slot_key] = relabeled

    return meta


def _resolved_docs(docs: Dict[str, dict], translations: Dict[str, str]) -> Dict[str, dict]:
    return {
        key: _resolve_doc_i18n_fields(value, translations)
        for key, value in docs.items()
        if isinstance(value, dict)
    }


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    project_dir = _get_project_dir()
    src_root = project_dir / "src"
    locale = os.getenv("FLOW_CFGDOC_LOCALE", "fr").strip().lower() or "fr"

    out_path = project_dir / "data" / "webinterface" / "cfgdocs.json"
    cfgmods_out_path = project_dir / "data" / "webinterface" / "cfgmods.json"

    cfgdocs_docs, cfgdocs_meta, cfgdocs_files = _load_text_docs(src_root, stem="cfgdocs", locale=locale)
    cfgmods_docs, cfgmods_meta, cfgmods_files = _load_text_docs(src_root, stem="cfgmods", locale=locale)
    i18n, i18n_files = _load_text_translations(src_root, locale=locale)

    pio_env = _detect_pio_env()
    profile = _profile_override_from_project_options() or _profile_from_pio_env(pio_env)

    combined_meta = _resolve_meta_i18n(_merge_meta_dict(cfgdocs_meta, cfgmods_meta), i18n)
    combined_meta = _apply_profile_specific_io_enum_sets(combined_meta, profile)

    merged_docs = _resolved_docs(dict(cfgdocs_docs), i18n)

    cfgdocs_payload = {
        "_meta": {
            "generated": True,
            "locale": locale,
            "source": "text",
            "total": len(merged_docs),
        },
        "meta": combined_meta if isinstance(combined_meta, dict) else {},
        "docs": dict(sorted(merged_docs.items(), key=lambda kv: kv[0])),
    }
    _write_json(out_path, cfgdocs_payload)

    cfgmods_payload = {
        "_meta": {
            "generated": True,
            "locale": locale,
            "version": 1,
            "source": "text",
        },
        "meta": combined_meta if isinstance(combined_meta, dict) else {},
        "docs": dict(sorted(_resolved_docs(cfgmods_docs, i18n).items(), key=lambda kv: kv[0])),
    }
    _write_json(cfgmods_out_path, cfgmods_payload)

    print(
        f"[generate_config_docs] wrote {out_path} "
        f"(docs={len(cfgdocs_payload['docs'])} cfgmods={len(cfgmods_payload['docs'])} "
        f"text_files={len(cfgdocs_files) + len(cfgmods_files)} i18n_files={len(i18n_files)} "
        f"pio_env={pio_env or '-'} profile={profile})"
    )


if __name__ == "__main__":
    main()
