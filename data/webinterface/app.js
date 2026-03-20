    const drawer = document.getElementById('drawer');
    const overlay = document.getElementById('overlay');
    const menuToggles = Array.from(document.querySelectorAll('[data-menu-toggle]'));
    const menuItems = Array.from(document.querySelectorAll('.menu-item'));
    const pages = Array.from(document.querySelectorAll('.page'));
    const webAssetVersion = (typeof window.__FLOW_WEB_ASSET_VERSION__ === 'string')
      ? window.__FLOW_WEB_ASSET_VERSION__
      : '';
    const supervisorFirmwareVersion = (typeof window.__FLOW_FIRMWARE_VERSION__ === 'string' &&
      window.__FLOW_FIRMWARE_VERSION__.trim())
      ? window.__FLOW_FIRMWARE_VERSION__.trim()
      : '-';

    function versionedWebAssetUrl(path) {
      if (!webAssetVersion) return path;
      return path + '?v=' + encodeURIComponent(webAssetVersion);
    }

    function isMobileLayout() {
      return window.innerWidth <= 900;
    }

    function setMobileDrawerOpen(open) {
      drawer.classList.toggle('mobile-open', open);
      overlay.classList.toggle('visible', open);
    }

    function closeMobileDrawer() {
      if (isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
    }

    function startUpgradeStatusPolling() {
      if (upgradeStatusPollTimer) return;
      upgradeStatusPollTimer = setInterval(() => {
        refreshUpgradeStatus().catch(() => {});
      }, 4000);
    }

    function stopUpgradeStatusPolling() {
      if (!upgradeStatusPollTimer) return;
      clearInterval(upgradeStatusPollTimer);
      upgradeStatusPollTimer = null;
    }

    let flowRemoteFetchQueue = Promise.resolve();

    function fetchFlowRemoteQueued(url, options) {
      const queued = flowRemoteFetchQueue
        .catch(() => {})
        .then(() => fetch(url, options));
      flowRemoteFetchQueue = queued.catch(() => {});
      return queued;
    }

    function showPage(pageId) {
      pages.forEach((el) => el.classList.toggle('active', el.id === pageId));
      menuItems.forEach((el) => el.classList.toggle('active', el.dataset.page === pageId));
      terminalActive = pageId === 'page-terminal';
      if (terminalActive) {
        connectLogSocket();
      } else {
        closeLogSocket();
        setWsStatusText('inactif');
      }
      if (pageId === 'page-status') {
        refreshFlowStatus().catch(() => {});
      }
      if (pageId === 'page-config') {
        onConfigPageShown().catch(() => {});
      }
      if (pageId === 'page-control') {
        onControlPageShown().catch(() => {});
      }
      if (pageId === 'page-local-config') {
        onLocalConfigPageShown().catch(() => {});
      }
      if (pageId === 'page-upgrade') {
        onUpgradePageShown().catch(() => {});
      } else {
        stopUpgradeStatusPolling();
      }
      closeMobileDrawer();
    }

    menuItems.forEach((item) => item.addEventListener('click', () => showPage(item.dataset.page)));

    menuToggles.forEach((btn) => btn.addEventListener('click', () => {
      if (isMobileLayout()) {
        setMobileDrawerOpen(!drawer.classList.contains('mobile-open'));
      } else {
        drawer.classList.toggle('collapsed');
      }
    }));

    overlay.addEventListener('click', closeMobileDrawer);
    window.addEventListener('resize', () => {
      if (!isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
    });

    const term = document.getElementById('term');
    const wsStatus = document.getElementById('wsStatus');
    const logSourceSelect = document.getElementById('logSourceSelect');
    const line = document.getElementById('line');
    const sendBtn = document.getElementById('send');
    const clearBtn = document.getElementById('clear');
    const toggleAutoscrollInput = document.getElementById('toggleAutoscroll');
    let autoScrollEnabled = true;
    let terminalActive = false;
    const lineDefaultPlaceholder = line ? line.placeholder : '';

    const updateHost = document.getElementById('updateHost');
    const flowPath = document.getElementById('flowPath');
    const nextionPath = document.getElementById('nextionPath');
    const supervisorPath = document.getElementById('supervisorPath');
    const cfgdocsPath = document.getElementById('cfgdocsPath');
    const saveCfgBtn = document.getElementById('saveCfg');
    const upSupervisorBtn = document.getElementById('upSupervisor');
    const upFlowBtn = document.getElementById('upFlow');
    const upNextionBtn = document.getElementById('upNextion');
    const upCfgdocsBtn = document.getElementById('upCfgdocs');
    const refreshStateBtn = document.getElementById('refreshState');
    const upgradeStatusText = document.getElementById('upgradeStatusText');
    const upgradeProgressBar = document.getElementById('upgradeProgressBar');
    const upStatusChip = document.getElementById('upStatusChip');

    const wifiEnabled = document.getElementById('wifiEnabled');
    const wifiSsid = document.getElementById('wifiSsid');
    const wifiSsidList = document.getElementById('wifiSsidList');
    const wifiPass = document.getElementById('wifiPass');
    const toggleWifiPassBtn = document.getElementById('toggleWifiPass');
    const scanWifiBtn = document.getElementById('scanWifi');
    const applyWifiCfgBtn = document.getElementById('applyWifiCfg');
    const wifiConfigStatus = document.getElementById('wifiConfigStatus');
    const rebootSupervisorBtn = document.getElementById('rebootSupervisor');
    const rebootFlowBtn = document.getElementById('rebootFlow');
    const flowFactoryResetBtn = document.getElementById('flowFactoryReset');
    const systemStatusText = document.getElementById('systemStatusText');
    const flowStatusRefreshBtn = document.getElementById('flowStatusRefresh');
    const flowStatusChip = document.getElementById('flowStatusChip');
    const flowStatusGrid = document.getElementById('flowStatusGrid');
    const flowStatusRaw = document.getElementById('flowStatusRaw');
    const flowCfgTitle = document.getElementById('flowCfgTitle');
    const flowCfgRefreshBtn = document.getElementById('flowCfgRefresh');
    const flowCfgApplyBtn = document.getElementById('flowCfgApply');
    const flowCfgSections = document.getElementById('flowCfgSections');
    const flowCfgFields = document.getElementById('flowCfgFields');
    const flowCfgStatus = document.getElementById('flowCfgStatus');
    const supCfgModuleSelect = document.getElementById('supCfgModuleSelect');
    const supCfgRefreshBtn = document.getElementById('supCfgRefresh');
    const supCfgApplyBtn = document.getElementById('supCfgApply');
    const supCfgFields = document.getElementById('supCfgFields');
    const supCfgStatus = document.getElementById('supCfgStatus');
    let wifiScanPollTimer = null;
    let upgradeStatusPollTimer = null;
    let flowCfgCurrentModule = '';
    let flowCfgCurrentData = {};
    let flowCfgChildrenCache = {};
    let flowCfgPath = [];
    let cfgDocSources = [];
    let flowCfgDocsLoaded = false;
    let flowCfgLoadingDepth = 0;
    let upgradeCfgLoadedOnce = false;
    let wifiConfigLoadedOnce = false;
    let flowCfgLoadedOnce = false;
    let supCfgLoadedOnce = false;
    let supCfgCurrentModule = '';
    let supCfgCurrentData = {};
    let wifiScanAutoRequested = false;
    let flowStatusReqSeq = 0;
    const flowStatusDomainTtlMs = 20000;
    const flowStatusDomainKeys = ['system', 'wifi', 'mqtt', 'i2c'];
    const flowStatusDomainCache = {
      system: { data: null, fetchedAt: 0 },
      wifi: { data: null, fetchedAt: 0 },
      mqtt: { data: null, fetchedAt: 0 },
      i2c: { data: null, fetchedAt: 0 }
    };

    const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
    let logSource = 'flow';
    let logSocket = null;

    function setWsStatusText(status) {
      const sourceLabel = logSource === 'supervisor' ? 'Supervisor' : 'Flow.IO';
      wsStatus.textContent = sourceLabel + ' : ' + status;
    }

    const ansiState = { fg: null };
    const ansiFgMap = {
      30: '#94a3b8', 31: '#ef4444', 32: '#22c55e', 33: '#f59e0b',
      34: '#60a5fa', 35: '#f472b6', 36: '#22d3ee', 37: '#e2e8f0',
      90: '#64748b', 91: '#f87171', 92: '#4ade80', 93: '#fbbf24',
      94: '#93c5fd', 95: '#f9a8d4', 96: '#67e8f9', 97: '#f8fafc'
    };
    const ansiRe = /\u001b\[([0-9;]*)m/g;

    function applySgrCodes(rawCodes) {
      const codes = rawCodes === '' ? [0] : rawCodes.split(';').map((v) => parseInt(v, 10)).filter(Number.isFinite);
      for (const code of codes) {
        if (code === 0 || code === 39) {
          ansiState.fg = null;
          continue;
        }
        if (Object.prototype.hasOwnProperty.call(ansiFgMap, code)) {
          ansiState.fg = ansiFgMap[code];
        }
      }
    }

    function decodeAnsiLine(rawLine) {
      let out = '';
      let cursor = 0;
      let lineColor = ansiState.fg;
      rawLine.replace(ansiRe, (full, codes, idx) => {
        out += rawLine.slice(cursor, idx);
        applySgrCodes(codes);
        if (ansiState.fg) lineColor = ansiState.fg;
        cursor = idx + full.length;
        return '';
      });
      out += rawLine.slice(cursor);
      return { text: out, color: lineColor };
    }

    function updateTerminalInputState() {
      const canSend = logSource === 'flow';
      line.disabled = !canSend;
      sendBtn.disabled = !canSend;
      line.placeholder = canSend ? lineDefaultPlaceholder : 'Envoi désactivé pour les journaux Supervisor';
    }

    function closeLogSocket() {
      if (!logSocket) return;
      logSocket.onopen = null;
      logSocket.onclose = null;
      logSocket.onerror = null;
      logSocket.onmessage = null;
      try {
        logSocket.close();
      } catch (err) {
        // Ignore close errors on stale sockets.
      }
      logSocket = null;
    }

    function connectLogSocket() {
      closeLogSocket();
      setWsStatusText('connexion...');
      const path = logSource === 'supervisor' ? '/wslog' : '/wsserial';
      const socket = new WebSocket(wsProto + '://' + location.host + path);
      logSocket = socket;
      socket.onopen = () => {
        if (socket !== logSocket) return;
        setWsStatusText('connecté');
      };
      socket.onclose = (ev) => {
        if (socket !== logSocket) return;
        const code = ev && Number.isFinite(ev.code) ? ev.code : 0;
        setWsStatusText(code ? ('déconnecté (' + code + ')') : 'déconnecté');
      };
      socket.onerror = () => {
        if (socket !== logSocket) return;
        setWsStatusText('erreur');
      };
      socket.onmessage = (ev) => {
        if (socket !== logSocket) return;
        const raw = String(ev.data || '');
        const parsed = decodeAnsiLine(raw);
        const row = document.createElement('div');
        row.className = 'log-line';
        if (parsed.color) row.style.color = parsed.color;
        row.textContent = parsed.text;
        term.appendChild(row);
        while (term.childNodes.length > 2000) term.removeChild(term.firstChild);
        if (autoScrollEnabled) term.scrollTop = term.scrollHeight;
      };
    }

    function setLogSource(source) {
      logSource = source === 'supervisor' ? 'supervisor' : 'flow';
      if (logSourceSelect && logSourceSelect.value !== logSource) {
        logSourceSelect.value = logSource;
      }
      updateTerminalInputState();
      if (terminalActive) {
        connectLogSocket();
      }
    }

    function refreshAutoscrollUi() {
      toggleAutoscrollInput.checked = autoScrollEnabled;
      toggleAutoscrollInput.setAttribute('aria-checked', autoScrollEnabled ? 'true' : 'false');
      toggleAutoscrollInput.setAttribute(
        'title',
        autoScrollEnabled ? 'Défilement auto activé' : 'Défilement auto désactivé'
      );
    }

    function sendLine() {
      const txt = line.value;
      if (!txt) return;
      if (logSource !== 'flow') return;
      if (logSocket && logSocket.readyState === WebSocket.OPEN) {
        logSocket.send(txt);
      }
      line.value = '';
      line.focus();
    }

    const iconeOeilOuvert =
      '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M12 5C6.7 5 2.73 8.11 1.16 11.25a1.7 1.7 0 0 0 0 1.5C2.73 15.89 6.7 19 12 19s9.27-3.11 10.84-6.25a1.7 1.7 0 0 0 0-1.5C21.27 8.11 17.3 5 12 5Zm0 12c-4.46 0-7.88-2.66-9.29-5 .64-1.06 1.74-2.21 3.17-3.11A11.7 11.7 0 0 1 12 7c4.46 0 7.88 2.66 9.29 5-.64 1.06-1.74 2.21-3.17 3.11A11.7 11.7 0 0 1 12 17Zm0-6.4A2.4 2.4 0 1 0 14.4 13 2.4 2.4 0 0 0 12 10.6Z"/></svg>';
    const iconeOeilBarre =
      '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M3.28 2.22 2.22 3.28l2.64 2.64c-1.54 1.14-2.84 2.65-3.7 4.34a1.7 1.7 0 0 0 0 1.5C2.73 14.89 6.7 18 12 18c2.2 0 4.18-.53 5.93-1.38l2.79 2.79 1.06-1.06ZM12 16.2c-4.46 0-7.88-2.66-9.29-5 .73-1.21 2.09-2.57 3.9-3.53l1.66 1.66a3.8 3.8 0 0 0 5.4 5.4l1.75 1.75c-1.02.44-2.16.72-3.42.72Zm.04-9.4 4.35 4.35c0-.05.01-.1.01-.15a4.4 4.4 0 0 0-4.4-4.4h-.01a.8.8 0 0 1 .05.2Zm9.8 4.45C20.27 8.11 16.3 5 11 5c-1.8 0-3.46.35-4.97.95l1.47 1.47A11.5 11.5 0 0 1 12 6.8c4.46 0 7.88 2.66 9.29 5-.43.71-1.03 1.47-1.79 2.16l1.28 1.28c.86-.78 1.56-1.65 2.06-2.63a1.7 1.7 0 0 0 0-1.36Z"/></svg>';

    function mettreAJourEtatVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer) {
      if (!inputEl || !toggleBtn) return;
      const isVisible = inputEl.type === 'text';
      toggleBtn.innerHTML = isVisible ? iconeOeilOuvert : iconeOeilBarre;
      toggleBtn.setAttribute('aria-pressed', isVisible ? 'true' : 'false');
      toggleBtn.setAttribute('aria-label', isVisible ? labelMasquer : labelAfficher);
      toggleBtn.setAttribute('title', isVisible ? 'Mot de passe en clair' : 'Mot de passe masqué');
    }

    function basculerVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer) {
      if (!inputEl || !toggleBtn) return;
      const isMasked = inputEl.type === 'password';
      inputEl.type = isMasked ? 'text' : 'password';
      mettreAJourEtatVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer);
    }
    sendBtn.addEventListener('click', sendLine);
    line.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') sendLine();
    });
    clearBtn.addEventListener('click', () => { term.textContent = ''; });
    toggleAutoscrollInput.addEventListener('change', () => {
      autoScrollEnabled = !!toggleAutoscrollInput.checked;
      refreshAutoscrollUi();
      if (autoScrollEnabled) term.scrollTop = term.scrollHeight;
    });
    if (logSourceSelect) {
      logSourceSelect.addEventListener('change', () => {
        setLogSource(logSourceSelect.value);
      });
    }
    refreshAutoscrollUi();
    logSource = logSourceSelect ? logSourceSelect.value : 'flow';
    updateTerminalInputState();
    setWsStatusText('inactif');
    flowCfgApplyBtn.disabled = true;
    supCfgApplyBtn.disabled = true;

    function setUpgradeProgress(value) {
      const p = Math.max(0, Math.min(100, Number(value) || 0));
      upgradeProgressBar.style.width = p + '%';
    }

    function setUpgradeMessage(text) {
      upgradeStatusText.textContent = text;
    }

    function updateUpgradeView(data) {
      if (!data || data.ok !== true) return;
      const state = data.state || 'inconnu';
      const target = data.target || '-';
      const progress = Number.isFinite(data.progress) ? data.progress : 0;
      const msg = data.msg || '';
      let stateLabel = state;
      if (state === 'idle') stateLabel = 'inactif';
      else if (state === 'queued') stateLabel = 'en attente';
      else if (state === 'running') stateLabel = 'en cours';
      else if (state === 'done') stateLabel = 'terminé';
      else if (state === 'error') stateLabel = 'erreur';
      upStatusChip.textContent = stateLabel;
      let p = progress;
      if (state === 'done') p = 100;
      if (state === 'queued' && p <= 0) p = 2;
      setUpgradeProgress(p);
      setUpgradeMessage(stateLabel + ' | cible=' + target + (msg ? ' | ' + msg : ''));
    }

    async function loadUpgradeConfig() {
      try {
        const res = await fetch('/api/fwupdate/config', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) {
          updateHost.value = data.update_host || '';
          flowPath.value = data.flowio_path || '';
          supervisorPath.value = data.supervisor_path || '';
          nextionPath.value = data.nextion_path || '';
          cfgdocsPath.value = data.cfgdocs_path || '';
        }
      } catch (err) {
        setUpgradeMessage('Échec du chargement de la configuration : ' + err);
      }
    }

    async function saveUpgradeConfig() {
      const body = new URLSearchParams();
      body.set('update_host', updateHost.value.trim());
      body.set('flowio_path', flowPath.value.trim());
      body.set('supervisor_path', supervisorPath.value.trim());
      body.set('nextion_path', nextionPath.value.trim());
      body.set('cfgdocs_path', cfgdocsPath.value.trim());
      const res = await fetch('/api/fwupdate/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec enregistrement');
      setUpgradeMessage('Configuration enregistrée.');
    }

    async function refreshUpgradeStatus() {
      try {
        const res = await fetch('/api/fwupdate/status', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) updateUpgradeView(data);
      } catch (err) {
        setUpgradeMessage('Échec de lecture de l\'état : ' + err);
      }
    }

    async function startUpgrade(target) {
      try {
        await saveUpgradeConfig();
        let endpoint = '/fwupdate/nextion';
        if (target === 'supervisor') endpoint = '/fwupdate/supervisor';
        else if (target === 'flowio') endpoint = '/fwupdate/flowio';
        else if (target === 'cfgdocs') endpoint = '/fwupdate/cfgdocs';
        const res = await fetch(endpoint, { method: 'POST' });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data.ok) throw new Error('échec démarrage');
        setUpgradeProgress(1);
        setUpgradeMessage('Demande de mise à jour acceptée pour ' + target + '.');
        await refreshUpgradeStatus();
      } catch (err) {
        setUpgradeMessage('Échec de la mise à jour : ' + err);
      }
    }

    async function onConfigPageShown() {
      if (!wifiConfigLoadedOnce) {
        wifiConfigLoadedOnce = true;
        await loadWifiConfig();
      }
      if (!wifiScanAutoRequested) {
        wifiScanAutoRequested = true;
        await refreshWifiScanStatus(true);
      } else {
        await refreshWifiScanStatus(false);
      }
    }

    async function onControlPageShown() {
      if (!flowCfgDocsLoaded) {
        await chargerFlowCfgDocs();
      }
      if (!flowCfgLoadedOnce) {
        flowCfgLoadedOnce = true;
        await chargerFlowCfgModules(false);
        return;
      }
      await chargerFlowCfgModules(false);
    }

    async function onLocalConfigPageShown() {
      if (!flowCfgDocsLoaded) {
        await chargerFlowCfgDocs();
      }
      if (!supCfgLoadedOnce) {
        supCfgLoadedOnce = true;
        await chargerSupervisorCfgModules(false);
        return;
      }
      await chargerSupervisorCfgModules(true);
    }

    function fmtFlowStatusVal(v) {
      if (v === null || typeof v === 'undefined') return '-';
      return String(v);
    }

    function buildFlowStatusBoolIcon(v) {
      const ok = !!v;
      const span = document.createElement('span');
      span.className = ok ? 'status-bool is-true' : 'status-bool is-false';
      span.setAttribute('role', 'img');
      span.setAttribute('aria-label', ok ? 'OK' : 'NOK');
      span.title = ok ? 'OK' : 'NOK';
      span.innerHTML = ok
        ? '<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="6"></circle><path d="M5 8.2 7 10.2 11 6.2"></path></svg>'
        : '<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="6"></circle><path d="M5.5 5.5 10.5 10.5"></path><path d="M10.5 5.5 5.5 10.5"></path></svg>';
      return span;
    }

    function fmtFlowUptime(ms) {
      if (!Number.isFinite(ms) || ms < 0) return '-';
      const sec = Math.floor(ms / 1000);
      const h = Math.floor(sec / 3600);
      const m = Math.floor((sec % 3600) / 60);
      const s = sec % 60;
      if (h > 0) return h + 'h ' + m + 'm ' + s + 's';
      if (m > 0) return m + 'm ' + s + 's';
      return s + 's';
    }

    function normalizeIpValue(ip) {
      if (typeof ip === 'string') {
        const trimmed = ip.trim();
        return trimmed || '-';
      }
      if (Array.isArray(ip)) {
        return ip.map((part) => String(part)).join('.') || '-';
      }
      if (ip && typeof ip === 'object') {
        const keys = Object.keys(ip).sort((a, b) => Number(a) - Number(b));
        if (keys.length > 0) {
          return keys.map((key) => String(ip[key])).join('.') || '-';
        }
      }
      if (typeof ip === 'number' && Number.isFinite(ip)) {
        return String(ip);
      }
      return '-';
    }

    function buildFlowStatusFromDomains(domainData) {
      const merged = { ok: true };
      let anyDomainOk = false;

      const system = domainData.system;
      if (system && system.ok === true) {
        anyDomainOk = true;
        merged.fw = system.fw || '';
        merged.upms = system.upms ?? 0;
        merged.heap = (system.heap && typeof system.heap === 'object') ? system.heap : {};
      }

      const wifi = domainData.wifi;
      if (wifi && wifi.ok === true && wifi.wifi && typeof wifi.wifi === 'object') {
        anyDomainOk = true;
        merged.wifi = Object.assign({}, wifi.wifi, { ip: normalizeIpValue(wifi.wifi.ip) });
      }

      const mqtt = domainData.mqtt;
      if (mqtt && mqtt.ok === true && mqtt.mqtt && typeof mqtt.mqtt === 'object') {
        anyDomainOk = true;
        merged.mqtt = mqtt.mqtt;
      }

      const i2c = domainData.i2c;
      if (i2c && i2c.ok === true && i2c.i2c && typeof i2c.i2c === 'object') {
        anyDomainOk = true;
        merged.i2c = i2c.i2c;
      }

      return anyDomainOk ? merged : null;
    }

    function getCachedFlowStatusData() {
      const domainData = {};
      flowStatusDomainKeys.forEach((domainKey) => {
        domainData[domainKey] = flowStatusDomainCache[domainKey].data;
      });
      return buildFlowStatusFromDomains(domainData);
    }

    async function fetchFlowStatusDomain(domainKey, forceRefresh) {
      const cacheEntry = flowStatusDomainCache[domainKey];
      const now = Date.now();
      const cacheValid = !!cacheEntry.data && ((now - cacheEntry.fetchedAt) < flowStatusDomainTtlMs);
      if (!forceRefresh && cacheValid) {
        return cacheEntry.data;
      }

      try {
        const res = await fetchFlowRemoteQueued(
          '/api/flow/status/domain?d=' + encodeURIComponent(domainKey),
          { cache: 'no-store' }
        );
        const data = await res.json().catch(() => null);
        if (!res.ok || !data || data.ok !== true) {
          throw new Error('statut ' + domainKey + ' indisponible');
        }
        cacheEntry.data = data;
        cacheEntry.fetchedAt = Date.now();
        return data;
      } catch (err) {
        if (cacheEntry.data) {
          return cacheEntry.data;
        }
        throw err;
      }
    }

    function appendFlowStatusCard(title, rows) {
      const card = document.createElement('div');
      card.className = 'status-card';
      const heading = document.createElement('h3');
      heading.textContent = title;
      card.appendChild(heading);

      const kv = document.createElement('div');
      kv.className = 'status-kv';
      rows.forEach((row) => {
        const line = document.createElement('div');
        const key = document.createElement('span');
        key.textContent = row[0];
        const value = document.createElement('b');
        if (typeof row[1] === 'boolean') {
          value.appendChild(buildFlowStatusBoolIcon(row[1]));
        } else {
          value.textContent = fmtFlowStatusVal(row[1]);
        }
        line.appendChild(key);
        line.appendChild(value);
        kv.appendChild(line);
      });
      card.appendChild(kv);
      flowStatusGrid.appendChild(card);
    }

    function createSkeletonLine(className, widthPercent) {
      const line = document.createElement('div');
      line.className = className ? ('skeleton-line ' + className) : 'skeleton-line';
      if (Number.isFinite(widthPercent) && widthPercent > 0) {
        line.style.width = widthPercent + '%';
      }
      return line;
    }

    function appendFlowStatusSkeletonCard() {
      const card = document.createElement('div');
      card.className = 'status-card status-card-skeleton';
      const title = createSkeletonLine('skeleton-title', 46);
      card.appendChild(title);

      const kv = document.createElement('div');
      kv.className = 'status-kv';
      const widths = [
        [44, 24],
        [40, 30],
        [35, 20],
        [48, 26]
      ];
      widths.forEach((pair) => {
        const row = document.createElement('div');
        row.appendChild(createSkeletonLine('skeleton-key', pair[0]));
        row.appendChild(createSkeletonLine('skeleton-value', pair[1]));
        kv.appendChild(row);
      });
      card.appendChild(kv);
      flowStatusGrid.appendChild(card);
    }

    function renderFlowStatusSkeleton() {
      flowStatusChip.textContent = 'chargement...';
      flowStatusGrid.innerHTML = '';
      for (let i = 0; i < 4; ++i) {
        appendFlowStatusSkeletonCard();
      }
      flowStatusRaw.hidden = true;
      flowStatusRaw.classList.remove('is-skeleton');
      flowStatusRaw.innerHTML = '';
    }

    function renderFlowStatus(data) {
      const wifi = (data && typeof data.wifi === 'object') ? data.wifi : {};
      const mqtt = (data && typeof data.mqtt === 'object') ? data.mqtt : {};
      const heap = (data && data.heap && typeof data.heap === 'object') ? data.heap : {};
      const i2c = (data && data.i2c && typeof data.i2c === 'object') ? data.i2c : {};
      const firmware = data.fw || '-';
      const uptimeMs = data.upms || 0;
      const wifiReady = !!wifi.rdy;
      const wifiIp = normalizeIpValue(wifi.ip);
      const wifiHasRssi = !!wifi.hrss;
      const wifiRssi = wifi.rssi ?? '-';
      const mqttReady = !!mqtt.rdy;
      const mqttRxDrop = mqtt.rxdrp ?? 0;
      const mqttParseFail = mqtt.prsf ?? 0;
      const mqttHandlerFail = mqtt.hndf ?? 0;
      const mqttOversizeDrop = mqtt.ovr ?? 0;
      const i2cLinkOk = !!i2c.lnk;
      const i2cSeen = !!i2c.seen;
      const i2cReqCount = i2c.req ?? 0;
      const i2cAgo = i2c.ago ?? 0;

      flowStatusGrid.innerHTML = '';
      appendFlowStatusCard('Réseau', [
        ['WiFi connecté', wifiReady],
        ['IP', wifiIp],
        ['RSSI (dBm)', wifiHasRssi ? wifiRssi : '-']
      ]);
      appendFlowStatusCard('Supervisor', [
        ['Firmware', supervisorFirmwareVersion],
        ['Lien actif', i2cLinkOk],
        ['Supervisor vu', i2cSeen],
        ['Req total', i2cReqCount],
        ['Dernière req (ms)', i2cAgo]
      ]);
      appendFlowStatusCard('Système Flow.IO', [
        ['Firmware', firmware],
        ['Uptime', fmtFlowUptime(uptimeMs)],
        ['Heap libre', heap.free],
        ['Heap min', heap.min]
      ]);
      appendFlowStatusCard('MQTT', [
        ['Connecté', mqttReady],
        ['RX drop', mqttRxDrop],
        ['Parse fail', mqttParseFail],
        ['Handler fail', mqttHandlerFail],
        ['Oversize drop', mqttOversizeDrop]
      ]);

      flowStatusChip.textContent = i2cLinkOk ? 'Flow.IO en ligne (I2C OK)' : 'Flow.IO partiel / lien I2C faible';
      flowStatusRaw.hidden = true;
      flowStatusRaw.classList.remove('is-skeleton');
      flowStatusRaw.innerHTML = '';
    }

    async function refreshFlowStatus(forceRefresh) {
      const reqSeq = ++flowStatusReqSeq;
      const cachedData = !forceRefresh ? getCachedFlowStatusData() : null;
      if (cachedData) {
        renderFlowStatus(cachedData);
      } else {
        renderFlowStatusSkeleton();
      }
      try {
        const domainData = {};
        for (const domainKey of flowStatusDomainKeys) {
          domainData[domainKey] = await fetchFlowStatusDomain(domainKey, !!forceRefresh);
          if (reqSeq !== flowStatusReqSeq) return;
        }
        if (reqSeq !== flowStatusReqSeq) return;
        const data = buildFlowStatusFromDomains(domainData);
        if (!data) throw new Error('statut indisponible');
        renderFlowStatus(data);
      } catch (err) {
        if (reqSeq !== flowStatusReqSeq) return;
        const fallbackData = getCachedFlowStatusData();
        if (fallbackData) {
          renderFlowStatus(fallbackData);
          flowStatusChip.textContent = 'statut affiché depuis le cache local';
          return;
        }
        flowStatusRaw.hidden = true;
        flowStatusRaw.classList.remove('is-skeleton');
        flowStatusChip.textContent = 'erreur lecture statut';
        flowStatusGrid.innerHTML = '';
        flowStatusRaw.innerHTML = '';
      }
    }

    async function onUpgradePageShown() {
      if (!upgradeCfgLoadedOnce) {
        upgradeCfgLoadedOnce = true;
        await loadUpgradeConfig();
      }
      await refreshUpgradeStatus();
      startUpgradeStatusPolling();
    }

    function stopWifiScanPolling() {
      if (wifiScanPollTimer) {
        clearTimeout(wifiScanPollTimer);
        wifiScanPollTimer = null;
      }
    }

    function scheduleWifiScanPolling() {
      stopWifiScanPolling();
      wifiScanPollTimer = setTimeout(() => {
        refreshWifiScanStatus(false);
      }, 1200);
    }

    function renderWifiScanList(data) {
      const networks = (data && Array.isArray(data.networks)) ? data.networks : [];
      const currentSsid = (wifiSsid.value || '').trim();
      const prevSelected = wifiSsidList.value || '';
      const maxWifiOptionLabelLen = 56;

      wifiSsidList.innerHTML = '';
      const manualOpt = document.createElement('option');
      manualOpt.value = '';
      manualOpt.textContent = 'Saisie manuelle';
      wifiSsidList.appendChild(manualOpt);

      for (const net of networks) {
        if (!net || typeof net.ssid !== 'string' || net.ssid.length === 0) continue;
        if (net.hidden) continue;
        const opt = document.createElement('option');
        const secureSuffix = net.secure ? ' (securise)' : ' (ouvert)';
        const rssiSuffix = Number.isFinite(net.rssi) ? (' ' + net.rssi + ' dBm') : '';
        const fullLabel = net.ssid + secureSuffix + rssiSuffix;
        opt.value = net.ssid;
        opt.textContent = fullLabel.length > maxWifiOptionLabelLen
            ? (fullLabel.slice(0, maxWifiOptionLabelLen - 1) + '…')
            : fullLabel;
        wifiSsidList.appendChild(opt);
      }

      const values = Array.from(wifiSsidList.options).map((o) => o.value);
      if (currentSsid && values.includes(currentSsid)) {
        wifiSsidList.value = currentSsid;
      } else if (prevSelected && values.includes(prevSelected)) {
        wifiSsidList.value = prevSelected;
      } else {
        wifiSsidList.value = '';
      }
    }

    function updateWifiScanStatusText(data, reqError) {
      if (reqError) {
        wifiConfigStatus.textContent = 'Scan WiFi indisponible: ' + reqError;
        return;
      }
      if (!data || data.ok !== true) {
        wifiConfigStatus.textContent = 'Scan WiFi : réponse invalide.';
        return;
      }

      const running = !!data.running;
      const requested = !!data.requested;
      const count = Number.isFinite(data.count) ? data.count : 0;
      const totalFound = Number.isFinite(data.total_found) ? data.total_found : count;
      if (running || requested) {
        wifiConfigStatus.textContent = 'Scan WiFi en cours...';
        return;
      }
      if (count > 0) {
        wifiConfigStatus.textContent = 'Scan WiFi terminé : ' + count + ' réseaux affichés (' + totalFound + ' détectés).';
        return;
      }
      wifiConfigStatus.textContent = 'Aucun réseau visible détecté.';
    }

    function toBool(v) {
      if (typeof v === 'boolean') return v;
      if (typeof v === 'number') return v !== 0;
      if (typeof v === 'string') {
        const s = v.trim().toLowerCase();
        return s === '1' || s === 'true' || s === 'on' || s === 'yes';
      }
      return false;
    }

    async function requestWifiScan(force) {
      const body = new URLSearchParams();
      body.set('force', force ? '1' : '0');
      const res = await fetch('/api/wifi/scan', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec démarrage scan');
      return data;
    }

    async function refreshWifiScanStatus(triggerScan) {
      try {
        if (triggerScan) {
          await requestWifiScan(true);
        }
        const res = await fetch('/api/wifi/scan', { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true) throw new Error('échec lecture état');

        renderWifiScanList(data);
        updateWifiScanStatusText(data, null);

        if (data.running || data.requested) {
          scheduleWifiScanPolling();
        } else {
          stopWifiScanPolling();
        }
      } catch (err) {
        stopWifiScanPolling();
        updateWifiScanStatusText(null, err);
      }
    }

    async function loadWifiConfig() {
      try {
        const res = await fetch('/api/wifi/config', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) {
          wifiEnabled.checked = toBool(data.enabled);
          wifiSsid.value = data.ssid || '';
          wifiPass.value = data.pass || '';
          wifiConfigStatus.textContent = 'Configuration WiFi chargée.';
        }
      } catch (err) {
        wifiConfigStatus.textContent = 'Chargement WiFi échoué: ' + err;
      }
    }

    async function saveWifiConfig() {
      const body = new URLSearchParams();
      body.set('enabled', wifiEnabled.checked ? '1' : '0');
      body.set('ssid', wifiSsid.value.trim());
      body.set('pass', wifiPass.value);

      const res = await fetch('/api/wifi/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec application');
      wifiConfigStatus.textContent = 'Configuration WiFi appliquée (reconnexion en cours).';
    }

    function nettoyerNomFlowCfg(moduleName) {
      return String(moduleName || '').trim().replace(/^\/+|\/+$/g, '');
    }

    function cheminFlowCfgCourant() {
      return flowCfgPath.length > 0 ? flowCfgPath.join('/') : '';
    }

    function flowCfgTitreDepuisChemin(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return 'Configuration';
      return 'Configuration > ' + cleanPath.split('/').join(' > ');
    }

    function flowCfgCachedChildren(prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      const key = flowCfgCacheKey(p);
      const node = flowCfgChildrenCache[key];
      if (!node || !Array.isArray(node.children)) return [];
      return node.children
        .filter((name) => !isConfigPathHidden(p ? (p + '/' + name) : name))
        .slice();
    }

    function closeFlowCfgCrumbMenus(except) {
      const wrappers = flowCfgTitle.querySelectorAll('.control-crumb.open');
      wrappers.forEach((wrapper) => {
        if (except && wrapper === except) return;
        wrapper.classList.remove('open');
      });
    }

    function renderFlowCfgTitle(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const segs = cleanPath.length > 0 ? cleanPath.split('/') : [];
      flowCfgTitle.innerHTML = '';
      flowCfgTitle.setAttribute('aria-label', flowCfgTitreDepuisChemin(cleanPath));

      const rootBtn = document.createElement('button');
      rootBtn.type = 'button';
      rootBtn.className = 'control-title-root-btn' + (segs.length === 0 ? ' active' : '');
      rootBtn.setAttribute('aria-label', 'Racine configuration');
      rootBtn.innerHTML =
        '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M19.14 12.94c.04-.31.06-.63.06-.94s-.02-.63-.06-.94l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.3 7.3 0 0 0-1.63-.94l-.36-2.54a.5.5 0 0 0-.5-.42h-3.84a.5.5 0 0 0-.5.42l-.36 2.54c-.58.23-1.13.54-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.71 8.84a.5.5 0 0 0 .12.64l2.03 1.58c-.04.31-.06.63-.06.94s.02.63.06.94l-2.03 1.58a.5.5 0 0 0-.12.64l1.92 3.32a.5.5 0 0 0 .6.22l2.39-.96c.5.4 1.05.71 1.63.94l.36 2.54a.5.5 0 0 0 .5.42h3.84a.5.5 0 0 0 .5-.42l.36-2.54c.58-.23 1.13-.54 1.63-.94l2.39.96a.5.5 0 0 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64l-2.03-1.58ZM12 15.5A3.5 3.5 0 1 1 12 8.5a3.5 3.5 0 0 1 0 7Z"/></svg>';
      if (segs.length === 0) {
        rootBtn.disabled = true;
      } else {
        rootBtn.addEventListener('click', async () => {
          closeFlowCfgCrumbMenus();
          flowCfgPath = [];
          await renderFlowCfgNavigator(false);
        });
      }
      flowCfgTitle.appendChild(rootBtn);

      if (segs.length === 0) {
        return;
      }

      const rootSep = document.createElement('span');
      rootSep.className = 'control-title-sep';
      rootSep.textContent = '/';
      flowCfgTitle.appendChild(rootSep);

      for (let i = 0; i < segs.length; ++i) {
        if (i > 0) {
          const sep = document.createElement('span');
          sep.className = 'control-title-sep';
          sep.textContent = '/';
          flowCfgTitle.appendChild(sep);
        }

        const crumb = document.createElement('span');
        crumb.className = 'control-crumb';
        const depth = i + 1;
        const isActive = depth === segs.length;

        const labelBtn = document.createElement('button');
        labelBtn.type = 'button';
        labelBtn.className = 'control-title-crumb-btn' + (isActive ? ' active' : '');
        labelBtn.textContent = segs[i];
        if (isActive) {
          labelBtn.disabled = true;
        } else {
          labelBtn.addEventListener('click', async () => {
            closeFlowCfgCrumbMenus();
            flowCfgPath = segs.slice(0, depth);
            await renderFlowCfgNavigator(false);
          });
        }
        crumb.appendChild(labelBtn);

        const parentPrefix = i === 0 ? '' : segs.slice(0, i).join('/');
        const siblings = flowCfgCachedChildren(parentPrefix);
        const menuChoices = siblings.length > 0 ? siblings : [segs[i]];

        const toggleBtn = document.createElement('button');
        toggleBtn.type = 'button';
        toggleBtn.className = 'control-crumb-toggle';
        toggleBtn.setAttribute('aria-label', 'Choisir une branche au niveau ' + (i + 1));
        toggleBtn.innerHTML =
          '<svg class="control-crumb-arrows" viewBox="0 0 12 16" aria-hidden="true">' +
          '<path fill="currentColor" d="M6 2.2 9.4 6H2.6L6 2.2Z"/>' +
          '<path fill="currentColor" d="M6 13.8 2.6 10h6.8L6 13.8Z"/>' +
          '</svg>';
        toggleBtn.addEventListener('click', (event) => {
          event.stopPropagation();
          const willOpen = !crumb.classList.contains('open');
          closeFlowCfgCrumbMenus(crumb);
          crumb.classList.toggle('open', willOpen);
        });
        crumb.appendChild(toggleBtn);

        const menu = document.createElement('div');
        menu.className = 'control-crumb-menu';
        const sortedChoices = Array.from(new Set(menuChoices)).sort((a, b) => a.localeCompare(b));
        for (const choice of sortedChoices) {
          const item = document.createElement('button');
          item.type = 'button';
          item.className = 'control-crumb-menu-item' + (choice === segs[i] ? ' active' : '');
          item.textContent = choice;
          item.addEventListener('click', async (event) => {
            event.stopPropagation();
            closeFlowCfgCrumbMenus();
            flowCfgPath = segs.slice(0, i).concat([choice]);
            await renderFlowCfgNavigator(false);
          });
          menu.appendChild(item);
        }
        crumb.appendChild(menu);
        flowCfgTitle.appendChild(crumb);
      }
    }

    function renderFlowCfgSections(node, currentPath) {
      flowCfgSections.innerHTML = '';
      const cleanPath = nettoyerNomFlowCfg(currentPath);
      const children = (node && Array.isArray(node.children))
        ? node.children.filter((name) => !isConfigPathHidden(cleanPath ? (cleanPath + '/' + name) : name))
        : [];
      if (children.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'control-chip-empty';
        empty.textContent = 'Aucune sous-section.';
        flowCfgSections.appendChild(empty);
        return;
      }

      for (const child of children) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'control-chip';
        button.textContent = child;
        button.addEventListener('click', async () => {
          const nextPath = currentPath ? (currentPath + '/' + child) : child;
          flowCfgPath = nextPath.split('/');
          await renderFlowCfgNavigator(false);
        });
        flowCfgSections.appendChild(button);
      }
    }

    function flowCfgCacheKey(prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      return p.length > 0 ? p : '__root__';
    }

    function renderFlowCfgSectionsSkeleton() {
      flowCfgSections.innerHTML = '';
      const widths = [16, 22, 14, 20, 18];
      widths.forEach((w) => {
        const chip = document.createElement('span');
        chip.className = 'control-chip control-chip-skeleton';
        chip.style.width = w + '%';
        flowCfgSections.appendChild(chip);
      });
    }

    function renderFlowCfgFieldsSkeleton() {
      flowCfgFields.innerHTML = '';
      for (let i = 0; i < 5; ++i) {
        const row = document.createElement('div');
        row.className = 'control-row control-row-skeleton';

        const labelWrap = document.createElement('div');
        labelWrap.className = 'control-label-wrap';
        labelWrap.appendChild(createSkeletonLine('', i % 2 === 0 ? 38 : 44));
        if (i % 2 === 0) {
          labelWrap.appendChild(createSkeletonLine('', 62));
        }
        row.appendChild(labelWrap);

        const inputSkel = document.createElement('div');
        if (i === 1) {
          inputSkel.className = 'control-switch-skeleton';
        } else {
          inputSkel.className = 'control-input-skeleton';
        }
        row.appendChild(inputSkel);
        flowCfgFields.appendChild(row);
      }
    }

    function beginFlowCfgLoading(statusText) {
      flowCfgLoadingDepth += 1;
      if (flowCfgLoadingDepth === 1) {
        flowCfgTitle.classList.add('is-loading');
        flowCfgRefreshBtn.disabled = true;
        flowCfgApplyBtn.disabled = true;
        renderFlowCfgSectionsSkeleton();
        renderFlowCfgFieldsSkeleton();
      }
      if (statusText) {
        flowCfgStatus.textContent = statusText;
      }
    }

    function endFlowCfgLoading() {
      if (flowCfgLoadingDepth > 0) {
        flowCfgLoadingDepth -= 1;
      }
      if (flowCfgLoadingDepth === 0) {
        flowCfgTitle.classList.remove('is-loading');
        flowCfgRefreshBtn.disabled = false;
      }
    }

    async function chargerFlowCfgDocs() {
      flowCfgDocsLoaded = true;
      try {
        const res = await fetch(versionedWebAssetUrl('/webinterface/cfgdocs.fr.json'), { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || typeof data !== 'object') {
          throw new Error('invalid docs payload');
        }
        const docs = (data.docs && typeof data.docs === 'object') ? data.docs : {};
        const meta = (data._meta && typeof data._meta === 'object') ? data._meta : {};
        cfgDocSources = [{ docs: docs, meta: meta }];
        try {
          const modsRes = await fetch(versionedWebAssetUrl('/webinterface/cfgmods.fr.json'), { cache: 'no-store' });
          const modsData = await modsRes.json();
          if (modsRes.ok && modsData && typeof modsData === 'object') {
            const modsDocs = (modsData.docs && typeof modsData.docs === 'object') ? modsData.docs : {};
            const modsMeta = (modsData._meta && typeof modsData._meta === 'object') ? modsData._meta : {};
            cfgDocSources.push({ docs: modsDocs, meta: modsMeta });
          }
        } catch (err) {
          // Ignore optional manual cfgmods file failures.
        }
      } catch (err) {
        cfgDocSources = [];
      }
    }

    function normalizeDocSource(source) {
      if (!source || typeof source !== 'object') return null;
      const docs = (source.docs && typeof source.docs === 'object') ? source.docs : {};
      const meta = (source.meta && typeof source.meta === 'object') ? source.meta : {};
      return { docs: docs, meta: meta };
    }

    function resolveEnumOptions(enumSetName, sources) {
      const setName = String(enumSetName || '').trim();
      if (!setName) return null;
      for (const src of sources) {
        const normalized = normalizeDocSource(src);
        if (!normalized) continue;
        const enumSets = (normalized.meta && normalized.meta.enum_sets &&
          typeof normalized.meta.enum_sets === 'object')
          ? normalized.meta.enum_sets
          : null;
        if (enumSets && Array.isArray(enumSets[setName])) {
          return enumSets[setName];
        }
      }
      return null;
    }

    function enrichResolvedDoc(doc, sources) {
      if (!doc || typeof doc !== 'object') return null;
      const resolved = Object.assign({}, doc);
      const enumSetName = (typeof resolved.enum_set === 'string') ? resolved.enum_set.trim() : '';
      const enumOptions = resolveEnumOptions(enumSetName, sources);
      if (enumSetName && Array.isArray(enumOptions)) {
        resolved._enumOptions = enumOptions;
      }
      return resolved;
    }

    function formatConfigValueForDisplay(value, displayFormat) {
      if (displayFormat === 'hex' && typeof value === 'number' && Number.isFinite(value)) {
        const raw = Math.max(0, Math.trunc(value));
        const width = raw <= 0xFF ? 2 : 0;
        const hex = raw.toString(16).toUpperCase();
        return '0x' + (width > 0 ? hex.padStart(width, '0') : hex);
      }
      return String(value ?? '');
    }

    function parseConfigNumericValue(rawValue, kind, displayFormat) {
      const raw = String(rawValue ?? '').trim();
      if (displayFormat === 'hex') {
        const normalized = raw.length > 0 ? raw : '0';
        const parsed = normalized.startsWith('0x') || normalized.startsWith('0X')
          ? Number.parseInt(normalized, 16)
          : Number.parseInt(normalized, 16);
        if (!Number.isFinite(parsed)) return 0;
        return (kind === 'float') ? parsed : parsed;
      }
      if (kind === 'float') {
        const parsed = Number.parseFloat(raw);
        return Number.isFinite(parsed) ? parsed : 0;
      }
      const parsed = Number.parseInt(raw, 10);
      return Number.isFinite(parsed) ? parsed : 0;
    }

    function configDocFor(moduleName, key, extraSources) {
      const m = nettoyerNomFlowCfg(moduleName);
      const k = String(key || '').trim();
      if (!m || !k) return null;

      const sources = [];
      if (Array.isArray(extraSources)) {
        extraSources.forEach((src) => {
          const normalized = normalizeDocSource(src);
          if (normalized) sources.push(normalized);
        });
      }
      cfgDocSources.forEach((src) => {
        const normalized = normalizeDocSource(src);
        if (normalized) sources.push(normalized);
      });

      let merged = null;
      for (const source of sources) {
        const docs = source.docs;
        const full = m + '/' + k;
        const wildcard = docs['*/' + k];
        if (wildcard && typeof wildcard === 'object') {
          merged = Object.assign(merged || {}, wildcard);
        }
        const exact = docs[full];
        if (exact && typeof exact === 'object') {
          merged = Object.assign(merged || {}, exact);
        }
      }
      return enrichResolvedDoc(merged, sources);
    }

    function configPathMeta(pathValue) {
      const p = nettoyerNomFlowCfg(pathValue);
      if (!p) return null;
      const sources = [];
      for (const src of cfgDocSources) {
        const normalized = normalizeDocSource(src);
        if (!normalized) continue;
        sources.push(normalized);
      }
      let merged = null;
      for (const normalized of sources) {
        const exact = normalized.docs[p];
        if (exact && typeof exact === 'object') {
          merged = Object.assign(merged || {}, exact);
        }
      }
      return enrichResolvedDoc(merged, sources);
    }

    function isConfigPathHidden(pathValue) {
      const meta = configPathMeta(pathValue);
      return !!(meta && meta.hidden === true);
    }

    async function chargerFlowCfgChildren(prefix, forceReload) {
      const p = nettoyerNomFlowCfg(prefix);
      const key = flowCfgCacheKey(p);
      if (!forceReload && flowCfgChildrenCache[key]) {
        return flowCfgChildrenCache[key];
      }

      const url = p.length > 0
        ? ('/api/flowcfg/children?prefix=' + encodeURIComponent(p))
        : '/api/flowcfg/children';
      const res = await fetchFlowRemoteQueued(url, { cache: 'no-store' });
      const data = await res.json();
      if (!res.ok || !data || data.ok !== true || !Array.isArray(data.children)) {
        throw new Error('liste enfants indisponible');
      }

      const children = data.children
        .filter((name) => typeof name === 'string' && name.length > 0)
        .map((name) => nettoyerNomFlowCfg(name))
        .filter((name) => name.length > 0)
        .sort((a, b) => a.localeCompare(b));

      const node = {
        prefix: p,
        hasExact: !!data.has_exact,
        children: Array.from(new Set(children))
      };
      flowCfgChildrenCache[key] = node;
      return node;
    }

    function resetFlowCfgEditor(message) {
      flowCfgCurrentModule = '';
      flowCfgCurrentData = {};
      flowCfgFields.innerHTML = '';
      flowCfgApplyBtn.disabled = true;
      if (message) {
        flowCfgStatus.textContent = message;
      }
    }

    function renderConfigFields(containerEl, moduleName, dataObj) {
      containerEl.innerHTML = '';
      const data = (dataObj && typeof dataObj === 'object') ? dataObj : {};
      const keys = Object.keys(data).sort();
      if (keys.length === 0) {
        const row = document.createElement('div');
        row.className = 'control-row';
        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = 'Aucun champ configurable dans cette branche.';
        row.appendChild(label);
        containerEl.appendChild(row);
        return;
      }

      for (const key of keys) {
        const value = data[key];
        const row = document.createElement('div');
        row.className = 'control-row';

        const doc = configDocFor(moduleName, key, []);
        const labelWrap = document.createElement('div');
        labelWrap.className = 'control-label-wrap';
        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = (doc && typeof doc.label === 'string' && doc.label.length > 0) ? doc.label : key;
        labelWrap.appendChild(label);

        const helpTxt = (doc && typeof doc.help === 'string') ? doc.help : '';
        if (helpTxt.length > 0) {
          const help = document.createElement('span');
          help.className = 'control-help';
          help.textContent = helpTxt;
          labelWrap.appendChild(help);
        }
        row.appendChild(labelWrap);

        const enumOptions = (doc && Array.isArray(doc._enumOptions)) ? doc._enumOptions : null;

        if (typeof value === 'boolean') {
          const sw = document.createElement('span');
          sw.className = 'md3-switch';
          const input = document.createElement('input');
          input.type = 'checkbox';
          input.checked = value;
          input.dataset.key = key;
          input.dataset.kind = 'bool';
          const track = document.createElement('span');
          track.className = 'md3-track';
          const thumb = document.createElement('span');
          thumb.className = 'md3-thumb';
          sw.appendChild(input);
          sw.appendChild(track);
          sw.appendChild(thumb);
          row.appendChild(sw);
        } else if (enumOptions && enumOptions.length > 0) {
          const select = document.createElement('select');
          select.className = 'control-input';
          select.dataset.key = key;
          select.dataset.kind = (typeof value === 'number' && Number.isInteger(value)) ? 'int' : (typeof value === 'number' ? 'float' : 'string');
          if (doc && typeof doc.display_format === 'string') {
            select.dataset.format = doc.display_format;
          }
          const currentValue = String(value);
          enumOptions.forEach((opt) => {
            if (!opt || typeof opt !== 'object') return;
            const optionEl = document.createElement('option');
            optionEl.value = String(opt.value);
            optionEl.textContent = (typeof opt.label === 'string' && opt.label.length > 0)
              ? opt.label
              : String(opt.value);
            if (optionEl.value === currentValue) {
              optionEl.selected = true;
            }
            select.appendChild(optionEl);
          });
          row.appendChild(select);
        } else if (typeof value === 'number') {
          const input = document.createElement('input');
          input.className = 'control-input';
          const displayFormat = (doc && typeof doc.display_format === 'string') ? doc.display_format : '';
          input.type = displayFormat === 'hex' ? 'text' : 'number';
          input.value = formatConfigValueForDisplay(value, displayFormat);
          if (displayFormat !== 'hex') {
            input.step = Number.isInteger(value) ? '1' : '0.001';
          }
          input.dataset.key = key;
          input.dataset.kind = Number.isInteger(value) ? 'int' : 'float';
          if (displayFormat) {
            input.dataset.format = displayFormat;
          }
          row.appendChild(input);
        } else {
          const isSecret = /pass|token|secret/i.test(key);
          const textValue = String(value ?? '');
          const input = document.createElement('input');
          input.className = 'control-input';
          input.type = isSecret ? 'password' : 'text';
          if (isSecret && textValue === '***') {
            input.value = '';
            input.placeholder = 'Conserver (masqué)';
            input.dataset.masked = '1';
          } else {
            input.value = textValue;
            input.dataset.masked = '0';
          }
          input.dataset.key = key;
          input.dataset.kind = 'string';
          row.appendChild(input);
        }

        containerEl.appendChild(row);
      }
    }

    function renderFlowCfgFields(dataObj) {
      renderConfigFields(flowCfgFields, flowCfgCurrentModule, dataObj);
    }

    function renderSupervisorCfgFields(dataObj) {
      renderConfigFields(supCfgFields, supCfgCurrentModule, dataObj);
    }

    function buildPatchJsonFromFields(fieldsContainer, moduleName) {
      if (!moduleName) throw new Error('branche non sélectionnée');
      const patch = {};
      const modulePatch = {};
      const fields = fieldsContainer.querySelectorAll('[data-key]');
      fields.forEach((el) => {
        const key = el.dataset.key;
        const kind = el.dataset.kind;
        const displayFormat = String(el.dataset.format || '').trim();
        if (!key || !kind) return;
        if (kind === 'bool') {
          modulePatch[key] = !!el.checked;
          return;
        }
        if (kind === 'int') {
          modulePatch[key] = parseConfigNumericValue(el.value, kind, displayFormat);
          return;
        }
        if (kind === 'float') {
          modulePatch[key] = parseConfigNumericValue(el.value, kind, displayFormat);
          return;
        }
        const masked = el.dataset.masked === '1';
        const raw = String(el.value ?? '');
        if (masked && raw.length === 0) return;
        modulePatch[key] = raw;
      });
      patch[moduleName] = modulePatch;
      return JSON.stringify(patch);
    }

    function buildFlowCfgPatchJson() {
      return buildPatchJsonFromFields(flowCfgFields, flowCfgCurrentModule);
    }

    function buildSupervisorCfgPatchJson() {
      return buildPatchJsonFromFields(supCfgFields, supCfgCurrentModule);
    }

    async function chargerFlowCfgModule(moduleName) {
      beginFlowCfgLoading('Chargement de la branche distante...');
      const m = nettoyerNomFlowCfg(moduleName);
      try {
        if (!m) {
          renderFlowCfgTitle(cheminFlowCfgCourant());
          resetFlowCfgEditor('Aucune branche sélectionnée.');
          return;
        }
        const res = await fetchFlowRemoteQueued(
          '/api/flowcfg/module?name=' + encodeURIComponent(m),
          { cache: 'no-store' }
        );
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module impossible');
        }
        flowCfgCurrentModule = m;
        flowCfgCurrentData = data.data;
        renderFlowCfgTitle(m);
        renderFlowCfgFields(flowCfgCurrentData);
        flowCfgApplyBtn.disabled = false;
        flowCfgStatus.textContent = data.truncated
          ? 'Branche chargée (tronquée, buffer distant atteint).'
          : 'Branche chargée.';
      } catch (err) {
        renderFlowCfgTitle(cheminFlowCfgCourant());
        resetFlowCfgEditor('Chargement branche échoué: ' + err);
      } finally {
        endFlowCfgLoading();
      }
    }

    async function renderFlowCfgNavigator(forceReloadCurrent) {
      beginFlowCfgLoading('Chargement de la configuration distante...');
      try {
        const currentPath = cheminFlowCfgCourant();
        const node = await chargerFlowCfgChildren(currentPath, !!forceReloadCurrent);
        renderFlowCfgTitle(currentPath);
        renderFlowCfgSections(node, currentPath);
        if (node.hasExact) {
          await chargerFlowCfgModule(currentPath);
          return;
        }
        if (node.children.length > 0) {
          resetFlowCfgEditor('Sélectionnez une section.');
        } else {
          resetFlowCfgEditor('Aucune sous-branche disponible.');
        }
      } finally {
        endFlowCfgLoading();
      }
    }

    async function chargerFlowCfgModules(forceReload) {
      const force = !!forceReload;
      beginFlowCfgLoading('Chargement de la configuration distante...');
      try {
        if (force) {
          flowCfgChildrenCache = {};
        }
        try {
          await chargerFlowCfgChildren(cheminFlowCfgCourant(), force);
        } catch (err) {
          flowCfgPath = [];
          await chargerFlowCfgChildren('', force);
        }
        await renderFlowCfgNavigator(force);
      } catch (err) {
        flowCfgStatus.textContent = 'Chargement des branches échoué: ' + err;
      } finally {
        endFlowCfgLoading();
      }
    }

    async function appliquerFlowCfg() {
      try {
        const patch = buildFlowCfgPatchJson();
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetchFlowRemoteQueued('/api/flowcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error('apply refusé');
        }
        flowCfgStatus.textContent = 'Configuration appliquée sur Flow.IO.';
        await chargerFlowCfgModule(flowCfgCurrentModule);
      } catch (err) {
        flowCfgStatus.textContent = 'Application cfg échouée: ' + err;
      }
    }

    function resetSupervisorCfgEditor(message) {
      supCfgCurrentModule = '';
      supCfgCurrentData = {};
      supCfgFields.innerHTML = '';
      supCfgApplyBtn.disabled = true;
      if (message) {
        supCfgStatus.textContent = message;
      }
    }

    async function chargerSupervisorCfgModule(moduleName) {
      const m = nettoyerNomFlowCfg(moduleName);
      if (!m) {
        resetSupervisorCfgEditor('Aucune branche locale sélectionnée.');
        return;
      }
      try {
        const res = await fetch('/api/supervisorcfg/module?name=' + encodeURIComponent(m), { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module impossible');
        }
        supCfgCurrentModule = m;
        supCfgCurrentData = data.data;
        renderSupervisorCfgFields(supCfgCurrentData);
        supCfgApplyBtn.disabled = false;
        supCfgStatus.textContent = data.truncated
          ? 'Branche locale chargée (tronquée, buffer atteint).'
          : 'Branche locale chargée.';
      } catch (err) {
        resetSupervisorCfgEditor('Chargement branche locale échoué: ' + err);
      }
    }

    async function chargerSupervisorCfgModules(keepCurrentSelection) {
      try {
        const res = await fetch('/api/supervisorcfg/modules', { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || !Array.isArray(data.modules)) {
          throw new Error('liste modules indisponible');
        }

        const modules = data.modules
          .filter((name) => typeof name === 'string' && name.length > 0)
          .map((name) => nettoyerNomFlowCfg(name))
          .filter((name) => name.length > 0)
          .filter((name) => !isConfigPathHidden(name))
          .sort((a, b) => a.localeCompare(b));

        supCfgModuleSelect.innerHTML = '';
        if (modules.length === 0) {
          const empty = document.createElement('option');
          empty.value = '';
          empty.textContent = 'Aucune branche locale';
          supCfgModuleSelect.appendChild(empty);
          resetSupervisorCfgEditor('Aucune branche locale disponible.');
          return;
        }

        modules.forEach((name) => {
          const opt = document.createElement('option');
          opt.value = name;
          opt.textContent = name;
          supCfgModuleSelect.appendChild(opt);
        });

        const current = keepCurrentSelection ? nettoyerNomFlowCfg(supCfgCurrentModule) : '';
        const selected = modules.includes(current) ? current : modules[0];
        supCfgModuleSelect.value = selected;
        await chargerSupervisorCfgModule(selected);
      } catch (err) {
        resetSupervisorCfgEditor('Chargement des branches locales échoué: ' + err);
      }
    }

    async function appliquerSupervisorCfg() {
      try {
        const patch = buildSupervisorCfgPatchJson();
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetch('/api/supervisorcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error('apply refusé');
        }
        supCfgStatus.textContent = 'Configuration locale appliquée.';
        await chargerSupervisorCfgModule(supCfgCurrentModule);
      } catch (err) {
        supCfgStatus.textContent = 'Application cfg locale échouée: ' + err;
      }
    }

    async function callSystemAction(target, action) {
      let endpoint = '/api/system/reboot';
      if (target === 'flow' && action === 'reboot') endpoint = '/api/flow/system/reboot';
      else if (target === 'flow' && action === 'factory_reset') endpoint = '/api/flow/system/factory-reset';
      else if (target === 'supervisor' && action === 'factory_reset') endpoint = '/api/system/factory-reset';
      const res = (target === 'flow')
        ? await fetchFlowRemoteQueued(endpoint, { method: 'POST' })
        : await fetch(endpoint, { method: 'POST' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec action');
      if (target === 'flow' && action === 'factory_reset') {
        systemStatusText.textContent = 'Réinitialisation usine de Flow.IO lancée. Redémarrage en cours...';
      } else if (target === 'flow' && action === 'reboot') {
        systemStatusText.textContent = 'Redémarrage de Flow.IO lancé...';
      } else if (target === 'supervisor' && action === 'factory_reset') {
        systemStatusText.textContent = 'Réinitialisation usine du Superviseur lancée. Redémarrage en cours...';
      } else {
        systemStatusText.textContent = 'Redémarrage du Superviseur lancé...';
      }
    }

    saveCfgBtn.addEventListener('click', async () => {
      try {
        await saveUpgradeConfig();
      } catch (err) {
        setUpgradeMessage('Échec de l\'enregistrement : ' + err);
      }
    });
    flowStatusRefreshBtn.addEventListener('click', async () => {
      await refreshFlowStatus(true);
    });
    upSupervisorBtn.addEventListener('click', () => startUpgrade('supervisor'));
    upFlowBtn.addEventListener('click', () => startUpgrade('flowio'));
    upNextionBtn.addEventListener('click', () => startUpgrade('nextion'));
    upCfgdocsBtn.addEventListener('click', () => startUpgrade('cfgdocs'));
    refreshStateBtn.addEventListener('click', refreshUpgradeStatus);
    if (toggleWifiPassBtn && wifiPass) {
      mettreAJourEtatVisibiliteMotDePasse(
        wifiPass,
        toggleWifiPassBtn,
        'Afficher le mot de passe WiFi',
        'Masquer le mot de passe WiFi'
      );
      toggleWifiPassBtn.addEventListener('click', () => {
        basculerVisibiliteMotDePasse(
          wifiPass,
          toggleWifiPassBtn,
          'Afficher le mot de passe WiFi',
          'Masquer le mot de passe WiFi'
        );
      });
    }
    wifiSsidList.addEventListener('change', () => {
      const picked = (wifiSsidList.value || '').trim();
      if (picked.length > 0) {
        wifiSsid.value = picked;
      }
    });
    scanWifiBtn.addEventListener('click', () => {
      refreshWifiScanStatus(true);
    });
    applyWifiCfgBtn.addEventListener('click', async () => {
      try {
        await saveWifiConfig();
      } catch (err) {
        wifiConfigStatus.textContent = 'Application WiFi échouée: ' + err;
      }
    });
    rebootSupervisorBtn.addEventListener('click', async () => {
      try {
        await callSystemAction('supervisor', 'reboot');
      } catch (err) {
        systemStatusText.textContent = 'Redémarrage du Superviseur échoué : ' + err;
      }
    });
    rebootFlowBtn.addEventListener('click', async () => {
      try {
        await callSystemAction('flow', 'reboot');
      } catch (err) {
        systemStatusText.textContent = 'Redémarrage de Flow.IO échoué : ' + err;
      }
    });
    flowFactoryResetBtn.addEventListener('click', async () => {
      if (!confirm('Confirmer la réinitialisation usine de Flow.IO ? Cette action efface la configuration distante.')) return;
      try {
        await callSystemAction('flow', 'factory_reset');
      } catch (err) {
        systemStatusText.textContent = 'Réinitialisation usine de Flow.IO échouée : ' + err;
      }
    });
    flowCfgRefreshBtn.addEventListener('click', async () => {
      await chargerFlowCfgModules(true);
    });
    flowCfgApplyBtn.addEventListener('click', async () => {
      await appliquerFlowCfg();
    });
    supCfgRefreshBtn.addEventListener('click', async () => {
      await chargerSupervisorCfgModules(true);
    });
    supCfgModuleSelect.addEventListener('change', async () => {
      const selected = nettoyerNomFlowCfg(supCfgModuleSelect.value);
      await chargerSupervisorCfgModule(selected);
    });
    supCfgApplyBtn.addEventListener('click', async () => {
      await appliquerSupervisorCfg();
    });

    document.addEventListener('click', (event) => {
      if (!flowCfgTitle.contains(event.target)) {
        closeFlowCfgCrumbMenus();
      }
    });
    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') {
        closeFlowCfgCrumbMenus();
      }
    });
    document.addEventListener('visibilitychange', () => {
      const active = document.querySelector('.page.active');
      const onUpgradePage = !!active && active.id === 'page-upgrade';
      const onTerminalPage = !!active && active.id === 'page-terminal';
      if (document.hidden || !onUpgradePage) {
        stopUpgradeStatusPolling();
      } else {
        startUpgradeStatusPolling();
      }
      if (document.hidden || !onTerminalPage) {
        closeLogSocket();
        setWsStatusText('inactif');
      } else if (terminalActive) {
        connectLogSocket();
      }
    });

    setUpgradeProgress(0);
    {
      const activePage = document.querySelector('.page.active');
      if (activePage && activePage.id) {
        showPage(activePage.id);
      } else {
        showPage('page-status');
      }
    }
