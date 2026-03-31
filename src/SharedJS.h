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
)rawliteral";
