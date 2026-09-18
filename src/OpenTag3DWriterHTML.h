#pragma once

// OpenTag3D writer page served at GET /writer/opentag3d
// Writes NDEF-wrapped OpenTag3D payload to NTAG213/215/216 via POST /api/write-opentag3d.

const char OPENTAG3D_WRITER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>OpenTag3D Writer &mdash; SpoolSense</title>
  <link rel="stylesheet" href="/css/shared.css?v=)rawliteral" FIRMWARE_VERSION R"rawliteral(" />
</head>
<body>
  <div class="wrap">
    <nav>
      <span class="nav-brand">SpoolSense</span>
      <a href="/">Home</a>
      <a href="/reader">Reader</a>
      <a href="/writer/openprinttag">OpenPrintTag</a>
      <a href="/writer/tigertag">TigerTag</a>
      <a href="/writer/opentag3d">OpenTag3D</a>
      <a href="/writer/openspool">OpenSpool</a>
      <a href="/register/uid">NFC+</a>
      <a href="/update">Update</a>
      <a href="/troubleshooting">Troubleshooting</a>
      <a href="/config">Config</a>
    </nav>

    <section class="card" id="createView">
      <div class="card-head">
        <div>
          <h1 class="card-title">Create OpenTag3D</h1>
          <p class="card-subtitle">Write filament data to an NTAG215/216 tag in OpenTag3D format.</p>
        </div>
      </div>

      <div class="card-body">
        <div id="spoolmanPicker" style="background:var(--card-alt,#1e1e35);border:1px solid var(--border);border-radius:8px;padding:12px;margin-bottom:16px"></div>
        <form id="writerForm">
          <section>
            <h2 class="section-title">Filament</h2>
            <div class="grid-2">
              <div class="field">
                <label for="base_material">Material</label>
                <input id="base_material" list="material-list" placeholder="Type to search materials" required value="PLA" />
                <datalist id="material-list">
                  <option value="PLA"></option>
                  <option value="PETG"></option>
                  <option value="TPU"></option>
                  <option value="ABS"></option>
                  <option value="ASA"></option>
                  <option value="PC"></option>
                  <option value="PCTG"></option>
                  <option value="PP"></option>
                  <option value="PA6"></option>
                  <option value="PA12"></option>
                  <option value="PA66"></option>
                  <option value="CPE"></option>
                  <option value="TPE"></option>
                  <option value="HIPS"></option>
                  <option value="PET"></option>
                  <option value="PEI"></option>
                  <option value="PVA"></option>
                  <option value="PVB"></option>
                  <option value="PEEK"></option>
                  <option value="PEKK"></option>
                </datalist>
              </div>
              <div class="field">
                <label for="material_modifiers">Modifiers</label>
                <select id="material_modifiers">
                  <option value="" selected>None</option>
                  <option value="CF">CF (Carbon Fiber)</option>
                  <option value="GF">GF (Glass Fiber)</option>
                  <option value="HF">HF (High Flow)</option>
                  <option value="HS">HS (High Speed)</option>
                  <option value="HT">HT (High Temp)</option>
                  <option value="Silk">Silk</option>
                  <option value="Matt">Matte</option>
                  <option value="Wood">Wood Fill</option>
                  <option value="Metal">Metal Fill</option>
                  <option value="Glow">Glow in Dark</option>
                </select>
              </div>
            </div>
            <div class="grid-2">
              <div class="field">
                <label for="manufacturer">Brand / Manufacturer</label>
                <input id="manufacturer" list="brand-list" maxlength="16" placeholder="Select or type a brand" />
                <datalist id="brand-list">
                  <option value="3DFuel"></option>
                  <option value="3DXTech"></option>
                  <option value="Amolen"></option>
                  <option value="Anycubic"></option>
                  <option value="AzureFilm"></option>
                  <option value="Bambu Lab"></option>
                  <option value="ColorFabb"></option>
                  <option value="Creality"></option>
                  <option value="eSun"></option>
                  <option value="Elegoo"></option>
                  <option value="Fiberlogy"></option>
                  <option value="Fillamentum"></option>
                  <option value="FlashForge"></option>
                  <option value="FormFutura"></option>
                  <option value="Hatchbox"></option>
                  <option value="Inland"></option>
                  <option value="Jayo"></option>
                  <option value="MatterHackers"></option>
                  <option value="Overture"></option>
                  <option value="Polar Filament"></option>
                  <option value="Polymaker"></option>
                  <option value="Prusament"></option>
                  <option value="Proto-Pasta"></option>
                  <option value="Push Plastics"></option>
                  <option value="Sunlu"></option>
                </datalist>
              </div>
              <div class="field">
                <label for="target_weight_g">Spool Weight (g)</label>
                <input id="target_weight_g" type="number" min="1" max="65535" value="1000" required />
              </div>
            </div>
            <div class="grid-2">
              <div class="field">
                <label for="diameter_um">Diameter</label>
                <select id="diameter_um">
                  <option value="1750" selected>1.75 mm</option>
                  <option value="2850">2.85 mm</option>
                </select>
              </div>
              <div class="field">
                <label for="density">Density (g/cm&sup3;)</label>
                <input id="density" type="number" min="0" max="65" step="0.001" value="1.24" />
              </div>
            </div>
          </section>

          <section>
            <h2 class="section-title">Color</h2>
            <div class="grid-2">
              <div class="field">
                <label for="color_name">Color Name (optional)</label>
                <input id="color_name" type="text" maxlength="32" placeholder="Galaxy Purple" />
              </div>
              <div class="field">
                <label>Color</label>
                <div class="color-row">
                  <input id="colorPicker" type="color" value="#ffffff" />
                  <input id="colorHex" type="text" value="#FFFFFF" maxlength="7" placeholder="#FFFFFF" required />
                </div>
              </div>
            </div>
          </section>

          <section>
            <h2 class="section-title">Temperatures (&deg;C, multiples of 5)</h2>
            <div class="grid-2">
              <div class="field">
                <label for="print_temp_c">Print Temperature</label>
                <input id="print_temp_c" type="number" min="0" max="1275" step="5" value="210" required />
              </div>
              <div class="field">
                <label for="bed_temp_c">Bed Temperature</label>
                <input id="bed_temp_c" type="number" min="0" max="1275" step="5" value="60" required />
              </div>
              <div class="field">
                <label for="chamber_temp_c">Chamber Temp (&deg;C)</label>
                <input id="chamber_temp_c" type="number" min="0" max="1275" step="5" value="0" />
                <div class="hint">Required by the v2 spec; 0 = no chamber.</div>
              </div>
            </div>
          </section>

          <section>
            <button type="button" class="advanced-toggle" id="advancedToggle" aria-expanded="false">
              <span>Advanced fields</span>
              <span data-toggle-text>Show</span>
            </button>

            <div class="advanced-box hidden" id="advancedBox">
              <div class="grid-2">
                <div class="field">
                  <label for="serial_number">Serial Number</label>
                  <input id="serial_number" type="text" maxlength="32" />
                </div>
                <div class="field">
                  <label for="online_url">Online URL (no https://)</label>
                  <input id="online_url" type="text" maxlength="32" />
                </div>
              </div>
              <div class="grid-2">
                <div class="field">
                  <label for="measured_filament_weight_g">Measured Filament Weight (g)</label>
                  <input id="measured_filament_weight_g" type="number" min="0" max="65535" />
                </div>
                <div class="field">
                  <label for="empty_spool_weight_g">Empty Spool Weight (g)</label>
                  <input id="empty_spool_weight_g" type="number" min="0" max="65535" />
                </div>
              </div>
              <div class="grid-3">
                <div class="field">
                  <label for="min_print_temp_c">Print Min (&deg;C)</label>
                  <input id="min_print_temp_c" type="number" min="0" max="1275" step="5" placeholder="190" />
                </div>
                <div class="field">
                  <label for="max_print_temp_c">Print Max (&deg;C)</label>
                  <input id="max_print_temp_c" type="number" min="0" max="1275" step="5" placeholder="230" />
                </div>
                <div class="field">
                  <label for="min_bed_temp_c">Bed Min (&deg;C)</label>
                  <input id="min_bed_temp_c" type="number" min="0" max="1275" step="5" placeholder="50" />
                </div>
                <div class="field">
                  <label for="max_bed_temp_c">Bed Max (&deg;C)</label>
                  <input id="max_bed_temp_c" type="number" min="0" max="1275" step="5" placeholder="70" />
                </div>
                <div class="field">
                  <label for="max_dry_temp_c">Dry Temp (&deg;C)</label>
                  <input id="max_dry_temp_c" type="number" min="0" max="1275" step="5" placeholder="45" />
                </div>
                <div class="field">
                  <label for="dry_time_hours">Dry Time (hrs)</label>
                  <input id="dry_time_hours" type="number" min="0" max="255" placeholder="4" />
                </div>
              </div>
              <div class="grid-2">
                <div class="field">
                  <label for="target_volumetric_speed">Target Vol. Speed (mm&sup3;/s)</label>
                  <input id="target_volumetric_speed" type="number" min="0" max="255" />
                </div>
                <div class="field">
                  <label for="transmission_distance">Transmission Dist. (&times;0.1mm)</label>
                  <input id="transmission_distance" type="number" min="0" max="250" />
                </div>
              </div>
              <div class="grid-3">
                <div class="field">
                  <label for="sku">SKU</label>
                  <input id="sku" type="text" maxlength="16" />
                </div>
                <div class="field">
                  <label for="barcode">Barcode (UPC/GTIN)</label>
                  <input id="barcode" type="text" maxlength="14" inputmode="numeric" pattern="[0-9]*" placeholder="digits only" />
                </div>
                <div class="field">
                  <label for="min_nozzle_diameter_mm">Min Nozzle (mm)</label>
                  <input id="min_nozzle_diameter_mm" type="number" min="0" max="25.5" step="0.1" placeholder="0.4" />
                </div>
              </div>
            </div>
          </section>

          <div id="spoolmanEnrichment" class="hidden" style="margin-top:16px;padding:14px;background:var(--card-alt,#1e1e35);border:1px solid var(--blue,#4a9eff);border-radius:8px;">
            <div style="display:flex;align-items:center;gap:8px;margin-bottom:4px">
              <span style="color:var(--blue,#4a9eff);font-size:11px;text-transform:uppercase;letter-spacing:1px;font-weight:600">Spoolman Enrichment</span>
              <span id="spoolmanMatchBadge" class="hidden" style="background:#4a9eff22;border:1px solid #4a9eff55;border-radius:3px;padding:0 6px;color:#4a9eff;font-size:10px"></span>
            </div>
            <p class="card-subtitle" style="font-size:11px;margin-bottom:10px">
              Spoolman-tracked remaining weight &mdash; saved to Spoolman on write.
            </p>
            <div class="grid-2">
              <div class="field">
                <label for="enrich-remaining">Remaining Weight (g)</label>
                <input id="enrich-remaining" type="number" placeholder="e.g. 1000" min="1" max="10000" />
              </div>
            </div>
          </div>

          <div class="write-warning">Keep the tag still &mdash; do not remove until writing is complete.</div>

          <div id="readPrompt" class="hidden write-warning" style="background:#0d2a1a;border-color:#2a7a4a;color:#4adf8a;text-align:center;padding:10px">
            Place tag on reader&hellip; <span style="font-size:11px;color:#5a9a6a">hold still until detected</span>
          </div>

          <div class="hint" style="margin-top:12px">Writes OpenTag3D v2.003 &mdash; requires NTAG215 or larger.</div>

          <div class="actions">
            <button type="submit" class="btn-primary" id="writeBtn">Write Tag</button>
            <button type="button" class="btn-secondary" id="readBtn">Read</button>
            <button type="button" class="btn-ghost" id="replaceBtn">Replace tag contents</button>
            <button type="reset" class="btn-ghost">Clear</button>
          </div>
          <p class="card-subtitle" style="margin-top:4px;font-size:11px">
            <strong>Write Tag</strong> updates the fields below and keeps everything else already on the tag.
            <strong>Replace tag contents</strong> erases the rest &mdash; only for a tag you want blanked.
          </p>
        </form>
      </div>
    </section>

    <section class="card hidden" id="statusView">
      <div class="card-head">
        <h1 class="card-title">Writing OpenTag3D</h1>
        <p class="card-subtitle">Keep the tag on the scanner until verification completes.</p>
      </div>

      <div class="card-body">
        <div class="status-wrap">
          <div class="status-banner" id="statusBanner">Starting write flow&hellip;</div>

          <div class="steps">
            <div class="step" id="step-wait">
              <div class="dot"></div>
              <div>
                <div class="step-title">Waiting for tag</div>
                <div class="step-sub">Place an NTAG215 or NTAG216 on the scanner.</div>
              </div>
            </div>

            <div class="step" id="step-detect">
              <div class="dot"></div>
              <div>
                <div class="step-title">Tag detected</div>
                <div class="step-sub">Tag presence and UID confirmed.</div>
              </div>
            </div>

            <div class="step" id="step-write">
              <div class="dot"></div>
              <div>
                <div class="step-title">Writing data</div>
                <div class="step-sub">Writing OpenTag3D NDEF payload to tag.</div>
              </div>
            </div>

            <div class="step" id="step-verify">
              <div class="dot"></div>
              <div>
                <div class="step-title">Verifying write</div>
                <div class="step-sub">Reading back tag to confirm data matches.</div>
              </div>
            </div>
          </div>

          <div class="result" id="resultBox">Waiting&hellip;</div>

          <div class="actions">
            <button type="button" class="btn-secondary hidden" id="backBtn">Back to Form</button>
            <button type="button" class="btn-primary hidden" id="anotherBtn">Write Another</button>
          </div>
        </div>
      </div>
    </section>

    <div class="footer-note">OpenTag3D v2.003 &mdash; NTAG215/216</div>
  </div>

  <script src="/js/shared.js?v=)rawliteral" FIRMWARE_VERSION R"rawliteral("></script>
  <script>
    var writerForm = document.getElementById('writerForm');

    // Tracks which tag (if any) the form currently mirrors, so a write can
    // tell the firmware to preserve untouched fields instead of replacing
    // the whole tag. Cleared whenever the form stops being a faithful copy
    // of that tag (Clear, Spoolman import, Write Another / Back).
    var ot3dSource = null;   // { uid: string, snapshot: {fieldId: string} }

    // Single-use token for the Replace action. The firmware merges onto any
    // tag that already carries data unless this arrives with the request, so
    // it is the only way to erase one — and it must never be left set for a
    // later write.
    var replaceConfirmed = false;

    var OT3D_EDITABLE_IDS = [
      'base_material','material_modifiers','manufacturer','color_name','colorHex',
      'diameter_um','target_weight_g','print_temp_c','bed_temp_c','density',
      'serial_number','online_url','measured_filament_weight_g','empty_spool_weight_g',
      'min_print_temp_c','max_print_temp_c','min_bed_temp_c','max_bed_temp_c',
      'max_dry_temp_c','dry_time_hours','target_volumetric_speed','transmission_distance',
      'sku','barcode','chamber_temp_c','min_nozzle_diameter_mm'
    ];

    function snapshotForm(ids) {
      var s = {};
      ids.forEach(function (id) {
        var el = document.getElementById(id);
        if (el) s[id] = el.value;
      });
      return s;
    }

    // Sets a select's value even when it's not one of the hardcoded options
    // (e.g. a modifier or diameter the tag carries that isn't in this form's
    // dropdown) by adding a temporary option instead of silently falling
    // back to whatever was selected by default.
    function putSelect(id, v) {
      if (v === undefined || v === null) return;
      var el = document.getElementById(id);
      if (!el) return;
      var found = Array.prototype.some.call(el.options, function (o) { return o.value == v; });
      if (!found) {
        var o = document.createElement('option');
        o.value = v;
        o.textContent = v + ' (on tag)';
        el.appendChild(o);
      }
      el.value = v;
    }

    // Fills the form from a status response's opentag3d block. Every
    // editable control is reset first so a second read (tag B) can never
    // inherit a value left over from a prior tag (tag A). Presence checks
    // (!== undefined/null) are used throughout instead of truthiness so a
    // real zero or empty string read off the tag is applied, not skipped.
    // Binds ot3dSource to this UID on success so a write back to the same
    // tag can preserve fields this function left untouched.
    function fillForm(status, uid) {
      var ot = status.opentag3d;
      if (!ot) return;

      OT3D_EDITABLE_IDS.forEach(function (id) {
        var el = document.getElementById(id);
        if (el) el.value = '';
      });

      function put(id, v) {
        if (v === undefined || v === null) return;
        var el = document.getElementById(id);
        if (el) el.value = v;
      }

      put('base_material', ot.base_material);
      putSelect('material_modifiers', ot.modifiers);
      put('manufacturer', ot.manufacturer);
      put('color_name', ot.color_name);
      put('colorHex', ot.color_hex);
      var colorPickerEl = document.getElementById('colorPicker');
      if (colorPickerEl && ot.color_hex !== undefined && ot.color_hex !== null) colorPickerEl.value = ot.color_hex;
      putSelect('diameter_um', ot.diameter_mm !== undefined ? Math.round(ot.diameter_mm * 1000) : undefined);
      put('target_weight_g', ot.target_weight_g);
      put('print_temp_c', ot.print_temp);
      put('bed_temp_c', ot.bed_temp);
      // v2 always carries chamber_temp (even 0); v1 omits it entirely, so
      // presence here correctly distinguishes "no chamber" from "unknown".
      put('chamber_temp_c', ot.chamber_temp);
      put('density', ot.density);
      put('serial_number', ot.serial_number);
      put('online_url', ot.online_url);
      put('sku', ot.sku);
      put('barcode', ot.barcode);
      put('measured_filament_weight_g', ot.measured_weight_g);
      put('empty_spool_weight_g', ot.empty_spool_g);
      put('min_print_temp_c', ot.min_print_temp);
      put('max_print_temp_c', ot.max_print_temp);
      put('min_bed_temp_c', ot.min_bed_temp);
      put('max_bed_temp_c', ot.max_bed_temp);
      put('max_dry_temp_c', ot.dry_temp);
      put('dry_time_hours', ot.dry_time_hours);
      put('target_volumetric_speed', ot.target_volumetric_speed);
      // Raw encoded value (mm x 0.1) — matches the form label, the POST
      // field, and the struct member. Never converted in either direction.
      put('transmission_distance', ot.transmission_distance);
      put('min_nozzle_diameter_mm', ot.min_nozzle_mm);

      ot3dSource = { uid: uid || '', snapshot: snapshotForm(OT3D_EDITABLE_IDS) };
    }

    writerForm.addEventListener('reset', function () {
      ot3dSource = null;
      replaceConfirmed = false;
    });

    syncColorPicker('colorPicker', 'colorHex');
    setupAdvancedToggle('advancedToggle', 'advancedBox');

    // Auto-fill temps and density from material selection
    var baseMaterialEl = document.getElementById('base_material');
    var modifiersEl = document.getElementById('material_modifiers');
    var ot3dFieldMap = {
      minPrintTemp: 'min_print_temp_c', maxPrintTemp: 'max_print_temp_c',
      minBedTemp: 'min_bed_temp_c', maxBedTemp: 'max_bed_temp_c',
      density: 'density',
      dryTemp: 'max_dry_temp_c', dryTime: 'dry_time_hours'
    };
    trackAutoFill(['print_temp_c','bed_temp_c','min_print_temp_c','max_print_temp_c','min_bed_temp_c','max_bed_temp_c','density','max_dry_temp_c','dry_time_hours']);
    function ot3dAutoFill() {
      var name = baseMaterialEl.value;
      var mod = modifiersEl ? modifiersEl.value : '';
      if (mod && mod !== 'None') name = name + '-' + mod;
      autoFillMaterialData(name, ot3dFieldMap);
      // Also fill the basic print/bed temp fields
      var m = lookupMaterial(name);
      if (m) {
        var pt = document.getElementById('print_temp_c');
        if (pt && pt.dataset.autoFilled !== 'false') { pt.value = m.extruder_temp; pt.dataset.autoFilled = 'true'; }
        var bt = document.getElementById('bed_temp_c');
        if (bt && bt.dataset.autoFilled !== 'false') { bt.value = m.bed_temp; bt.dataset.autoFilled = 'true'; }
      }
    }
    baseMaterialEl.addEventListener('input', ot3dAutoFill);

    // Pre-fill from a tag already on the scanner when the page loads. An
    // OpenTag3D tag gets the same field-complete fillForm used by the Read
    // button (and becomes eligible for a preserve-mode write back to that
    // UID); any other tag kind falls back to the generic cross-format seed,
    // which only has a handful of fields to offer and is seeding a brand
    // new OpenTag3D tag rather than reading one back, so material auto-fill
    // still runs there to fill in what that other tag can't provide.
    api('/api/status').then(function(status) {
      if (status && status.present && status.tag_kind === 'OpenTag3D' && status.opentag3d) {
        fillForm(status, status.uid);
        return;
      }
      prefillFromTag({
        material: 'base_material',
        color: 'colorHex',
        colorPicker: 'colorPicker',
        manufacturer: 'manufacturer',
        weight: 'target_weight_g',
        density: 'density',
        diameter: 'diameter_mm',
        nozzle_min: 'min_print_temp_c',
        nozzle_max: 'max_print_temp_c',
        bed_min: 'min_bed_temp_c',
        bed_max: 'max_bed_temp_c',
        dry_temp: 'max_dry_temp_c',
        dry_time: 'dry_time_hours'
      }).then(function(d) {
        if (!d) return;
        // Sync modifiers dropdown if available
        if (d.modifiers) {
          var modEl = document.getElementById('material_modifiers');
          if (modEl) modEl.value = d.modifiers;
        }
        // Sync basic print/bed temp from nozzle_max/bed_max
        var pt = document.getElementById('print_temp_c');
        if (pt && d.nozzle_max && pt.dataset.autoFilled !== 'false') {
          pt.value = d.nozzle_max; pt.dataset.autoFilled = 'true';
        }
        var bt = document.getElementById('bed_temp_c');
        if (bt && d.bed_max && bt.dataset.autoFilled !== 'false') {
          bt.value = d.bed_max; bt.dataset.autoFilled = 'true';
        }
        // Sync diameter dropdown (values in micrometers)
        if (d.diameter) {
          var dEl = document.getElementById('diameter_um');
          if (dEl && dEl.dataset.autoFilled !== 'false') {
            var um = Math.round(d.diameter * 1000);
            dEl.value = um; dEl.dataset.autoFilled = 'true';
          }
        }
        // Trigger material auto-fill for any missing fields — safe here
        // because this branch never carries real OpenTag3D field data to
        // clobber; it's seeding a new tag, not preserving one just read.
        var matEl = document.getElementById('base_material');
        if (matEl) matEl.dispatchEvent(new Event('input'));
      });
    }).catch(function() {});

    if (modifiersEl) modifiersEl.addEventListener('change', ot3dAutoFill);
    loadMaterialDb().then(function(db) {
      var dl = document.getElementById('material-list');
      var existing = {};
      var opts = dl.querySelectorAll('option');
      for (var i = 0; i < opts.length; i++) existing[opts[i].value.toUpperCase()] = true;
      Object.keys(db).sort().forEach(function(key) {
        if (!existing[key]) {
          var opt = document.createElement('option');
          opt.value = db[key].material || key;
          dl.appendChild(opt);
        }
      });
    });

    function intVal(id, fallback) {
      var raw = document.getElementById(id).value.trim();
      if (raw === '') return fallback;
      var n = parseInt(raw, 10);
      return isNaN(n) ? fallback : n;
    }

    function floatVal(id, fallback) {
      var raw = document.getElementById(id).value.trim();
      if (raw === '') return fallback;
      var n = parseFloat(raw);
      return isNaN(n) ? fallback : n;
    }

    function strVal(id) {
      var el = document.getElementById(id);
      return el ? el.value.trim() : '';
    }

    // Diffs the form against ot3dSource's snapshot so a write only carries
    // the fields the user actually touched. Preserve mode only applies when
    // writing back to the exact UID the form was filled from. No source at
    // all means the form was never filled from a tag, so every field is sent
    // as if creating one — but the mode is only a request now: the firmware
    // reads the tag and merges onto it anyway unless confirm_replace is set,
    // which is what stops a fresh page from erasing a spool it never read.
    // A source bound to a DIFFERENT UID than what's on the reader now is
    // still refused outright — the form is not a faithful copy of whatever
    // tag that is, so its values must not be pushed onto it.
    function buildPayload(uid) {
      // Consume the replace token here, not in the submit handler: the write
      // flow is async and calls this long after the handler has returned, so
      // the handler cannot clear it before it is read. Clearing it on entry
      // means one press authorises exactly one write, even if this function
      // throws below.
      var confirmed = replaceConfirmed;
      replaceConfirmed = false;

      var preserve;
      if (!ot3dSource || !ot3dSource.uid) {
        preserve = false;
      } else if (ot3dSource.uid === uid) {
        preserve = true;
      } else {
        throw new Error('This form was read from a different tag (' + ot3dSource.uid + '). Press Clear to write this tag from scratch, or re-read it first.');
      }
      // Replace means "write what is on screen and erase the rest", so the
      // diff must stop omitting fields the user did not touch.
      if (confirmed) preserve = false;

      var body = { uid: uid || '', mode: preserve ? 'preserve' : 'replace' };
      if (confirmed) body.confirm_replace = true;

      function changed(id) {
        if (!preserve) return true;
        return document.getElementById(id).value !== ot3dSource.snapshot[id];
      }

      if (changed('colorHex')) {
        var color = normalizeHex(document.getElementById('colorHex').value);
        if (!color) throw new Error('Color must be a valid 6-digit hex value');
        body.color_r = parseInt(color.substr(1, 2), 16);
        body.color_g = parseInt(color.substr(3, 2), 16);
        body.color_b = parseInt(color.substr(5, 2), 16);
      }
      if (!preserve) body.color_a = 255;   // a brand-new tag is opaque

      if (changed('base_material'))      body.base_material = strVal('base_material').toUpperCase();
      if (changed('material_modifiers')) body.material_modifiers = strVal('material_modifiers');
      if (changed('manufacturer'))       body.manufacturer = strVal('manufacturer');
      if (changed('color_name'))         body.color_name = strVal('color_name');
      if (changed('diameter_um'))        body.diameter_um = intVal('diameter_um', 1750);
      if (changed('target_weight_g'))    body.target_weight_g = intVal('target_weight_g', 1000);
      if (changed('print_temp_c'))       body.print_temp_c = intVal('print_temp_c', 210);
      if (changed('bed_temp_c'))         body.bed_temp_c = intVal('bed_temp_c', 60);
      if (changed('density')) {
        // density is the one non-required, non-select numeric field a user
        // can blank. floatVal's fallback (1.24) exists for its other
        // callers and is a fabricated real-looking number here — sending
        // it would silently overwrite a tag's genuine density (e.g. a real
        // 1.27 PETG) with a made-up 1.24. Only send when the user actually
        // typed a number; a blank box means "leave it alone," and omitting
        // the key is exactly how preserve mode expresses that.
        var densityRaw = strVal('density');
        var densityNum = parseFloat(densityRaw);
        if (densityRaw !== '' && !isNaN(densityNum)) body.density_ugcm3 = Math.round(densityNum * 1000);
      }
      if (changed('chamber_temp_c'))     body.chamber_temp_c = intVal('chamber_temp_c', 0);
      if (changed('sku'))                body.sku = strVal('sku');

      // Raw pass-through fields: transmission_distance is mm x 0.1 on both
      // the status read and this POST, matched to the struct member and the
      // form label — never convert it here.
      var numeric = [
        ['serial_number', 'serial_number', 'str'],
        ['online_url', 'online_url', 'str'],
        ['measured_filament_weight_g', 'measured_filament_weight_g', 'int'],
        ['empty_spool_weight_g', 'empty_spool_weight_g', 'int'],
        ['min_print_temp_c', 'min_print_temp_c', 'int'],
        ['max_print_temp_c', 'max_print_temp_c', 'int'],
        ['min_bed_temp_c', 'min_bed_temp_c', 'int'],
        ['max_bed_temp_c', 'max_bed_temp_c', 'int'],
        ['max_dry_temp_c', 'max_dry_temp_c', 'int'],
        ['dry_time_hours', 'dry_time_hours', 'int'],
        ['target_volumetric_speed', 'target_volumetric_speed', 'int'],
        ['transmission_distance', 'transmission_distance', 'int']
      ];
      numeric.forEach(function (f) {
        if (!changed(f[0])) return;
        body[f[1]] = f[2] === 'str' ? strVal(f[0]) : intVal(f[0], 0);
      });

      // Unlike density, the firmware treats a blank barcode/nozzle-diameter
      // as a meaningful, honoured clear (empty digit string / 0 respectively
      // — see WebServerManager.cpp's barcode and min_nozzle_diameter patch
      // handling), so a deliberate blank must be sent, not dropped.
      if (changed('barcode')) {
        body.barcode = strVal('barcode');
      }
      if (changed('min_nozzle_diameter_mm')) {
        var mm = floatVal('min_nozzle_diameter_mm', -1);
        body.min_nozzle_diameter = mm >= 0 ? Math.round(mm * 10) : 0;
      }

      return body;
    }

    var ENRICHMENT_FIELDS = ['enrich-remaining'];

    function showCreateView() {
      document.getElementById('statusView').classList.add('hidden');
      document.getElementById('createView').classList.remove('hidden');
      // The form on screen may still show the values from the tag just
      // written, but it's no longer a guaranteed-faithful copy of any tag
      // (the physical tag may be swapped before the next write), so a
      // stale snapshot must not be allowed to claim preserve mode.
      ot3dSource = null;
      replaceConfirmed = false;
    }

    document.getElementById('backBtn').addEventListener('click', showCreateView);
    document.getElementById('anotherBtn').addEventListener('click', showCreateView);

    document.getElementById('replaceBtn').addEventListener('click', function () {
      if (!window.confirm('Replace everything on this tag?\n\nThe spool\'s serial, batch, manufacture date, measured weight and any other data already on it will be erased and cannot be recovered.')) return;
      replaceConfirmed = true;
      if (writerForm.requestSubmit) writerForm.requestSubmit();
      else writerForm.dispatchEvent(new Event('submit', { cancelable: true }));
    });

    writerForm.addEventListener('submit', function(e) {
      e.preventDefault();
      sharedWriteFlow({
        stepIds: ['step-wait', 'step-detect', 'step-write', 'step-verify'],
        endpoint: '/api/write-opentag3d',
        formatName: 'OpenTag3D',
        buildPayload: buildPayload,
        verify: function (status, payload, requestId) {
          // Keyed by request_id — a form-value comparison can't tell a real
          // write from an unrelated preserve-mode edit that never touched
          // these fields, so it would report success on a pending/failed write.
          var w = status.write_result;
          if (!w || w.request_id !== requestId || w.uid !== payload.uid) return false;   // not our result yet
          if (!w.ok) throw new Error(w.error || 'Write failed');
          return true;
        },
        afterSuccess: function(uid) {
          return saveEnrichmentToSpoolman(uid, {
            tagFormat: 'opentag3d',
            enrichmentFieldIds: ENRICHMENT_FIELDS,
            getFields: function() {
              var nozzleMin = parseInt(document.getElementById('min_print_temp_c').value) || 0;
              var nozzleMax = parseInt(document.getElementById('max_print_temp_c').value) || 0;
              var bedMin = parseInt(document.getElementById('min_bed_temp_c').value) || 0;
              var bedMax = parseInt(document.getElementById('max_bed_temp_c').value) || 0;
              var diameter = parseInt(document.getElementById('diameter_um').value) || 1750;
              return {
                manufacturer: document.getElementById('manufacturer').value.trim(),
                material: document.getElementById('base_material').value.trim(),
                colorHex: document.getElementById('colorHex').value || '',
                remainingG: parseFloat(document.getElementById('enrich-remaining').value) || 0,
                density: parseFloat(document.getElementById('density').value) || 0,
                nozzleTemp: (nozzleMin && nozzleMax) ? Math.round((nozzleMin + nozzleMax) / 2) : (nozzleMin || nozzleMax || parseInt(document.getElementById('print_temp_c').value) || 0),
                bedTemp: (bedMin && bedMax) ? Math.round((bedMin + bedMax) / 2) : (bedMin || bedMax || parseInt(document.getElementById('bed_temp_c').value) || 0),
                diameterMm: diameter / 1000.0
              };
            }
          });
        }
      // buildPayload consumes the token, but the flow can end before it ever
      // runs (no tag presented, unreadable UID) — clear it either way.
      }).finally(function () { replaceConfirmed = false; });
    });

    renderSpoolmanPicker('spoolmanPicker', {
      material: 'base_material',
      manufacturer: 'manufacturer',
      color: 'colorHex',
      colorPicker: 'colorPicker',
      remaining: 'target_weight_g',
      density: 'density',
      diameter: 'diameter_um',
      diameterUnit: 'um',
      nozzle_single: 'print_temp_c',
      nozzle_min: 'min_print_temp_c',
      nozzle_max: 'max_print_temp_c',
      bed_single: 'bed_temp_c',
      bed_min: 'min_bed_temp_c',
      bed_max: 'max_bed_temp_c'
    });

    // renderSpoolmanPicker's apply handler (selectSpoolmanSpool, in
    // shared.js) overwrites form fields from a picked spool — once that
    // happens the form is no longer a faithful copy of ot3dSource's tag.
    // Its result rows are rendered into #spoolmanPickerResults with an
    // inline onclick, so a capturing listener on the container (which
    // fires before that inline handler) catches every pick, including
    // rows added after this listener was attached.
    var spoolmanPickerEl = document.getElementById('spoolmanPicker');
    if (spoolmanPickerEl) {
      spoolmanPickerEl.addEventListener('click', function (e) {
        if (e.target.closest && e.target.closest('#spoolmanPickerResults')) {
          ot3dSource = null;
          replaceConfirmed = false;
        }
      }, true);
    }

    var _enrichCheck = setInterval(function() {
      if (document.querySelector('#spoolmanPickerSearch')) {
        document.getElementById('spoolmanEnrichment').classList.remove('hidden');
        clearInterval(_enrichCheck);
      }
    }, 500);
    setTimeout(function() { clearInterval(_enrichCheck); }, 15000);

    function showMatchBadge(text) {
      var badge = document.getElementById('spoolmanMatchBadge');
      badge.textContent = text;
      badge.classList.remove('hidden');
    }

    setupReadButton({
      expectedKind: 'OpenTag3D',
      wrongKindMsg: 'wrong format \u2014 expected OpenTag3D',
      showMatchBadge: showMatchBadge,
      // setupReadButton (shared.js) always calls this with just the status
      // object, so the UID it should bind for preserve mode comes off
      // status.uid rather than a second argument.
      fillForm: function(status) {
        fillForm(status, status.uid);
      },
      fillEnrichment: function(status) {
        var sp = status.spoolman || {};
        if (sp.remaining_g !== undefined) setVal('enrich-remaining', sp.remaining_g.toFixed(1));
      }
    });

  </script>
</body>
</html>
)rawliteral";
