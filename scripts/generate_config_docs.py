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

    combined_meta = _resolve_meta_i18n(_merge_meta_dict(cfgdocs_meta, cfgmods_meta), i18n)

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
        f"text_files={len(cfgdocs_files) + len(cfgmods_files)} i18n_files={len(i18n_files)})"
    )


if __name__ == "__main__":
    main()
