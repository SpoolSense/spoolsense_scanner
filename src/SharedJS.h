#pragma once

// Shared JavaScript utilities for the SpoolSense Scanner multi-page web UI.
// Served at GET /js/shared.js — referenced by all pages.

const char SHARED_JS[] PROGMEM = R"rawliteral(
/* ---- Nav bar highlight ---- */
(function(){
  var path = location.pathname;
  document.querySelectorAll('nav a[href]').forEach(function(a){
    if(a.getAttribute('href') === path) a.classList.add('active');
  });
})();

/* ---- API helper ---- */
function api(url, options) {
  return fetch(url, options).then(function(res){
    return res.json().then(function(data){
      if(!res.ok || (data && data.success === false))
        throw new Error((data && data.error) ? data.error : ('HTTP ' + res.status));
      return data;
    });
  });
}

function sleep(ms) {
  return new Promise(function(resolve){ setTimeout(resolve, ms); });
}

function nearlyEqual(a, b, tolerance) {
  return Math.abs(Number(a) - Number(b)) < tolerance;
}

/* ---- Color helpers ---- */
function normalizeHex(value) {
  var v = String(value || '').trim().toUpperCase();
  if (!v) return '#FF0000';
  if (!v.startsWith('#')) v = '#' + v;
  if (/^#[0-9A-F]{6}$/.test(v)) return v;
  return null;
}

function syncColorPicker(pickerId, hexId) {
  var picker = document.getElementById(pickerId);
  var hex = document.getElementById(hexId);
  if (!picker || !hex) return;

  picker.addEventListener('input', function(){
    hex.value = picker.value.toUpperCase();
  });
  hex.addEventListener('input', function(){
    var valid = normalizeHex(hex.value);
    if (valid) { hex.value = valid; picker.value = valid; }
  });
  hex.addEventListener('blur', function(){
    var valid = normalizeHex(hex.value);
    if (valid) { hex.value = valid; picker.value = valid; }
  });
}

/* ---- Form helpers ---- */
function readString(id) {
  var raw = document.getElementById(id).value.trim();
  return raw === '' ? undefined : raw;
}

function readPositiveNumber(id) {
  var raw = document.getElementById(id).value.trim();
  if (raw === '') return undefined;
  var num = Number(raw);
  if (!Number.isFinite(num) || num <= 0) return undefined;
  return num;
}

function readRequiredNumber(id) {
  var raw = document.getElementById(id).value.trim();
  var num = Number(raw);
  if (!Number.isFinite(num)) throw new Error('Invalid number for ' + id);
  return num;
}

function maybeSetValue(id, value) {
  if (value === undefined || value === null) return;
  var el = document.getElementById(id);
  if (el) el.value = value;
}

/* ---- Step indicators ---- */
function setStepState(id, state) {
  var el = document.getElementById(id);
  if (!el) return;
  el.classList.remove('active', 'done', 'error');
  if (state) el.classList.add(state);
}

function resetAllSteps(stepIds) {
  stepIds.forEach(function(id){ setStepState(id, ''); });
}

function setBanner(id, text) {
  var el = document.getElementById(id);
  if (el) el.textContent = text;
}

function setResult(id, text, type) {
  var el = document.getElementById(id);
  if (!el) return;
  el.textContent = text;
  el.className = 'result' + (type ? ' ' + type : '');
}

/* ---- Advanced toggle ---- */
function setupAdvancedToggle(toggleId, boxId) {
  var toggle = document.getElementById(toggleId);
  var box = document.getElementById(boxId);
  if (!toggle || !box) return;
  var textSpan = toggle.querySelector('[data-toggle-text]');

  function set(open) {
    box.classList.toggle('hidden', !open);
    toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
    if (textSpan) textSpan.textContent = open ? 'Hide' : 'Show';
  }

  toggle.addEventListener('click', function(){
    set(box.classList.contains('hidden'));
  });

  return { open: function(){ set(true); }, close: function(){ set(false); } };
}

/* ---- Material data auto-fill ---- */

// Hardcoded fallback — works offline when API is unreachable
var _materialFallback = {
  'PLA':  {extruder_temp:240, bed_temp:55,  density:1.24},
  'PETG': {extruder_temp:270, bed_temp:75,  density:1.27},
  'ABS':  {extruder_temp:280, bed_temp:90,  density:1.04},
  'ASA':  {extruder_temp:280, bed_temp:100, density:1.05},
  'TPU':  {extruder_temp:250, bed_temp:40,  density:1.21},
  'PC':   {extruder_temp:290, bed_temp:115, density:1.3},
  'PCTG': {extruder_temp:270, bed_temp:75,  density:1.21},
  'PP':   {extruder_temp:250, bed_temp:60,  density:0.9},
  'HIPS': {extruder_temp:270, bed_temp:95,  density:1.03},
  'PVA':  {extruder_temp:240, bed_temp:60,  density:1.23},
  'PEEK': {extruder_temp:410, bed_temp:145, density:1.32},
  'PA6':  {extruder_temp:300, bed_temp:45,  density:1.52},
  'PA12': {extruder_temp:300, bed_temp:45,  density:1.52},
  'PEI':  {extruder_temp:380, bed_temp:130, density:1.27},
  'CPE':  {extruder_temp:270, bed_temp:80,  density:1.25}
};
var _materialDb = {};
var _materialDbLoaded = false;

function loadMaterialDb() {
  if (_materialDbLoaded) return Promise.resolve(_materialDb);
  // Start with fallback data
  Object.keys(_materialFallback).forEach(function(k) {
    _materialDb[k] = _materialFallback[k];
  });
  return fetch('https://raw.githubusercontent.com/TigerTag-Project/TigerTag-RFID-Guide/main/database/id_material.json')
    .then(function(r) { return r.ok ? r.json() : []; })
    .then(function(data) {
      if (Array.isArray(data)) {
        data.forEach(function(m) {
          if (m.label) {
            var r = m.recommended || {};
            _materialDb[m.label.toUpperCase()] = {
              material: m.label,
              extruder_temp: r.nozzleTempMax,
              bed_temp: r.bedTempMax,
              density: m.density,
              minPrintTemp: r.nozzleTempMin,
              maxPrintTemp: r.nozzleTempMax,
              minBedTemp: r.bedTempMin,
              maxBedTemp: r.bedTempMax,
              dryTemp: r.dryTemp,
              dryTime: r.dryTime
            };
          }
        });
      }
      _materialDbLoaded = true;
      return _materialDb;
    })
    .catch(function() {
      _materialDbLoaded = true;
      return _materialDb;
    });
}

function lookupMaterial(name) {
  if (!name) return null;
  var key = name.toUpperCase().replace(/\s+/g, '');
  // Exact match first
  if (_materialDb[key]) return _materialDb[key];
  // Try with common separators
  var withDash = key.replace(/([A-Z]+)(\d)/, '$1-$2');
  if (_materialDb[withDash]) return _materialDb[withDash];
  // Prefix match (e.g. "PA6 (Nylon 6)" → "PA6")
  var prefix = key.split(/[^A-Z0-9-]/)[0];
  if (prefix && _materialDb[prefix]) return _materialDb[prefix];
  return null;
}

// Track which fields the user has manually edited
function trackAutoFill(fieldIds) {
  fieldIds.forEach(function(id) {
    var el = document.getElementById(id);
    if (!el) return;
    el.dataset.autoFilled = 'true';
    el.addEventListener('input', function() {
      el.dataset.autoFilled = 'false';
    });
    el.addEventListener('change', function() {
      if (el.value === '') el.dataset.autoFilled = 'true';
    });
  });
}

function _setOrClear(fieldMap, key, value) {
  if (!fieldMap[key]) return;
  var el = document.getElementById(fieldMap[key]);
  if (!el || el.dataset.autoFilled === 'false') return;
  el.value = (value !== undefined && value !== null) ? value : '';
  el.dataset.autoFilled = 'true';
}

function autoFillMaterialData(materialName, fieldMap) {
  var m = lookupMaterial(materialName);
  if (!m) return;
  _setOrClear(fieldMap, 'minPrintTemp', m.minPrintTemp);
  _setOrClear(fieldMap, 'maxPrintTemp', m.maxPrintTemp);
  _setOrClear(fieldMap, 'minBedTemp', m.minBedTemp);
  _setOrClear(fieldMap, 'maxBedTemp', m.maxBedTemp);
  _setOrClear(fieldMap, 'density', m.density);
  _setOrClear(fieldMap, 'dryTemp', m.dryTemp);
  _setOrClear(fieldMap, 'dryTime', m.dryTime);
}

/* ---- Tag kind labels ---- */
var TAG_KIND_LABELS = {
  'OpenPrintTag': 'OpenPrintTag',
  'GenericUidTag': 'Generic UID Tag',
  'TigerTag': 'TigerTag',
  'BambuTag': 'Bambu Lab Tag',
  'BlankTag': 'Blank Tag',
  'OpenTag3D': 'OpenTag3D',
  'Unsupported': 'Unsupported'
};

function tagKindLabel(kind) {
  return TAG_KIND_LABELS[kind] || kind || 'Unknown';
}

/* ---- Tag data pre-fill for writer pages ---- */

function normalizeTagData(s) {
  if (!s || !s.present) return null;
  var d = {};
  var kind = s.tag_kind || '';

  if (kind === 'TigerTag' && s.tigertag) {
    var t = s.tigertag;
    d.material = t.material_name || '';
    d.color = t.color_hex || '';
    d.manufacturer = t.brand_name || '';
    d.weight = t.weight_g || 0;
    d.diameter = t.diameter_mm || 0;
    d.nozzle_min = t.nozzle_temp_min || 0;
    d.nozzle_max = t.nozzle_temp_max || 0;
    d.bed_min = t.bed_temp_min || 0;
    d.bed_max = t.bed_temp_max || 0;
    d.dry_temp = t.dry_temp || 0;
    d.dry_time = t.dry_time_hours || 0;
    d.tigertag_material_id = t.material_id;
    d.tigertag_brand_id = t.brand_id;
  } else if (kind === 'OpenTag3D' && s.opentag3d) {
    var o = s.opentag3d;
    d.material = o.base_material || '';
    d.modifiers = o.modifiers || '';
    d.color = o.color_hex || '';
    d.manufacturer = o.manufacturer || '';
    d.weight = o.target_weight_g || 0;
    d.diameter = o.diameter_mm || 0;
    d.density = o.density || 0;
    d.nozzle_min = o.min_print_temp || o.print_temp || 0;
    d.nozzle_max = o.max_print_temp || o.print_temp || 0;
    d.bed_min = o.min_bed_temp || o.bed_temp || 0;
    d.bed_max = o.max_bed_temp || o.bed_temp || 0;
    d.dry_temp = o.dry_temp || 0;
    d.dry_time = o.dry_time_hours || 0;
  } else if (s.tag_data_valid) {
    // OpenPrintTag
    d.material = s.material_name || '';
    d.material_type = s.material_type;
    d.color = s.color || '';
    d.manufacturer = s.manufacturer || '';
    d.weight = s.initial_weight_g || 0;
    d.remaining = s.remaining_g || 0;
    d.density = s.density || 0;
    d.diameter = s.diameter_mm || 0;
    d.nozzle_min = s.min_print_temp || 0;
    d.nozzle_max = s.max_print_temp || 0;
    d.bed_min = s.min_bed_temp || 0;
    d.bed_max = s.max_bed_temp || 0;
    d.preheat = s.preheat_temp || 0;
    d.spoolman_id = s.spoolman_id || 0;
  } else if (s.material_name) {
    // NFC+ with Spoolman data
    d.material = s.material_name || '';
    d.color = s.color || '';
    d.manufacturer = s.manufacturer || '';
    d.remaining = s.remaining_g || 0;
    d.nozzle_min = s.extruder_temp || 0;
    d.nozzle_max = s.extruder_temp || 0;
    d.bed_min = s.bed_temp || 0;
    d.bed_max = s.bed_temp || 0;
  } else {
    return null;
  }

  return d;
}

function prefillFromTag(fieldMap) {
  return api('/api/status').then(function(s) {
    var d = normalizeTagData(s);
    if (!d) return null;

    function fill(key, value) {
      if (!fieldMap[key] || value === undefined || value === null || value === 0 || value === '') return;
      var el = document.getElementById(fieldMap[key]);
      if (!el) return;
      if (el.dataset.autoFilled === 'false') return; // user already edited
      el.value = value;
      el.dataset.autoFilled = 'true';
    }

    fill('material', d.material);
    fill('manufacturer', d.manufacturer);
    fill('weight', d.weight);
    fill('remaining', d.remaining);
    fill('density', d.density);
    fill('diameter', d.diameter);
    fill('nozzle_min', d.nozzle_min);
    fill('nozzle_max', d.nozzle_max);
    fill('bed_min', d.bed_min);
    fill('bed_max', d.bed_max);
    fill('dry_temp', d.dry_temp);
    fill('dry_time', d.dry_time);
    fill('preheat', d.preheat);

    // Color — sync both hex input and color picker
    if (d.color && fieldMap.color) {
      var hexEl = document.getElementById(fieldMap.color);
      if (hexEl && hexEl.dataset.autoFilled !== 'false') {
        var c = d.color.charAt(0) === '#' ? d.color : '#' + d.color;
        hexEl.value = c.toUpperCase();
        hexEl.dataset.autoFilled = 'true';
        if (fieldMap.colorPicker) {
          var picker = document.getElementById(fieldMap.colorPicker);
          if (picker) picker.value = c;
        }
      }
    }

    return d;
  }).catch(function() { return null; });
}

var _spoolmanPickerSpools = [];
var _spoolmanPickerFieldMap = {};
var _selectedSpoolId = -1;

function renderSpoolmanPicker(containerId, fieldMap) {
  var container = document.getElementById(containerId);
  if (!container) return;

  fetch('/api/spoolman/spools')
    .then(function(r) { return r.ok ? r.json() : null; })
    .then(function(spools) {
      if (!spools || !Array.isArray(spools)) {
        container.innerHTML = '<div style="padding:12px;opacity:0.6;font-size:0.9em">Configure Spoolman in <a href="/config" style="color:var(--accent)">settings</a> to import spool data.</div>';
        return;
      }
      container.innerHTML =
        '<div style="font-weight:600;color:var(--accent);margin-bottom:8px">Import from Spoolman</div>' +
        '<input type="text" id="spoolmanPickerSearch" placeholder="Search spools..." style="width:100%;padding:8px 12px;border-radius:8px;border:1px solid var(--border);background:var(--card);color:var(--text);font-size:0.95em;box-sizing:border-box;margin-bottom:8px" />' +
        '<div id="spoolmanPickerResults" style="max-height:200px;overflow-y:auto;border:1px solid var(--border);border-radius:8px;display:none"></div>';

      document.getElementById('spoolmanPickerSearch').addEventListener('input', function() {
        var results = document.getElementById('spoolmanPickerResults');
        if (this.value.length > 0) {
          results.style.display = '';
          filterSpoolmanPicker(spools, this.value, fieldMap);
        } else {
          results.style.display = 'none';
        }
      });
    })
    .catch(function() {
      container.innerHTML = '<div style="padding:12px;opacity:0.6;font-size:0.9em">Configure Spoolman in <a href="/config" style="color:var(--accent)">settings</a> to import spool data.</div>';
    });
}

function filterSpoolmanPicker(spools, query, fieldMap) {
  _spoolmanPickerSpools = spools;
  _spoolmanPickerFieldMap = fieldMap;
  var results = document.getElementById('spoolmanPickerResults');
  if (!results) return;
  var q = query.toLowerCase();
  var filtered = spools.filter(function(s) {
    var fil = s.filament || {};
    var vendor = fil.vendor ? fil.vendor.name : '';
    var text = (vendor + ' ' + (fil.material || '') + ' ' + (fil.name || '')).toLowerCase();
    return !q || text.indexOf(q) >= 0;
  });
  if (filtered.length === 0) {
    results.innerHTML = '<div style="padding:12px;opacity:0.5;text-align:center">No spools found</div>';
    return;
  }
  results.innerHTML = filtered.slice(0, 20).map(function(spool) {
    var fil = spool.filament || {};
    var vendor = fil.vendor ? fil.vendor.name : '';
    var esc = function(s) { var d = document.createElement('div'); d.textContent = s; return d.innerHTML; };
    var color = (fil.color_hex || '').replace(/[^0-9a-fA-F]/g, '');
    var swatch = color ? '<span style="display:inline-block;width:14px;height:14px;border-radius:50%;background:#' + color + ';vertical-align:middle;margin-right:6px"></span>' : '';
    var remaining = spool.remaining_weight ? Math.round(spool.remaining_weight) + 'g' : '?';
    var spoolId = Number(spool.id);
    if (!isFinite(spoolId) || spoolId <= 0) return '';
    return '<div style="padding:8px 10px;border-bottom:1px solid var(--border);cursor:pointer" onclick="selectSpoolmanSpool(' + spoolId + ')">'
      + swatch + '#' + spoolId + ' ' + (vendor ? esc(vendor) + ' ' : '') + esc(fil.material || fil.name || '?') + ' \u2014 ' + remaining
      + '</div>';
  }).join('');
}

function selectSpoolmanSpool(spoolId) {
  _selectedSpoolId = spoolId;
  var spool = _spoolmanPickerSpools.find(function(s) { return s.id === spoolId; });
  if (spool) fillFromSpoolman(spool, _spoolmanPickerFieldMap);
  renderLinkOnlyBar(spoolId);
}

// "Link tag (no write)" — bind an already-written tag to the selected spool
// without touching the tag. Arms the firmware pending link and reports the
// outcome by polling: armed=false with time left means a sync consumed it.
function renderLinkOnlyBar(spoolId) {
  spoolId = Number(spoolId);
  if (!isFinite(spoolId) || spoolId <= 0) return;
  var results = document.getElementById('spoolmanPickerResults');
  if (!results) return;
  var bar = document.getElementById('linkOnlyBar');
  if (!bar) {
    bar = document.createElement('div');
    bar.id = 'linkOnlyBar';
    bar.style.cssText = 'margin-top:8px;padding:10px;border:1px solid var(--border);border-radius:8px;font-size:0.92em';
    results.parentNode.insertBefore(bar, results.nextSibling);
  }
  bar.innerHTML = 'Spool #' + spoolId + ' selected \u2014 '
    + '<button type="button" onclick="linkTagOnly(' + spoolId + ')" '
    + 'style="padding:6px 12px;border-radius:8px;border:1px solid var(--accent);background:transparent;color:var(--accent);cursor:pointer">'
    + 'Link tag (no write)</button>'
    + '<div id="linkOnlyStatus" style="margin-top:6px;opacity:0.75"></div>';
}

async function linkTagOnly(spoolId) {
  spoolId = Number(spoolId);
  if (!isFinite(spoolId) || spoolId <= 0) return;
  var status = document.getElementById('linkOnlyStatus');
  try {
    await api('/api/spoolman/pending-link', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({spool_id: spoolId})
    });
  } catch (e) {
    if (status) status.textContent = 'Could not arm the link \u2014 is the scanner reachable?';
    return;
  }
  _selectedSpoolId = -1;  // consumed by this flow — do not re-arm on Write Tag
  var deadline = Date.now() + 135000;
  while (Date.now() < deadline) {
    var st = null;
    try { st = await api('/api/spoolman/pending-link'); } catch (e) {}
    if (st && st.state === 'consumed' && st.spool_id === spoolId) {
      if (status) status.textContent = st.link_ok
        ? ('\u2713 Tag ' + (st.uid || '') + ' linked to spool #' + spoolId + '.')
        : ('\u2717 Link failed \u2014 Spoolman rejected the update. Try again.');
      return;
    }
    if (st && (st.state === 'expired' || st.state === 'idle')) {
      if (status) status.textContent = 'Link window expired \u2014 select the spool and try again.';
      return;
    }
    var secs = st && st.remaining_ms ? Math.ceil(st.remaining_ms / 1000) : Math.ceil((deadline - Date.now()) / 1000);
    if (status) status.textContent = 'Place the tag on the reader now (' + secs + 's). '
      + 'If it\u2019s already sitting there, lift it off and set it back down.';
    await sleep(1000);
  }
  if (status) status.textContent = 'Link window expired \u2014 select the spool and try again.';
}

