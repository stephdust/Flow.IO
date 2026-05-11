const $ = (id) => document.getElementById(id);

const ui = {
  product: "Flow.io",
  runtime: true,
  ap: true,
  mqtt: true,
  cfg: true,
  full: true,
  rebootAfterWifi: false
};

function status(text) {
  $("status").textContent = text;
}

async function json(url, options) {
  const res = await fetch(url, options);
  const data = await res.json().catch(() => null);
  if (!res.ok || !data || data.ok === false) throw new Error(url);
  return data;
}

function form(values) {
  return new URLSearchParams(values);
}

function applyBrand(meta) {
  const profile = String((meta && (meta.profile || meta.profile_name)) || "").toLowerCase();
  const product = String((meta && meta.product_name) || "");
  const isDisplay = product === "Flow Connect Display" || profile.indexOf("flowconnectdisplay") >= 0 || profile.indexOf("flow_connect_display") >= 0;
  const name = isDisplay ? "Flow Connect Display" : profile === "micronova" ? "Pellet" : "Flow";
  const suffix = document.querySelector(".brand-io");
  $("brandName").textContent = name;
  if (suffix) suffix.textContent = isDisplay ? "" : ".io";
  $("brand").setAttribute("aria-label", isDisplay ? name : name + ".io");
  document.title = isDisplay ? name : name + ".io";
}

function applyCapabilities(meta) {
  const wifiOnly = !!(meta && meta.wifi_only);
  ui.product = String((meta && meta.product_name) || ui.product);
  ui.runtime = !wifiOnly && meta && meta.runtime_enabled !== false;
  ui.ap = !meta || meta.ap_status_enabled !== false;
  ui.mqtt = !wifiOnly && (!meta || meta.mqtt_config_enabled !== false);
  ui.cfg = !wifiOnly && (!meta || meta.config_browser_enabled !== false);
  ui.full = !wifiOnly && (!meta || meta.full_ui_enabled !== false);
  ui.rebootAfterWifi = !!(meta && meta.reboot_after_wifi_save);

  $("runtimeBox").classList.toggle("hidden", !ui.runtime);
  $("apBox").classList.toggle("hidden", !ui.ap);
  $("mqttBox").classList.toggle("hidden", true);
  $("cfgBox").classList.toggle("hidden", !ui.cfg);
  $("fullUiBtn").classList.toggle("hidden", !ui.full);
}

let scanTimer = 0;

function scanStatus(text) {
  $("wifiScanStatus").textContent = text;
}

function renderWifiScan(data) {
  const nets = data && Array.isArray(data.networks) ? data.networks : [];
  const list = $("wifiSsidList");
  const current = ($("wifiSsid").value || "").trim();
  list.innerHTML = "";
  const manual = document.createElement("option");
  manual.value = "";
  manual.textContent = "Saisie manuelle";
  list.appendChild(manual);
  for (const net of nets) {
    if (!net || !net.ssid || net.hidden) continue;
    const opt = document.createElement("option");
    const secure = net.secure ? " (securise)" : " (ouvert)";
    const rssi = Number.isFinite(net.rssi) ? " " + net.rssi + " dBm" : "";
    opt.value = net.ssid;
    opt.textContent = (net.ssid + secure + rssi).slice(0, 72);
    list.appendChild(opt);
  }
  if (current) {
    for (const opt of list.options) {
      if (opt.value === current) {
        list.value = current;
        break;
      }
    }
  }
}

function updateScanStatus(data) {
  if (!data || data.ok !== true) {
    scanStatus("Scan Wi-Fi indisponible.");
    return;
  }
  const running = !!data.running || !!data.requested;
  const count = Number.isFinite(data.count) ? data.count : 0;
  const total = Number.isFinite(data.total_found) ? data.total_found : count;
  if (running) {
    scanStatus("Scan Wi-Fi en cours...");
    return;
  }
  scanStatus(count > 0
    ? "Scan Wi-Fi terminé : " + count + " réseaux affichés (" + total + " détectés)."
    : "Aucun réseau visible détecté.");
}

async function refreshWifiScan(trigger) {
  try {
    clearTimeout(scanTimer);
    if (trigger) {
      await json("/api/wifi/scan", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: form({ force: "1" })
      });
    }
    const data = await json("/api/wifi/scan", { cache: "no-store" });
    renderWifiScan(data);
    updateScanStatus(data);
    if (data.running || data.requested) scanTimer = setTimeout(() => refreshWifiScan(false), 1200);
  } catch (err) {
    scanStatus("Erreur scan Wi-Fi: " + err.message);
  }
}

async function loadRuntime() {
  try {
    const ids = "2901,2903,2904,2905,2906,2907,2908,2909,2912,2914,2001,1703,1704,1002,1003";
    const data = await json("/api/runtime/values?ids=" + encodeURIComponent(ids));
    const lines = [];
    (data.values || []).forEach((value) => {
      const key = (value.key || String(value.id))
        .replace(/^micronova\./, "")
        .replace(/^mqtt\./, "MQTT ")
        .replace(/^system\./, "system ")
        .replace(/^wifi\./, "WiFi ");
      let display = value.status || value.value;
      if (value.unit && display !== undefined) display += " " + value.unit;
      lines.push(key + ": " + display);
    });
    $("runtimeOut").textContent = lines.join("\n") || "-";
  } catch (err) {
    $("runtimeOut").textContent = "Runtime indisponible: " + err.message;
  }
}

