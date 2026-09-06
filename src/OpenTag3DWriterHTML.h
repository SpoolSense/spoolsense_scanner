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
                  <option value="Polymaker"></option>
                  <option value="Prusament"></option>
                  <option value="Proto-Pasta"></option>
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
              <span>Extended fields (NTAG215/216 only)</span>
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
                  <input id="transmission_distance" type="number" min="0" max="65535" />
                </div>
              </div>
              <div class="grid-3">
                <div class="field">
                  <label for="sku">SKU</label>
                  <input id="sku" type="text" maxlength="16" />
                </div>
                <div class="field">
                  <label for="barcode">Barcode (UPC/GTIN)</label>
                  <input id="barcode" type="text" maxlength="13" inputmode="numeric" pattern="[0-9]*" placeholder="digits only" />
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

          <div class="hint" style="margin-top:12px">Writes OpenTag3D v2.000 &mdash; requires NTAG215 or larger.</div>

          <div class="actions">
            <button type="submit" class="btn-primary" id="writeBtn">Write Tag</button>
            <button type="button" class="btn-secondary" id="readBtn">Read</button>
            <button type="reset" class="btn-ghost">Clear</button>
          </div>
          <p class="card-subtitle" style="margin-top:4px;font-size:11px">
            Use <strong>Read</strong> to load an existing tag before overwriting.
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

    <div class="footer-note">OpenTag3D v2.000 &mdash; NTAG215/216</div>
  </div>

  <script src="/js/shared.js?v=)rawliteral" FIRMWARE_VERSION R"rawliteral("></script>
  <script>
    var writerForm = document.getElementById('writerForm');

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

    // Pre-fill from scanned tag if present
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
      // Trigger material auto-fill for any missing fields
      var matEl = document.getElementById('base_material');
      if (matEl) matEl.dispatchEvent(new Event('input'));
    });

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

    var EXTENDED_IDS = [
      'serial_number', 'online_url', 'measured_filament_weight_g',
      'empty_spool_weight_g', 'min_print_temp_c', 'max_print_temp_c',
      'min_bed_temp_c', 'max_bed_temp_c', 'max_dry_temp_c',
      'dry_time_hours', 'target_volumetric_speed', 'transmission_distance'
    ];

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

    function hasExtended() {
      for (var i = 0; i < EXTENDED_IDS.length; i++) {
        var el = document.getElementById(EXTENDED_IDS[i]);
        if (el && el.value.trim() !== '') return true;
      }
      return false;
    }

    function buildPayload(uid) {
      var color = normalizeHex(document.getElementById('colorHex').value);
      if (!color) throw new Error('Color must be a valid 6-digit hex value');

      var r = parseInt(color.substr(1,2), 16);
      var g = parseInt(color.substr(3,2), 16);
      var b = parseInt(color.substr(5,2), 16);

      var densityGcm3 = floatVal('density', 1.24);
      var densityUgcm3 = Math.round(densityGcm3 * 1000);

      var body = {
        uid: uid || '',
        base_material: strVal('base_material').toUpperCase(),
        material_modifiers: strVal('material_modifiers'),
        manufacturer: strVal('manufacturer'),
        color_name: strVal('color_name'),
        color_r: r,
        color_g: g,
        color_b: b,
        color_a: 255,
        diameter_um: intVal('diameter_um', 1750),
        target_weight_g: intVal('target_weight_g', 1000),
        print_temp_c: intVal('print_temp_c', 210),
        bed_temp_c: intVal('bed_temp_c', 60),
        density_ugcm3: densityUgcm3
      };

      if (hasExtended()) {
        var sn = strVal('serial_number');
        if (sn) body.serial_number = sn;
        var url = strVal('online_url');
        if (url) body.online_url = url;
        if (strVal('measured_filament_weight_g')) body.measured_filament_weight_g = intVal('measured_filament_weight_g', 0);
        if (strVal('empty_spool_weight_g')) body.empty_spool_weight_g = intVal('empty_spool_weight_g', 0);
        if (strVal('min_print_temp_c')) body.min_print_temp_c = intVal('min_print_temp_c', 0);
        if (strVal('max_print_temp_c')) body.max_print_temp_c = intVal('max_print_temp_c', 0);
        if (strVal('min_bed_temp_c')) body.min_bed_temp_c = intVal('min_bed_temp_c', 0);
        if (strVal('max_bed_temp_c')) body.max_bed_temp_c = intVal('max_bed_temp_c', 0);
        if (strVal('max_dry_temp_c')) body.max_dry_temp_c = intVal('max_dry_temp_c', 0);
        if (strVal('dry_time_hours')) body.dry_time_hours = intVal('dry_time_hours', 0);
        if (strVal('target_volumetric_speed')) body.target_volumetric_speed = intVal('target_volumetric_speed', 0);
        if (strVal('transmission_distance')) body.transmission_distance = intVal('transmission_distance', 0);
      }

      body.sku = strVal('sku');
      var barcodeDigits = strVal('barcode');
      if (barcodeDigits) body.barcode = barcodeDigits;
      body.chamber_temp_c = intVal('chamber_temp_c', 0);
      var minNozzleMm = floatVal('min_nozzle_diameter_mm', -1);
      if (minNozzleMm >= 0) body.min_nozzle_diameter = Math.round(minNozzleMm * 10);

      return body;
    }

    var ENRICHMENT_FIELDS = ['enrich-remaining'];

    function showCreateView() {
      document.getElementById('statusView').classList.add('hidden');
      document.getElementById('createView').classList.remove('hidden');
    }

    document.getElementById('backBtn').addEventListener('click', showCreateView);
    document.getElementById('anotherBtn').addEventListener('click', showCreateView);

    writerForm.addEventListener('submit', function(e) {
      e.preventDefault();
      sharedWriteFlow({
        stepIds: ['step-wait', 'step-detect', 'step-write', 'step-verify'],
        endpoint: '/api/write-opentag3d',
        formatName: 'OpenTag3D',
        buildPayload: buildPayload,
        verify: function(status, payload) {
          if (status.tag_kind !== 'OpenTag3D' || !status.opentag3d) return false;
          var ot = status.opentag3d;
          return ot.base_material === payload.base_material &&
                 ot.manufacturer === payload.manufacturer &&
                 ot.target_weight_g === payload.target_weight_g;
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
      });
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
      fillForm: function(status) {
        var ot = status.opentag3d || {};
        setVal('base_material', ot.base_material || '');
        setVal('manufacturer', ot.manufacturer || '');
        if (ot.color_hex) {
          var c = ot.color_hex.startsWith('#') ? ot.color_hex : '#' + ot.color_hex;
          setVal('colorHex', c);
          setVal('colorPicker', c);
        }
        if (ot.target_weight_g) setVal('target_weight_g', ot.target_weight_g);
        if (ot.density) setVal('density', ot.density);
        if (ot.print_temp) setVal('print_temp_c', ot.print_temp);
        if (ot.bed_temp) setVal('bed_temp_c', ot.bed_temp);
        if (ot.min_print_temp) setVal('min_print_temp_c', ot.min_print_temp);
        if (ot.max_print_temp) setVal('max_print_temp_c', ot.max_print_temp);
        if (ot.min_bed_temp) setVal('min_bed_temp_c', ot.min_bed_temp);
        if (ot.max_bed_temp) setVal('max_bed_temp_c', ot.max_bed_temp);
        if (ot.dry_temp) setVal('max_dry_temp_c', ot.dry_temp);
        if (ot.dry_time_hours) setVal('dry_time_hours', ot.dry_time_hours);
        if (ot.chamber_temp) setVal('chamber_temp_c', ot.chamber_temp);
        if (ot.diameter_mm) {
          var dEl = document.getElementById('diameter_um');
          if (dEl) dEl.value = Math.round(ot.diameter_mm * 1000);
        }
        if (ot.color_name) setVal('color_name', ot.color_name);
        setVal('material_modifiers', ot.modifiers || '');
        setVal('serial_number', ot.serial_number || '');
        setVal('measured_filament_weight_g', ot.measured_weight_g || '');
        setVal('empty_spool_weight_g', ot.empty_spool_g || '');
        if (ot.sku) setVal('sku', ot.sku);
        if (ot.barcode) setVal('barcode', String(ot.barcode));
        if (ot.min_nozzle_mm) setVal('min_nozzle_diameter_mm', ot.min_nozzle_mm);
        var matEl = document.getElementById('base_material');
        if (matEl) matEl.dispatchEvent(new Event('input'));
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