function fillFromSpoolman(spool, fieldMap) {
  var fil = spool.filament || {};
  var vendor = fil.vendor ? fil.vendor.name : '';
  var color = (fil.color_hex || '').replace(/^#/, '');

  function fill(key, value) {
    if (!fieldMap[key] || value === undefined || value === null || value === '' || value === 0) return;
    var el = document.getElementById(fieldMap[key]);
    if (!el) return;
    el.value = value;
    el.dataset.autoFilled = 'true';
  }

  fill('material', fil.material || fil.name || '');
  fill('manufacturer', vendor);

  // Dispatch input events to trigger hidden ID sync handlers
  if (fieldMap.material) {
    var matEl = document.getElementById(fieldMap.material);
    if (matEl) matEl.dispatchEvent(new Event('input', {bubbles: true}));
  }
  if (fieldMap.manufacturer) {
    var mfgEl = document.getElementById(fieldMap.manufacturer);
    if (mfgEl) mfgEl.dispatchEvent(new Event('input', {bubbles: true}));
  }

  fill('remaining', spool.remaining_weight);
  fill('weight', fil.weight);
  fill('density', fil.density);
  fill('spoolman_id', spool.id);

  // Temps
  var extruder = fil.settings_extruder_temp;
  var bed = fil.settings_bed_temp;
  if (extruder) {
    fill('nozzle_min', extruder.min || extruder);
    fill('nozzle_max', extruder.max || extruder);
    fill('nozzle_single', (extruder.min && extruder.max) ? Math.round((extruder.min + extruder.max) / 2) : (extruder.min || extruder));
  }
  if (bed) {
    fill('bed_min', bed.min || bed);
    fill('bed_max', bed.max || bed);
    fill('bed_single', (bed.min && bed.max) ? Math.round((bed.min + bed.max) / 2) : (bed.min || bed));
  }

  // Color
  if (color && fieldMap.color) {
    var hexEl = document.getElementById(fieldMap.color);
    if (hexEl) {
      hexEl.value = '#' + color.toUpperCase();
      hexEl.dataset.autoFilled = 'true';
      if (fieldMap.colorPicker) {
        var picker = document.getElementById(fieldMap.colorPicker);
        if (picker) picker.value = '#' + color;
      }
    }
  }

  // Diameter
  if (fil.diameter && fieldMap.diameter) {
    var el = document.getElementById(fieldMap.diameter);
    if (el) {
      if (el.tagName === 'SELECT') {
        if (fieldMap.diameterUnit === 'um') {
          el.value = fil.diameter < 2.0 ? '1750' : '2850';
        } else {
          el.value = fil.diameter < 2.0 ? '56' : '221';
        }
      } else if (fieldMap.diameterUnit === 'um') {
        el.value = Math.round(fil.diameter * 1000);
      } else {
        el.value = fil.diameter;
      }
      el.dataset.autoFilled = 'true';
    }
  }

  // Feedback
  var search = document.getElementById('spoolmanPickerSearch');
  if (search) search.value = '';
  var results = document.getElementById('spoolmanPickerResults');
  if (results) results.innerHTML = '<div style="padding:12px;color:var(--accent);text-align:center;font-weight:600">Loaded spool #' + spool.id + '</div>';
}

/* ---- Shared write flow ---- */

async function waitForTag(timeoutMs) {
  var deadline = Date.now() + timeoutMs;
  setStepState('step-wait', 'active');
  setBanner('statusBanner', 'Waiting for tag\u2026');
  setResult('resultBox', 'Place and hold a tag on the scanner.', '');

  while (Date.now() < deadline) {
    var status = await api('/api/status');
    if (status.present) {
      setStepState('step-wait', 'done');
      return status;
    }
    await sleep(500);
  }

  setStepState('step-wait', 'error');
  throw new Error('No tag detected. Place the tag on the scanner and try again.');
}

// What one status poll means for a write this page started.
//   'fail'  - the firmware reported THIS request as failed
//   'wait'  - an id was issued but its result is not in yet
//   'check' - run the page's own verify
function writePollVerdict(status, requestId) {
  var w = status && status.write_result;
  var mine = !!(w && requestId && w.request_id === requestId);
  if (mine && w.ok === false) return 'fail';
  if (requestId && !mine) return 'wait';
  return 'check';
}

async function sharedWriteFlow(config) {
  var stepIds = config.stepIds;
  var createView = document.getElementById('createView');
  var statusView = document.getElementById('statusView');
  var backBtn = document.getElementById('backBtn');
  var anotherBtn = document.getElementById('anotherBtn');

  resetAllSteps(stepIds);
  createView.classList.add('hidden');
  statusView.classList.remove('hidden');
  backBtn.classList.add('hidden');
  anotherBtn.classList.add('hidden');

  if (_selectedSpoolId > 0) {
    try {
      await api('/api/spoolman/pending-link', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({spool_id: _selectedSpoolId})
      });
    } catch(e) {}
    _selectedSpoolId = -1;
  }

  try {
    var presentStatus = await waitForTag(8000);

    // The tag is detected here, at submit time — nothing requires the user to
    // press Read first. But a tag can report present before its UID is
    // readable, and writing without one lands on whatever tag is on the
    // scanner. Stop with an explanation rather than sending an unbound write.
    if (!presentStatus.uid) {
      setStepState('step-wait', 'error');
      throw new Error('Tag detected but its ID could not be read. Reposition it on the scanner and try again.');
    }

    setStepState('step-detect', 'active');
    setBanner('statusBanner', 'Tag detected.');
    setResult('resultBox', 'UID: ' + (presentStatus.uid || 'Unknown'), '');
    await sleep(250);
    setStepState('step-detect', 'done');

    if (config.formatCheck && config.formatCheck(presentStatus)) {
      setStepState('step-format', 'active');
      setBanner('statusBanner', 'Formatting tag\u2026');
      setResult('resultBox', 'Preparing blank tag for data.', '');
      var formatResp = await api(config.formatEndpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ uid: presentStatus.uid })
      });
      var formatRequestId = formatResp && formatResp.request_id;
      if (formatRequestId) {
        // The format is only queued; a failed format must not be followed by
        // a write onto an unformatted tag.
        var formatDeadline = Date.now() + 15000;
        var formatDone = false;
        while (Date.now() < formatDeadline) {
          await sleep(500);
          var formatStatus = await api('/api/status?request_id=' + formatRequestId);
          var formatVerdict = writePollVerdict(formatStatus, formatRequestId);
          if (formatVerdict === 'fail') {
            throw new Error(formatStatus.write_result.error ? ('Format failed: ' + formatStatus.write_result.error) : 'Format failed.');
          }
          if (formatVerdict === 'check') { formatDone = true; break; }
        }
        if (!formatDone) {
          throw new Error('Format timed out. Keep the tag on the scanner and try again.');
        }
      }
      setStepState('step-format', 'done');
    }

    var payload = config.buildPayload(presentStatus.uid);

    setStepState('step-write', 'active');
    setBanner('statusBanner', 'Writing ' + config.formatName + ' data\u2026');
    setResult('resultBox', 'Keep the tag on the scanner \u2014 writing and verifying takes a few seconds.', '');
    // Only a queue-acceptance ack \u2014 the actual outcome arrives later via
    // status.write_result, keyed by this request_id.
    var writeResp = await api(config.endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    var requestId = writeResp && writeResp.request_id;

    setStepState('step-verify', 'active');
    setBanner('statusBanner', 'Verifying write\u2026');
    setResult('resultBox', 'Reading tag back to confirm \u2014 keep the tag on the scanner.', '');

    var verifyDeadline = Date.now() + 15000;
    while (Date.now() < verifyDeadline) {
      await sleep(500);
      var status = await api('/api/status' + (requestId ? ('?request_id=' + requestId) : ''));
      // The firmware's own result outranks the page's field comparison: a
      // cached tag can still match after a write that failed half way.
      var verdict = writePollVerdict(status, requestId);
      if (verdict === 'fail') {
        throw new Error(status.write_result.error ? ('Write failed: ' + status.write_result.error) : 'Write failed.');
      }
      if (verdict === 'wait') continue;
      var mine = status.write_result && status.write_result.request_id === requestId;
      if ((status.present || mine) && config.verify(status, payload, requestId)) {
        setStepState('step-write', 'done');
        setBanner('statusBanner', 'Tag verified \u2014 hold for a moment\u2026');
        await sleep(2000);
        setStepState('step-verify', 'done');
        setBanner('statusBanner', 'Write complete \u2014 safe to remove tag.');
        setResult('resultBox', config.formatName + ' tag written and verified successfully.', 'success');
        await sleep(500);
        if (config.afterSuccess) await config.afterSuccess(presentStatus.uid);
        backBtn.classList.remove('hidden');
        anotherBtn.classList.remove('hidden');
        return;
      }
    }

    setStepState('step-verify', 'error');
    throw new Error('Verification timed out. Keep the tag on the scanner and try again.');
  } catch (err) {
    var msg = err && err.message ? err.message : 'Write failed';
    setBanner('statusBanner', 'Write failed.');
    setResult('resultBox', msg, 'error');

    stepIds.forEach(function(id) {
      var el = document.getElementById(id);
      if (el && el.classList.contains('active')) {
        setStepState(id, 'error');
      }
    });

    backBtn.classList.remove('hidden');
  }
}