async function loadApStatus() {
  try {
    const data = await json("/api/wifi/ap");
    const lines = [
      "mode: " + (data.mode || "-"),
      "actif: " + (data.active ? "oui" : "non"),
      "ssid: " + (data.ssid || "-"),
      "mot de passe: " + (data.pass || "-"),
      "ip: " + (data.ip || "-"),
      "clients: " + (Number.isFinite(data.clients) ? data.clients : 0)
    ];
    $("apOut").textContent = lines.join("\n");
  } catch (err) {
    $("apOut").textContent = "Etat AP indisponible: " + err.message;
  }
}

async function loadAll() {
  let ok = false;
  try {
    const meta = await json("/api/web/meta");
    applyBrand(meta);
    applyCapabilities(meta);
    $("subtitle").textContent = "Configuration Initiale - " + (meta.firmware_version || "Flow.io");
    status("Pret");
    ok = true;
  } catch (err) {
    status("Meta indisponible");
  }

  try {
    const wifi = await json("/api/wifi/config");
    $("wifiEnabled").checked = !!wifi.enabled;
    $("wifiSsid").value = wifi.ssid || "";
    $("wifiPass").value = wifi.pass || "";
    ok = true;
  } catch (err) {
    status("Erreur Wi-Fi: " + err.message);
  }

  if (ui.mqtt) try {
    const cfg = await json("/api/mqtt/config");
    $("mqttBox").classList.remove("hidden");
    $("mqttEnabled").checked = !!cfg.enabled;
    $("mqttHost").value = cfg.host || "";
    $("mqttPort").value = cfg.port || 1883;
    $("mqttUser").value = cfg.user || "";
    $("mqttPass").value = cfg.pass || "";
    $("mqttBaseTopic").value = cfg.baseTopic || "flowio";
    $("mqttTopicDeviceId").value = cfg.topicDeviceId || "";
  } catch (err) {
  }

  if (ui.runtime) await loadRuntime();
  if (ui.ap) await loadApStatus();
  if (ok) $("cfgOut").textContent = "Lecture a la demande.";
}

async function saveWifi() {
  try {
    const data = await json("/api/wifi/config", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: form({
        enabled: $("wifiEnabled").checked ? "1" : "0",
        ssid: $("wifiSsid").value,
        pass: $("wifiPass").value
      })
    });
    status((ui.rebootAfterWifi || data.reboot_scheduled)
      ? "Wi-Fi enregistre. Redemarrage en cours..."
      : "Wi-Fi enregistre. Le portail peut se couper si la connexion station reussit.");
  } catch (err) {
    status("Erreur Wi-Fi: " + err.message);
  }
}

async function saveMqtt() {
  try {
    const patch = {
      mqtt: {
        enabled: $("mqttEnabled").checked,
        host: $("mqttHost").value,
        port: parseInt($("mqttPort").value || "1883", 10),
        user: $("mqttUser").value,
        pass: $("mqttPass").value,
        baseTopic: $("mqttBaseTopic").value,
        topicDeviceId: $("mqttTopicDeviceId").value
      }
    };
    await json("/api/mqtt/config", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: form({
        enabled: patch.mqtt.enabled ? "1" : "0",
        host: patch.mqtt.host,
        port: String(patch.mqtt.port),
        user: patch.mqtt.user,
        pass: patch.mqtt.pass,
        baseTopic: patch.mqtt.baseTopic,
        topicDeviceId: patch.mqtt.topicDeviceId
      })
    });
    status("MQTT enregistre.");
  } catch (err) {
    status("Erreur MQTT: " + err.message);
  }
}

async function loadModule() {
  try {
    const name = $("cfgModule").value || "micronova";
    const data = await json("/api/supervisorcfg/module?name=" + encodeURIComponent(name));
    $("cfgOut").textContent = JSON.stringify(data.data, null, 2);
  } catch (err) {
    $("cfgOut").textContent = "Erreur: " + err.message;
  }
}

async function reboot() {
  try {
    await fetch("/api/system/reboot", { method: "POST" });
    status("Redemarrage demande.");
  } catch (err) {
    status("Erreur reboot: " + err.message);
  }
}

$("wifiScanBtn").addEventListener("click", () => refreshWifiScan(true));
$("wifiSsidList").addEventListener("change", () => {
  const value = $("wifiSsidList").value;
  if (value) $("wifiSsid").value = value;
});
$("runtimeRefresh").addEventListener("click", () => loadRuntime());
$("apRefresh").addEventListener("click", () => loadApStatus());
$("wifiSave").addEventListener("click", () => saveWifi());
$("mqttSave").addEventListener("click", () => saveMqtt());
$("cfgLoad").addEventListener("click", () => loadModule());
$("rebootBtn").addEventListener("click", () => reboot());
$("fullUiBtn").addEventListener("click", () => {
  location.href = "/webinterface?full=1&page=page-wifi";
});

loadAll();
