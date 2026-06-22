/* SmartRoute API Tester — app.js */
'use strict';

// ─── 전역 상태 ────────────────────────────────────────────────────────────────
let TOKEN      = null;
let autoTimer  = null;
let netIfaceCount = 0;   // 네트워크 설정 화면의 인터페이스 수

// ─── 내비게이션 ───────────────────────────────────────────────────────────────
function show(name) {
  document.querySelectorAll('.section').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('#sidebar button').forEach(el => el.classList.remove('active'));
  document.getElementById('sec-' + name).classList.add('active');
  event.target.classList.add('active');

  // Settings 섹션 진입 시 현재 설정 자동 로드
  if (name === 'config-system')  loadConfigInto('system');
  if (name === 'config-network') loadConfigInto('network');
  if (name === 'config-serial')  loadConfigInto('serial');
}

// 프로그래밍 방식으로 섹션 전환 (이벤트 없이 호출 가능)
function navTo(name) {
  document.querySelectorAll('.section').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('#sidebar button').forEach(btn => {
    const attr = btn.getAttribute('onclick') || '';
    btn.classList.toggle('active', attr.includes(`'${name}'`));
  });
  document.getElementById('sec-' + name).classList.add('active');
}

// ─── 기본 fetch 래퍼 ─────────────────────────────────────────────────────────
function baseUrl() {
  const host = document.getElementById('cfg-host').value.trim();
  const port = document.getElementById('cfg-port').value.trim();
  return `http://${host}:${port}`;
}

async function request(method, path, body, resultId) {
  const headers = { 'Content-Type': 'application/json' };
  if (TOKEN) headers['Authorization'] = `Bearer ${TOKEN}`;

  const opts = { method, headers };
  if (body !== null && body !== undefined) opts.body = JSON.stringify(body);

  const box = resultId ? document.getElementById(resultId) : null;
  if (box) {
    box.style.display = 'block';
    box.className = 'result-box';
    box.textContent = '요청 중…';
  }

  try {
    const resp = await fetch(baseUrl() + path, opts);
    let data;
    const ct = resp.headers.get('content-type') || '';
    if (ct.includes('application/json')) {
      data = await resp.json();
    } else {
      data = await resp.text();
    }

    const out = typeof data === 'string' ? data : JSON.stringify(data, null, 2);
    const ok  = resp.ok;

    if (box) {
      box.className = 'result-box ' + (ok ? 'ok' : 'err');
      box.textContent = `HTTP ${resp.status}\n\n${out}`;
    }
    return { ok, status: resp.status, data };
  } catch (e) {
    if (box) {
      box.className = 'result-box err';
      box.textContent = `네트워크 오류: ${e.message}`;
    }
    return { ok: false, status: 0, data: null };
  }
}

// 인증 없이 단순 호출
async function api(method, path, resultId, body) {
  return request(method, path, body || null, resultId);
}

// ─── 인증 ────────────────────────────────────────────────────────────────────
async function doLogin() {
  const username = document.getElementById('login-user').value.trim();
  const password = document.getElementById('login-pass').value;
  const result = await request('POST', '/api/login', { username, password }, 'r-login');
  if (result.ok && result.data && result.data.token) {
    TOKEN = result.data.token;
    updateTokenDisplay();
  }
}

async function doLogout() {
  await request('POST', '/api/logout', null, 'r-logout');
  TOKEN = null;
  updateTokenDisplay();
}

async function doSession() {
  await request('GET', '/api/session', null, 'r-session');
}

function updateTokenDisplay() {
  const disp   = document.getElementById('token-display');
  const status = document.getElementById('login-status');
  if (TOKEN) {
    disp.textContent   = `token: ${TOKEN}`;
    status.textContent = '● 인증됨';
    status.className   = 'ok';
  } else {
    disp.textContent   = 'token: -';
    status.textContent = '● 미인증';
    status.className   = 'off';
  }
}

// ─── Devices ─────────────────────────────────────────────────────────────────
const PROTOCOLS = {
  tcp:    [['modbus_tcp',   'modbus_tcp'],
           ['pclink_sum',   'pclink_sum']],
  serial: [['modbus_rtu',   'modbus_rtu'],
           ['modbus_ascii', 'modbus_ascii'],
           ['pclink',       'pclink'],
           ['pclink_sum',   'pclink_sum']],
};

