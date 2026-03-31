#pragma once

// OpenPrintTag writer page served at GET /writer/openprinttag
//
// API endpoints available to the page:
//   GET  /api/status      — current tag state JSON
//   POST /api/write-tag   — write all fields to tag (JSON body)
//   POST /api/format-tag  — format a blank tag (optional JSON body: {"uid":"..."})

const char OPENPRINTTAG_WRITER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>OpenPrintTag Writer &mdash; SpoolSense</title>
  <link rel="stylesheet" href="/css/shared.css" />
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
      <a href="/register/uid">NFC+</a>
      <a href="/update">Update</a>
      <a href="/troubleshooting">Troubleshooting</a>
      <a href="/config">Config</a>
    </nav>

    <section class="card" id="createView">
      <div class="card-head" style="display:flex;align-items:center;gap:16px">
        <img src="/img/openprinttag.png" alt="OpenPrintTag" style="height:48px;border-radius:8px" />
        <div>
          <h1 class="card-title">Create OpenPrintTag</h1>
          <p class="card-subtitle">Fill in the spool details, then write them to the tag.</p>
        </div>
      </div>

      <div class="card-body">
        <form id="writerForm">
          <section>
            <h2 class="section-title">Basic</h2>
            <div class="grid-2">
              <div class="field">
                <label for="material_search">Material</label>
                <input id="material_search" name="material_search" list="material-list" placeholder="Type to search materials" required />
                <datalist id="material-list">
                  <option data-id="0" value="PLA"></option>
                  <option data-id="1" value="PETG"></option>
                  <option data-id="2" value="TPU"></option>
                  <option data-id="3" value="ABS"></option>
                  <option data-id="4" value="ASA"></option>
                  <option data-id="5" value="PC"></option>
                  <option data-id="6" value="PCTG"></option>
                  <option data-id="7" value="PP"></option>
                  <option data-id="8" value="PA6 (Nylon 6)"></option>
                  <option data-id="9" value="PA11 (Nylon 11)"></option>
                  <option data-id="10" value="PA12 (Nylon 12)"></option>
                  <option data-id="11" value="PA66 (Nylon 66)"></option>
                  <option data-id="12" value="CPE"></option>
                  <option data-id="13" value="TPE"></option>
                  <option data-id="14" value="HIPS"></option>
                  <option data-id="15" value="PHA"></option>
                  <option data-id="16" value="PET"></option>
                  <option data-id="17" value="PEI"></option>
                  <option data-id="18" value="PBT"></option>
                  <option data-id="19" value="PVB"></option>
                  <option data-id="20" value="PVA"></option>
                  <option data-id="21" value="PEKK"></option>
                  <option data-id="22" value="PEEK"></option>
                </datalist>
                <input type="hidden" id="material_type" name="material_type" value="0" />
              </div>

              <div class="field">
                <label for="manufacturer">Brand / Manufacturer</label>
                <input id="manufacturer" name="manufacturer" list="brand-list" placeholder="Select or type a brand" required />
                <datalist id="brand-list">
                  <option value="3DXTech"></option>
                  <option value="Amolen"></option>
                  <option value="Ambrosia"></option>
                  <option value="Anycubic"></option>
                  <option value="Atomic Filament"></option>
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
                  <option value="IC3D"></option>
                  <option value="Inland"></option>
                  <option value="Jessie"></option>
                  <option value="MatterHackers"></option>
                  <option value="Overture"></option>
                  <option value="Polymaker"></option>
                  <option value="Prusament"></option>
                  <option value="Proto-Pasta"></option>
                  <option value="Siraya-Tech"></option>
                  <option value="Sunlu"></option>
                  <option value="Taulman3D"></option>
                </datalist>
              </div>

              <div class="field">
                <label for="material_name">Material Name</label>
                <input id="material_name" name="material_name" type="text" placeholder="PLA" />
                <div class="hint">Material name is forced to ALL CAPS.</div>
              </div>

              <div class="field">
                <label for="initial_weight_g">Full Weight (g)</label>
                <input id="initial_weight_g" name="initial_weight_g" type="number" min="0" step="0.1" placeholder="1000.0" required />
              </div>

              <div class="field">
                <label for="remaining_g">Remaining Weight (g)</label>
                <input id="remaining_g" name="remaining_g" type="number" min="0" step="0.1" placeholder="750.0" required />
              </div>

              <div class="field">
                <label>Color</label>
                <div class="color-row">
                  <input id="colorPicker" type="color" value="#ff0000" />
                  <input id="colorHex" name="color" type="text" value="#FF0000" maxlength="7" placeholder="#FF0000" required />
                </div>
                <div class="hint">Pick a color or type a hex value.</div>
              </div>
            </div>
          </section>

          <section>
            <button type="button" class="advanced-toggle" id="advancedToggle" aria-expanded="false">
              <span>Advanced fields</span>
              <span data-toggle-text>Show</span>
            </button>

            <div class="advanced-box hidden" id="advancedBox">
              <div class="grid-3">
                <div class="field">
                  <label for="density">Density</label>
                  <input id="density" name="density" type="number" min="0" step="0.01" placeholder="1.24" />
                </div>

                <div class="field">
                  <label for="diameter_mm">Diameter (mm)</label>
                  <input id="diameter_mm" name="diameter_mm" type="number" min="0" step="0.01" placeholder="1.75" />
                </div>

                <div class="field">
                  <label for="spoolman_id">Spoolman ID</label>
                  <input id="spoolman_id" name="spoolman_id" type="number" step="1" value="-1" />
                </div>

                <div class="field">
                  <label for="min_print_temp">Min Print Temp (&deg;C)</label>
                  <input id="min_print_temp" name="min_print_temp" type="number" min="0" step="1" placeholder="205" />
                </div>

                <div class="field">
                  <label for="max_print_temp">Max Print Temp (&deg;C)</label>
                  <input id="max_print_temp" name="max_print_temp" type="number" min="0" step="1" placeholder="225" />
                </div>

                <div class="field">
                  <label for="preheat_temp">Preheat Temp (&deg;C)</label>
                  <input id="preheat_temp" name="preheat_temp" type="number" min="0" step="1" placeholder="215" />
                </div>

                <div class="field">
                  <label for="min_bed_temp">Min Bed Temp (&deg;C)</label>
                  <input id="min_bed_temp" name="min_bed_temp" type="number" min="0" step="1" placeholder="55" />
                </div>

                <div class="field">
                  <label for="max_bed_temp">Max Bed Temp (&deg;C)</label>
                  <input id="max_bed_temp" name="max_bed_temp" type="number" min="0" step="1" placeholder="65" />
                </div>
              </div>
            </div>
          </section>

          <div class="write-warning">Keep the tag still &mdash; do not remove until writing is complete.</div>

          <div class="actions">
            <button type="submit" class="btn-primary" id="writeBtn">Write Tag</button>
            <button type="reset" class="btn-ghost">Clear</button>
          </div>
        </form>
      </div>
    </section>

    <section class="card hidden" id="statusView">
      <div class="card-head">
        <h1 class="card-title">Writing Tag</h1>
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
                <div class="step-sub">Checking scanner status for a present tag.</div>
              </div>
            </div>

            <div class="step" id="step-detect">
              <div class="dot"></div>
              <div>
                <div class="step-title">Tag detected</div>
                <div class="step-sub">Tag presence and UID confirmed.</div>
              </div>
            </div>

            <div class="step" id="step-format">
              <div class="dot"></div>
              <div>
                <div class="step-title">Formatting if needed</div>
                <div class="step-sub">Blank tags need NDEF structure before writing.</div>
              </div>
            </div>

            <div class="step" id="step-write">
              <div class="dot"></div>
              <div>
                <div class="step-title">Writing data</div>
                <div class="step-sub">Sending OpenPrintTag fields to the scanner.</div>
              </div>
            </div>

            <div class="step" id="step-verify">
              <div class="dot"></div>
              <div>
                <div class="step-title">Verifying write</div>
                <div class="step-sub">Polling status until the written values match.</div>
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

    <div class="footer-note">OpenPrintTag format &mdash; ISO15693</div>
  </div>

  <script src="/js/shared.js"></script>
  <script>
    var createView = document.getElementById('createView');
    var statusView = document.getElementById('statusView');
    var writerForm = document.getElementById('writerForm');

    var materialTypeEl = document.getElementById('material_type');
    var materialSearchEl = document.getElementById('material_search');
    var materialListEl = document.getElementById('material-list');
    var materialNameEl = document.getElementById('material_name');

    var backBtn = document.getElementById('backBtn');
    var anotherBtn = document.getElementById('anotherBtn');

    var STEP_IDS = ['step-wait', 'step-detect', 'step-format', 'step-write', 'step-verify'];

    syncColorPicker('colorPicker', 'colorHex');

    // Pre-fill from scanned tag if present
    prefillFromTag({
      material: 'material_search',
      color: 'colorHex',
      colorPicker: 'colorPicker',
      manufacturer: 'manufacturer',
      weight: 'initial_weight_g',
      remaining: 'remaining_g',
      density: 'density',
      diameter: 'diameter_mm',
      nozzle_min: 'min_print_temp',
      nozzle_max: 'max_print_temp',
      preheat: 'preheat_temp',
      bed_min: 'min_bed_temp',
      bed_max: 'max_bed_temp'
    }).then(function(d) {
      if (!d) return;
      // Sync material_type hidden field from material name
      var opts = document.getElementById('material-list').querySelectorAll('option');
      var search = document.getElementById('material_search');
      var hidden = document.getElementById('material_type');
      for (var i = 0; i < opts.length; i++) {
        if (opts[i].value.toUpperCase() === (d.material || '').toUpperCase()) {
          search.value = opts[i].value;
          hidden.value = opts[i].dataset.id || '0';
          break;
        }
      }
      // Trigger material DB auto-fill for any fields the tag didn't have
      var materialSearchEl = document.getElementById('material_search');
      if (materialSearchEl) materialSearchEl.dispatchEvent(new Event('input'));
    });

    setupAdvancedToggle('advancedToggle', 'advancedBox');

    // Sync material search → hidden material_type ID
    function syncMaterialId() {
      var opts = materialListEl.querySelectorAll('option');
      materialTypeEl.value = '0'; // default PLA
      for (var i = 0; i < opts.length; i++) {
        if (opts[i].value === materialSearchEl.value) {
          materialTypeEl.value = opts[i].dataset.id;
          break;
        }
      }
    }

    // Auto-fill temps and density from material selection
    var optFieldMap = {
      minPrintTemp: 'min_print_temp', maxPrintTemp: 'max_print_temp',
      minBedTemp: 'min_bed_temp', maxBedTemp: 'max_bed_temp',
      density: 'density'
    };
    trackAutoFill(['min_print_temp','max_print_temp','min_bed_temp','max_bed_temp','density']);
    materialSearchEl.addEventListener('input', function() {
      syncMaterialId();
      autoFillMaterialData(materialSearchEl.value, optFieldMap);
    });
    loadMaterialDb().then(function(db) {
      // Expand datalist with API materials that aren't in the hardcoded list
      var existing = {};
      var opts = materialListEl.querySelectorAll('option');
      for (var i = 0; i < opts.length; i++) existing[opts[i].value.toUpperCase()] = true;
      Object.keys(db).sort().forEach(function(key) {
        if (!existing[key]) {
          var opt = document.createElement('option');
          opt.value = db[key].material || key;
          opt.dataset.id = db[key].id || '0';
          materialListEl.appendChild(opt);
        }
      });
    });

    function showStatusView() {
      createView.classList.add('hidden');
      statusView.classList.remove('hidden');
      backBtn.classList.add('hidden');
      anotherBtn.classList.add('hidden');
    }

    function showCreateView() {
      statusView.classList.add('hidden');
      createView.classList.remove('hidden');
    }

    function syncMaterialNameFromSelection() {
      var selectedText = materialSearchEl.value || 'PLA';
      if (!materialNameEl.value.trim() || materialNameEl.dataset.autoFilled === 'true') {
        materialNameEl.value = selectedText.toUpperCase();
        materialNameEl.dataset.autoFilled = 'true';
      }
    }

    function readOptionalSpoolmanId() {
      var raw = document.getElementById('spoolman_id').value.trim();
      if (raw === '') return -1;
      var num = Number(raw);
      if (!Number.isFinite(num)) return -1;
      return num;
    }

    function prefillFromStatus(status) {
      if (!status || !status.present) return;
      maybeSetValue('material_type', status.material_type);
      if (status.manufacturer !== undefined) maybeSetValue('manufacturer', status.manufacturer);
      if (status.material_name !== undefined) {
        maybeSetValue('material_name', String(status.material_name).toUpperCase());
        materialNameEl.dataset.autoFilled = 'false';
      }
      if (status.initial_weight_g !== undefined) maybeSetValue('initial_weight_g', status.initial_weight_g);
      if (status.remaining_g !== undefined) maybeSetValue('remaining_g', status.remaining_g);
      if (status.color !== undefined) {
        var hex = normalizeHex(status.color);
        if (hex) {
          document.getElementById('colorHex').value = hex;
          document.getElementById('colorPicker').value = hex;
        }
      }
      if (status.density !== undefined) maybeSetValue('density', status.density);
      if (status.diameter_mm !== undefined) maybeSetValue('diameter_mm', status.diameter_mm);
      if (status.min_print_temp !== undefined) maybeSetValue('min_print_temp', status.min_print_temp);
      if (status.max_print_temp !== undefined) maybeSetValue('max_print_temp', status.max_print_temp);
      if (status.preheat_temp !== undefined) maybeSetValue('preheat_temp', status.preheat_temp);
      if (status.min_bed_temp !== undefined) maybeSetValue('min_bed_temp', status.min_bed_temp);
      if (status.max_bed_temp !== undefined) maybeSetValue('max_bed_temp', status.max_bed_temp);
      if (status.spoolman_id !== undefined) maybeSetValue('spoolman_id', status.spoolman_id);
    }

    function buildPayload(uid) {
      var color = normalizeHex(document.getElementById('colorHex').value);
      if (!color) throw new Error('Color must be a valid 6-digit hex value');

      var manufacturer = readString('manufacturer');
      if (!manufacturer) throw new Error('Manufacturer is required');

      var payload = {
        uid: uid || '',
        material_type: Number(materialTypeEl.value),
        color: color,
        manufacturer: manufacturer,
        initial_weight_g: readRequiredNumber('initial_weight_g'),
        remaining_g: readRequiredNumber('remaining_g')
      };

      var materialName = readString('material_name');
      if (materialName) payload.material_name = materialName.toUpperCase();

      var density = readPositiveNumber('density');
      if (density !== undefined) payload.density = density;

      var diameter = readPositiveNumber('diameter_mm');
      if (diameter !== undefined) payload.diameter_mm = diameter;

      var minPrint = readPositiveNumber('min_print_temp');
      if (minPrint !== undefined) payload.min_print_temp = minPrint;

      var maxPrint = readPositiveNumber('max_print_temp');
      if (maxPrint !== undefined) payload.max_print_temp = maxPrint;

      var preheat = readPositiveNumber('preheat_temp');
      if (preheat !== undefined) payload.preheat_temp = preheat;

      var minBed = readPositiveNumber('min_bed_temp');
      if (minBed !== undefined) payload.min_bed_temp = minBed;

      var maxBed = readPositiveNumber('max_bed_temp');
      if (maxBed !== undefined) payload.max_bed_temp = maxBed;

      payload.spoolman_id = readOptionalSpoolmanId();
      return payload;
    }

    function valuesMatch(status, payload) {
      var checks = [];
      if ('material_type' in payload)
        checks.push(Number(status.material_type) === Number(payload.material_type));
      if ('color' in payload)
        checks.push(String(status.color || '').toUpperCase() === String(payload.color).toUpperCase());
      if (payload.manufacturer !== undefined)
        checks.push(String(status.manufacturer || '') === String(payload.manufacturer));
      if ('initial_weight_g' in payload)
        checks.push(nearlyEqual(status.initial_weight_g, payload.initial_weight_g, 0.5));
      if ('remaining_g' in payload)
        checks.push(nearlyEqual(status.remaining_g, payload.remaining_g, 0.5));
      return checks.length > 0 && checks.every(Boolean);
    }

    async function writeFlow() {
      resetAllSteps(STEP_IDS);
      showStatusView();

      try {
        setStepState('step-wait', 'active');
        setBanner('statusBanner', 'Waiting for tag\u2026');
        setResult('resultBox', 'Place and hold the tag on the scanner.', '');

        var presentStatus = null;
        var deadline = Date.now() + 8000;
        while (Date.now() < deadline) {
          var s = await api('/api/status');
          if (s.present) { presentStatus = s; break; }
          await sleep(500);
        }
        if (!presentStatus) {
          setStepState('step-wait', 'error');
          throw new Error('No tag detected. Place the tag on the scanner and try again.');
        }
        setStepState('step-wait', 'done');

        setStepState('step-detect', 'active');
        setBanner('statusBanner', 'Tag detected.');
        setResult('resultBox', 'UID: ' + (presentStatus.uid || 'Unknown'), '');
        await sleep(250);
        setStepState('step-detect', 'done');

        var payload = buildPayload(presentStatus.uid);

        if (presentStatus.tag_data_valid === false) {
          setStepState('step-format', 'active');
          setBanner('statusBanner', 'Formatting blank tag\u2026');
          setResult('resultBox', 'Blank tag detected. Initializing NDEF structure.', '');
          await api('/api/format-tag', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ uid: presentStatus.uid })
          });
          await sleep(1000);
          setStepState('step-format', 'done');
        } else {
          setStepState('step-format', 'done');
        }

        setStepState('step-write', 'active');
        setBanner('statusBanner', 'Writing data\u2026');
        setResult('resultBox', 'Sending OpenPrintTag payload to scanner.', '');
        await api('/api/write-tag', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        setStepState('step-write', 'done');

        setStepState('step-verify', 'active');
        setBanner('statusBanner', 'Verifying write\u2026');
        setResult('resultBox', 'Reading tag back to confirm fields match.', '');

        var verifyDeadline = Date.now() + 15000;
        while (Date.now() < verifyDeadline) {
          await sleep(500);
          var status = await api('/api/status');
          if (status.present && valuesMatch(status, payload)) {
            setStepState('step-verify', 'done');
            setBanner('statusBanner', 'Write successful.');
            setResult('resultBox', 'OpenPrintTag data verified successfully.', 'success');
            backBtn.classList.remove('hidden');
            anotherBtn.classList.remove('hidden');
            return;
          }
        }

        setStepState('step-verify', 'error');
        throw new Error('Write timed out. Keep the tag on the scanner and try again.');
      } catch (err) {
        var msg = err && err.message ? err.message : 'Write failed';
        setBanner('statusBanner', 'Write failed.');
        setResult('resultBox', msg, 'error');

        STEP_IDS.forEach(function(id) {
          var el = document.getElementById(id);
          if (el && el.classList.contains('active')) setStepState(id, 'error');
        });

        backBtn.classList.remove('hidden');
      }
    }

    materialSearchEl.addEventListener('input', syncMaterialNameFromSelection);

    materialNameEl.addEventListener('input', function() {
      materialNameEl.value = materialNameEl.value.toUpperCase();
      materialNameEl.dataset.autoFilled = 'false';
    });

    writerForm.addEventListener('reset', function() {
      setTimeout(function() {
        document.getElementById('colorPicker').value = '#ff0000';
        document.getElementById('colorHex').value = '#FF0000';
        materialNameEl.dataset.autoFilled = 'true';
        syncMaterialNameFromSelection();
      }, 0);
    });

    writerForm.addEventListener('submit', function(e) {
      e.preventDefault();
      writeFlow();
    });

    backBtn.addEventListener('click', showCreateView);
    anotherBtn.addEventListener('click', showCreateView);

    syncMaterialNameFromSelection();

    (async function init() {
      try {
        var status = await api('/api/status');
        prefillFromStatus(status);
      } catch (e) {}
    })();
  </script>
</body>
</html>
)rawliteral";