/* ---- Shared read button ---- */

function setupReadButton(config) {
  var readBtn = document.getElementById('readBtn');
  var writeBtn = document.getElementById('writeBtn');
  var readWaiting = false;

  function setReadWaiting(active) {
    readWaiting = active;
    // A page may veto Write (the OpenTag3D page does for a read-only tag);
    // ending a read must not override that.
    writeBtn.disabled = active || (config.writeAllowed ? !config.writeAllowed() : false);
    if (active) {
      readBtn.textContent = 'Cancel';
      readBtn.onclick = cancelRead;
      document.getElementById('readPrompt').classList.remove('hidden');
    } else {
      readBtn.textContent = 'Read';
      readBtn.onclick = startRead;
      document.getElementById('readPrompt').classList.add('hidden');
    }
  }

  function cancelRead() {
    readWaiting = false;
    setReadWaiting(false);
  }

  async function startRead() {
    setReadWaiting(true);
    var deadline = Date.now() + 30000;
    var wrongKindSeen = 0;
    while (readWaiting && Date.now() < deadline) {
      try {
        var status = await fetch('/api/status').then(function(r) { return r.json(); });
        if (status.present && status.tag_kind === config.expectedKind) {
          config.fillForm(status);
          if (config.fillEnrichment) config.fillEnrichment(status);
          if (config.showMatchBadge) {
            if (status.spoolman && status.spoolman.spool_id > 0) {
              config.showMatchBadge('Spool #' + status.spoolman.spool_id + ' matched');
            } else {
              config.showMatchBadge('Looking up Spoolman...');
              pollForEnrichment();
            }
          }
          break;
        } else if (status.present) {
          wrongKindSeen++;
          if (wrongKindSeen >= 4) {
            if (config.showMatchBadge) config.showMatchBadge(config.wrongKindMsg);
            break;
          }
        }
      } catch(e) {}
      await new Promise(function(r) { setTimeout(r, 500); });
    }
    setReadWaiting(false);
  }

  var enrichTimer = null;
  function pollForEnrichment() {
    if (enrichTimer) clearInterval(enrichTimer);
    var attempts = 0;
    enrichTimer = setInterval(function() {
      attempts++;
      if (attempts > 8) {
        clearInterval(enrichTimer);
        enrichTimer = null;
        if (config.showMatchBadge) config.showMatchBadge('no Spoolman match');
        return;
      }
      fetch('/api/status').then(function(r) { return r.json(); }).then(function(s) {
        if (s.spoolman && s.spoolman.spool_id > 0) {
          clearInterval(enrichTimer);
          enrichTimer = null;
          if (config.fillEnrichment) config.fillEnrichment(s);
          if (config.showMatchBadge) config.showMatchBadge('Spool #' + s.spoolman.spool_id + ' matched');
        }
      }).catch(function(){});
    }, 1000);
  }

  readBtn.onclick = startRead;
}