function toggleConnType() {
  const type = document.getElementById('da-type').value;
  document.getElementById('da-tcp-fields').style.display = (type === 'tcp') ? '' : 'none';
  const sel = document.getElementById('da-protocol');
  sel.innerHTML = (PROTOCOLS[type] || PROTOCOLS.serial)
    .map(([v, l]) => `<option value="${v}">${l}</option>`).join('');
}

function buildDeviceBody() {
  const raw = document.getElementById('da-raw').value.trim();
  if (raw) return JSON.parse(raw);
  const type = document.getElementById('da-type').value;
  const obj = {
    deviceCode:  document.getElementById('da-code').value.trim(),
    name:        document.getElementById('da-name').value.trim(),
    displayName: document.getElementById('da-display').value.trim(),
    connType:    type,
    protocol:    document.getElementById('da-protocol').value,
    byteOrder:   document.getElementById('da-byteorder').value,
    slaveId:     parseInt(document.getElementById('da-slave').value) || 1,
    timeoutMs:   parseInt(document.getElementById('da-timeout').value) || 1000,
    intervalMs:  parseInt(document.getElementById('da-interval').value) || 5000,
    retryCount:  parseInt(document.getElementById('da-retry').value) || 3,
  };
  if (type === 'tcp') {
    obj.ipAddress = document.getElementById('da-ip').value.trim();
    obj.tcpPort   = parseInt(document.getElementById('da-tcp-port').value) || 502;
  }
  return obj;
}

async function doAddDevice() {
  try {
    const body = buildDeviceBody();
    await request('POST', '/api/devices', body, 'r-dev-add');
  } catch (e) {
    showErr('r-dev-add', 'JSON 파싱 오류: ' + e.message);
  }
}

async function doUpdateDevice() {
  const id = document.getElementById('de-id').value.trim();
  if (!id) return alert('Device ID 입력 필요');
  const rawStr = document.getElementById('de-raw').value.trim();
  try {
    const body = rawStr ? JSON.parse(rawStr) : {};
    await request('PUT', `/api/devices/${id}`, body, 'r-dev-put');
  } catch (e) {
    showErr('r-dev-put', 'JSON 파싱 오류: ' + e.message);
  }
}

async function doDeleteDevice() {
  const id = document.getElementById('ddel-id').value.trim();
  if (!id) return alert('Device ID 입력 필요');
  if (!confirm(`장치 ID ${id} 를 삭제합니까?`)) return;
  await request('DELETE', `/api/devices/${id}`, null, 'r-dev-del');
}

// ─── Registers ───────────────────────────────────────────────────────────────
async function doGetRegisters() {
  const id = document.getElementById('rg-id').value.trim();
  if (!id) return alert('Device ID 입력 필요');
  const result = await request('GET', `/api/devices/${id}/registers`, null, 'r-reg-list');
  if (result.ok && result.data) {
    renderRegisterTable(result.data.registers || []);
  }
}

function renderRegisterTable(registers, highlightAddress, highlightType) {
  const tbl = document.getElementById('r-reg-table');
  if (!tbl) return;

  if (registers.length === 0) {
    tbl.innerHTML = '<div style="color:#666; padding:4px">레지스터 없음</div>';
    return;
  }

  let html = `<table class="reg-table">
<thead><tr>
  <th>tagName</th><th>displayName</th><th>address</th><th>type</th>
  <th>length</th><th>unit</th><th>scale</th><th>isSigned</th><th>readOnly</th><th>bitLabels</th>
</tr></thead><tbody>`;

  for (const f of registers) {
    const isTarget = highlightAddress !== undefined
      && f.address === highlightAddress
      && f.type === highlightType;
    html += `<tr${isTarget ? ' class="row-highlight" id="reg-row-highlight"' : ''}>
      <td>${escHtml(f.tagName || '')}</td>
      <td>${escHtml(f.displayName || '')}</td>
      <td>${f.address}</td>
      <td>${escHtml(f.type || '')}</td>
      <td>${f.length}</td>
      <td>${escHtml(f.unit || '')}</td>
      <td>${f.scale}</td>
      <td>${f.isSigned ? '✓' : '-'}</td>
      <td>${f.readOnly ? '✓' : '-'}</td>
      <td>${renderBitLabelsSummary(f.bitLabels)}</td>
    </tr>`;
  }
  html += '</tbody></table>';
  tbl.innerHTML = html;

  if (highlightAddress !== undefined) {
    const row = document.getElementById('reg-row-highlight');
    if (row) row.scrollIntoView({ block: 'center', behavior: 'smooth' });
  }
}

async function navigateToRegister(deviceId, address, type) {
  navTo('registers');
  document.getElementById('rg-id').value = deviceId;
  const result = await request('GET', `/api/devices/${deviceId}/registers`, null, 'r-reg-list');
  if (result.ok && result.data) {
    renderRegisterTable(result.data.registers || [], address, type);
  }
}

async function doAddRegister() {
  const devId = document.getElementById('ra-dev-id').value.trim();
  if (!devId) return alert('Device ID 입력 필요');
  const rawStr = document.getElementById('ra-raw').value.trim();
  try {
    let body;
    if (rawStr) {
      body = JSON.parse(rawStr);
    } else {
      const bitLabelsStr = document.getElementById('ra-bit-labels').value.trim();
      if (bitLabelsStr) {
        try { JSON.parse(bitLabelsStr); } catch (e) { return alert('bitLabels JSON 형식 오류: ' + e.message); }
      }
      body = {
        tagName:     document.getElementById('ra-tag').value.trim(),
        displayName: document.getElementById('ra-display').value.trim(),
        address:     parseInt(document.getElementById('ra-addr').value) || 0,
        type:        document.getElementById('ra-type').value,
        length:      parseInt(document.getElementById('ra-len').value) || 1,
        unit:        document.getElementById('ra-unit').value.trim(),
        scale:       parseFloat(document.getElementById('ra-scale').value) || 1.0,
        isSigned:    document.getElementById('ra-signed').value === 'true',
        readOnly:    document.getElementById('ra-readonly').value === 'true',
        bitLabels:   bitLabelsStr,
      };
    }
    await request('POST', `/api/devices/${devId}/registers`, body, 'r-reg-add');
  } catch (e) {
    showErr('r-reg-add', 'JSON 파싱 오류: ' + e.message);
  }
}

async function doDeleteRegister() {
  const devId  = document.getElementById('rdel-dev-id').value.trim();
  const rawStr = document.getElementById('rdel-raw').value.trim();
  if (!devId) return alert('Device ID 입력 필요');
  try {
    const body = rawStr ? JSON.parse(rawStr) : {};
    await request('DELETE', `/api/devices/${devId}/registers`, body, 'r-reg-del');
  } catch (e) {
    showErr('r-reg-del', 'JSON 파싱 오류: ' + e.message);
  }
}

async function doWriteRegister() {
  const devId   = document.getElementById('rw-dev-id').value.trim();
  const address = parseInt(document.getElementById('rw-addr').value) || 0;
  const type    = document.getElementById('rw-type').value;
  const rawStr  = document.getElementById('rw-values').value.trim();
  if (!devId) return alert('Device ID 입력 필요');
  if (rawStr === '') return alert('rawValues 입력 필요');

  const rawValues = rawStr.split(',').map(v => {
    const n = parseInt(v.trim());
    return n < 0 ? (n & 0xFFFF) : n;
  });

  const body = { address, type, rawValues };
  await request('POST', `/api/devices/${devId}/registers/write`, body, 'r-reg-write');
}

// ─── Realtime ────────────────────────────────────────────────────────────────
async function doRealtime() {
  const result = await request('GET', '/api/registers/realtime', null, null);
  renderRealtime(result);
}