/* ---- Shared enrichment helpers ---- */

function enrichmentHasData(fieldIds) {
  return fieldIds.some(function(id) {
    var el = document.getElementById(id);
    return el && el.value && parseFloat(el.value) > 0;
  });
}

function setVal(id, val) {
  var el = document.getElementById(id);
  if (el && val !== undefined && val !== null) el.value = val;
}

async function saveEnrichmentToSpoolman(uid, config) {
  if (!enrichmentHasData(config.enrichmentFieldIds)) return;

  var fields = config.getFields();

  var vendorId = -1;
  if (fields.manufacturer) {
    try {
      var vr = await fetch('/api/spoolman/find-vendor?name=' + encodeURIComponent(fields.manufacturer)).then(function(r) { return r.json(); });
      if (vr.found) {
        var confirmed = confirm('Found existing manufacturer "' + vr.name + '" in Spoolman. Use it?');
        vendorId = confirmed ? vr.id : -2;
      }
    } catch(e) {}
  }

  var filamentId = -1;
  if (vendorId > 0 && fields.material) {
    try {
      var fr = await fetch('/api/spoolman/find-filament?vendor_id=' + vendorId
                       + '&material=' + encodeURIComponent(fields.material)
                       + (fields.colorHex ? '&color_hex=' + encodeURIComponent(fields.colorHex.replace('#','')) : '')).then(function(r) { return r.json(); });
      if (fr.found) {
        var fconfirmed = confirm('Found existing filament "' + fr.name + '" in Spoolman. Use it?');
        filamentId = fconfirmed ? fr.id : -2;
      }
    } catch(e) {}
  }

  try {
    var resp = await fetch('/api/spoolman/save-enrichment', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({
        uid: uid,
        manufacturer: fields.manufacturer,
        material: fields.material,
        color_hex: (fields.colorHex || '').replace('#', ''),
        remaining_g: fields.remainingG || 0,
        bed_temp: fields.bedTemp || 0,
        nozzle_temp: fields.nozzleTemp || 0,
        diameter_mm: fields.diameterMm || 1.75,
        density: fields.density || 0,
        vendor_id: vendorId,
        filament_id: filamentId,
        tag_format: config.tagFormat || ''
      })
    });
    var result = await resp.json();
    if (!resp.ok || result.success === false) {
      throw new Error(result.error || 'save failed');
    }
    setBanner('statusBanner', 'Tag written \u2713 Spoolman enrichment saved \u2713');
  } catch(e) {
    setBanner('statusBanner', 'Tag written \u2713 Spoolman save failed \u2014 check connection');
  }
}
)rawliteral";