function renderRealtime(result) {
  const meta  = document.getElementById('r-realtime-meta');
  const tbl   = document.getElementById('r-realtime-table');
  const raw   = document.getElementById('r-realtime-raw');

  if (!result.ok || !result.data) {
    raw.style.display = 'block';
    raw.className     = 'result-box err';
    raw.textContent   = result.status === 0
      ? '네트워크 오류'
      : `HTTP ${result.status}\n${JSON.stringify(result.data, null, 2)}`;
    tbl.innerHTML = '';
    meta.textContent = '';
    return;
  }

  raw.style.display = 'none';
  const regs = (result.data.registers || []).slice().sort((a, b) =>
    a.deviceId !== b.deviceId ? a.deviceId - b.deviceId : a.id - b.id);
  meta.textContent = `총 ${regs.length}개  |  갱신: ${new Date().toLocaleTimeString()}`;

  if (regs.length === 0) {
    tbl.innerHTML = '<div style="color:#666; padding:4px">데이터 없음</div>';
    return;
  }

  let html = `<table class="realtime-table">
<thead><tr>
  <th>id</th><th>name</th><th>deviceId</th>
  <th>scaledValue</th><th>비트상태</th><th>unit</th>
  <th>isValid</th><th>outOfRange</th><th>lastUpdated</th><th>error</th>
</tr></thead><tbody>`;

  for (const r of regs) {
    const valClass = !r.isValid ? 'val-err' : r.outOfRange ? 'val-range' : 'val-ok';
    html += `<tr class="clickable" onclick="navigateToRegister(${r.deviceId}, ${r.address}, '${r.sourceType}')" title="클릭 → 장치 ${r.deviceId} 레지스터 상세">
      <td>${r.id}</td>
      <td>${escHtml(r.name)}</td>
      <td>${r.deviceId}</td>
      <td class="${valClass}">${r.bitLabels ? fmtHex(r.rawWord) : fmtScaled(r.scaledValue, r.scale)}</td>
      <td>${renderActiveBits(r.bitLabels, r.rawWord)}</td>
      <td>${escHtml(r.unit || '')}</td>
      <td>${r.isValid ? '✓' : '✗'}</td>
      <td>${r.outOfRange ? '⚠' : '-'}</td>
      <td style="white-space:nowrap">${escHtml(r.lastUpdated || '')}</td>
      <td style="color:#f44747">${escHtml(r.errorMessage || '')}</td>
    </tr>`;
  }
  html += '</tbody></table>';
  tbl.innerHTML = html;
}

function toggleAuto() {
  const btn = document.getElementById('auto-btn');
  if (autoTimer) {
    clearInterval(autoTimer);
    autoTimer = null;
    btn.textContent = '시작';
    btn.classList.remove('warn');
  } else {
    const sec = parseFloat(document.getElementById('auto-interval').value) || 3;
    doRealtime();
    autoTimer = setInterval(doRealtime, sec * 1000);
    btn.textContent = '정지';
    btn.classList.add('warn');
  }
}

// ─── Users ───────────────────────────────────────────────────────────────────
async function doAddUser() {
  const username    = document.getElementById('ua-user').value.trim();
  const displayName = document.getElementById('ua-display').value.trim();
  const password    = document.getElementById('ua-pass').value;
  const role        = document.getElementById('ua-role').value;
  if (!username || !password) return alert('username / password 필요');
  await request('POST', '/api/users', { username, displayName, password, role }, 'r-user-add');
}

async function doDeleteUser() {
  const username = document.getElementById('udel-user').value.trim();
  if (!username) return alert('username 입력 필요');
  if (!confirm(`사용자 '${username}' 을 삭제합니까?`)) return;
  await request('DELETE', `/api/users/${username}`, null, 'r-user-del');
}

// ─── Settings: 공통 ──────────────────────────────────────────────────────────

// GET /api/config 로드 후 해당 섹션 폼 채우기
async function loadConfigInto(section) {
  const result = await request('GET', '/api/config', null, null);
  if (!result.ok || !result.data) return;
  const cfg = result.data;

  if (section === 'system') {
    document.getElementById('cs-hostname').value = cfg.system?.hostname  || '';
    document.getElementById('cs-ntp').value      = cfg.system?.ntpServer || '';

  } else if (section === 'network') {
    const ifaces = cfg.network?.interfaces || [];
    netIfaceCount = ifaces.length;
    document.getElementById('cn-form').innerHTML = buildNetworkCards(ifaces);

  } else if (section === 'serial') {
    const s = cfg.serial || {};
    document.getElementById('csr-device').value = s.device   || '';
    setSelectValue('csr-baud',   String(s.baudRate ?? 9600));
    setSelectValue('csr-data',   String(s.dataBits ?? 8));
    setSelectValue('csr-parity', s.parity   || 'none');
    setSelectValue('csr-stop',   String(s.stopBits ?? 1));
  }
}

// 저장 후 재시작 배너 표시
function showRestartBanner() {
  document.getElementById('restart-banner').style.display = 'flex';
}

// ─── Settings: 시스템 ────────────────────────────────────────────────────────
async function doSaveSystem() {
  const body = {
    hostname:  document.getElementById('cs-hostname').value.trim(),
    ntpServer: document.getElementById('cs-ntp').value.trim(),
  };
  if (!body.hostname) return alert('Hostname을 입력하세요.');

  const result = await request('PUT', '/api/config/system', body, 'r-config-system');
  if (result.ok) showRestartBanner();
}

// ─── Settings: 네트워크 ──────────────────────────────────────────────────────

// 인터페이스 카드 HTML 빌드
function buildNetworkCards(ifaces) {
  return ifaces.map((iface, idx) => {
    const isDhcp = iface.mode === 'dhcp';
    return `
      <div class="panel">
        <div class="panel-title">
          <span>${escHtml(iface.name)}</span>
          <span class="iface-role">${escHtml(iface.role)}</span>
        </div>
        <div class="panel-body">
          <div class="field-row">
            <label>활성화</label>
            <select id="cn-enabled-${idx}">
              <option value="true"  ${iface.enabled  ? 'selected' : ''}>활성</option>
              <option value="false" ${!iface.enabled ? 'selected' : ''}>비활성</option>
            </select>
          </div>
          <div class="field-row">
            <label>Mode</label>
            <select id="cn-mode-${idx}" onchange="toggleStaticFields(${idx})">
              <option value="static" ${!isDhcp ? 'selected' : ''}>static</option>
              <option value="dhcp"   ${isDhcp  ? 'selected' : ''}>dhcp</option>
            </select>
          </div>
          <div id="cn-static-${idx}" ${isDhcp ? 'style="display:none"' : ''}>
            <div class="field-row"><label>IP 주소</label><input id="cn-ip-${idx}"      value="${escHtml(iface.ipAddress || '')}"></div>
            <div class="field-row"><label>Netmask</label><input id="cn-netmask-${idx}" value="${escHtml(iface.netmask   || '')}"></div>
            <div class="field-row"><label>Gateway</label><input id="cn-gw-${idx}"      value="${escHtml(iface.gateway   || '')}"></div>
            <div class="field-row"><label>DNS</label>    <input id="cn-dns-${idx}"     value="${escHtml(iface.dns       || '')}"></div>
          </div>
        </div>
      </div>`;
  }).join('');
}

function toggleStaticFields(idx) {
  const isDhcp = document.getElementById(`cn-mode-${idx}`).value === 'dhcp';
  document.getElementById(`cn-static-${idx}`).style.display = isDhcp ? 'none' : '';
}

async function doSaveNetwork() {
  const interfaces = [];
  for (let i = 0; i < netIfaceCount; i++) {
    const name    = document.getElementById(`cn-form`).querySelectorAll('.panel-title span:first-child')[i]?.textContent || `eth${i}`;
    const role    = document.getElementById(`cn-form`).querySelectorAll('.iface-role')[i]?.textContent || '';
    const enabled = document.getElementById(`cn-enabled-${i}`).value === 'true';
    const mode    = document.getElementById(`cn-mode-${i}`).value;
    const iface   = { name, role, enabled, mode, ipAddress: '', netmask: '', gateway: '', dns: '' };
    if (mode === 'static') {
      iface.ipAddress = document.getElementById(`cn-ip-${i}`).value.trim();
      iface.netmask   = document.getElementById(`cn-netmask-${i}`).value.trim();
      iface.gateway   = document.getElementById(`cn-gw-${i}`).value.trim();
      iface.dns       = document.getElementById(`cn-dns-${i}`).value.trim();
    }
    interfaces.push(iface);
  }

  const result = await request('PUT', '/api/config/network', { interfaces }, 'r-config-network');
  if (result.ok) showRestartBanner();
}

// ─── Settings: 시리얼 ────────────────────────────────────────────────────────
async function doSaveSerial() {
  const body = {
    baudRate: parseInt(document.getElementById('csr-baud').value)   || 9600,
    dataBits: parseInt(document.getElementById('csr-data').value)   || 8,
    parity:   document.getElementById('csr-parity').value,
    stopBits: parseInt(document.getElementById('csr-stop').value)   || 1,
  };
  const result = await request('PUT', '/api/config/serial', body, 'r-config-serial');
  if (result.ok) showRestartBanner();
}

// ─── Settings: 재시작 ────────────────────────────────────────────────────────
async function doRestart() {
  if (!confirm('시스템을 재시작합니까?\n약 5~10초 후 연결이 복구됩니다.')) return;

  const box = document.getElementById('r-restart');
  box.style.display = 'block';
  box.className     = 'result-box';
  box.textContent   = '재시작 요청 중…';

  try {
    await fetch(baseUrl() + '/api/system/restart', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${TOKEN}` },
    });
  } catch (_) {
    // 서버가 즉시 종료되면 fetch가 실패할 수 있음 — 정상
  }

  box.className   = 'result-box ok';
  box.textContent = '재시작 중입니다. 잠시 후 페이지를 새로고침 하세요.';
  document.getElementById('restart-banner').querySelector('.banner-msg').textContent =
    '⏳ 재시작 중… 잠시 후 새로고침 하세요.';
}

// ─── 비트레이블 헬퍼 ──────────────────────────────────────────────────────────
function fmtHex(val) {
  return '0x' + ((val || 0) & 0xFFFF).toString(16).toUpperCase().padStart(4, '0');
}

function fmtScaled(value, scale) {
  const s = Math.abs(scale || 1);
  const places = s > 0 && s < 1 ? Math.max(0, Math.round(-Math.log10(s))) : 0;
  return Number(value).toFixed(places);
}

function renderBitLabelsSummary(bitLabels) {
  if (!bitLabels) return '<span style="color:#666">-</span>';
  try {
    const parsed = JSON.parse(bitLabels);
    const keys = Object.keys(parsed);
    if (keys.length === 0) return '<span style="color:#666">-</span>';
    const preview = keys.slice(0, 2).map(k => `${k}:${parsed[k]}`).join(', ');
    const suffix  = keys.length > 2 ? ` …+${keys.length - 2}` : '';
    return `<span title="${escHtml(bitLabels)}" style="color:#9cdcfe">${escHtml(preview)}${suffix}</span>`;
  } catch {
    return `<span style="color:#f44747" title="JSON 오류">오류</span>`;
  }
}

function renderActiveBits(bitLabels, rawWord) {
  if (!bitLabels) return '<span style="color:#666">-</span>';
  try {
    const labels = JSON.parse(bitLabels);
    if (Object.keys(labels).length === 0) return '<span style="color:#666">-</span>';
    const raw = rawWord || 0;
    const parts = [];
    for (const [bitStr, label] of Object.entries(labels)) {
      const isOn = !!((raw >> parseInt(bitStr)) & 1);
      parts.push(`<span class="bit-badge${isOn ? ' on' : ''}">${escHtml(label)}</span>`);
    }
    return parts.join('');
  } catch {
    return '<span style="color:#666">-</span>';
  }
}

// ─── 유틸 ────────────────────────────────────────────────────────────────────
function showErr(id, msg) {
  const box = document.getElementById(id);
  box.style.display = 'block';
  box.className     = 'result-box err';
  box.textContent   = msg;
}

function escHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

function setSelectValue(id, value) {
  const sel = document.getElementById(id);
  if (!sel) return;
  for (const opt of sel.options) {
    if (opt.value === value) { sel.value = value; return; }
  }
}
