#pragma once

// TigerTag writer page served at GET /writer/tigertag
// Writes 40-byte TigerTag binary format to NTAG213/215 via POST /api/write-tigertag.

const char TIGERTAG_WRITER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>TigerTag Writer &mdash; SpoolSense</title>
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
      <div class="card-head" style="display:flex;align-items:center;gap:16px">
        <img src="/img/tigertag.png" alt="TigerTag" style="height:48px;border-radius:8px" />
        <div>
          <h1 class="card-title">Create TigerTag</h1>
          <p class="card-subtitle">Write filament data to an NTAG213/215 tag in TigerTag format.</p>
        </div>
      </div>

      <div class="card-body">
        <div id="spoolmanPicker" style="background:var(--card-alt,#1e1e35);border:1px solid var(--border);border-radius:8px;padding:12px;margin-bottom:16px"></div>
        <form id="writerForm">
          <section>
            <h2 class="section-title">Filament</h2>
            <div class="grid-2">
              <div class="field">
                <label for="material_search">Material</label>
                <input id="material_search" name="material_search" list="material-list" placeholder="Type to search materials" required />
                <datalist id="material-list">
                  <option data-id="38219" value="PLA"></option>
                  <option data-id="46591" value="PLA+"></option>
                  <option data-id="38256" value="PETG"></option>
                  <option data-id="43518" value="TPU"></option>
                  <option data-id="20562" value="ABS"></option>
                  <option data-id="12844" value="ASA"></option>
                  <option data-id="30458" value="PC"></option>
                  <option data-id="15041" value="PCTG"></option>
                  <option data-id="30884" value="PP"></option>
                  <option data-id="56666" value="PA6"></option>
                  <option data-id="55796" value="PA12"></option>
                  <option data-id="59328" value="PA"></option>
                  <option data-id="26029" value="HIPS"></option>
                  <option data-id="52077" value="PET"></option>
                  <option data-id="45962" value="PVB"></option>
                  <option data-id="9483" value="PVA"></option>
                  <option data-id="29815" value="PEEK"></option>
                  <option data-id="53970" value="PEKK"></option>
                  <option data-id="56527" value="PEI"></option>
                  <option data-id="55279" value="PBT"></option>
                  <option data-id="33958" value="TPE"></option>
                  <option data-id="48310" value="PLA-CF"></option>
                  <option data-id="55418" value="PETG-CF"></option>
                  <option data-id="48815" value="PAHT-CF"></option>
                  <option data-id="12264" value="PA6-CF"></option>
                  <option data-id="10602" value="PLA Silk"></option>
                  <option data-id="48001" value="PLA Wood"></option>
                  <option data-id="65535" value="None"></option>
                </datalist>
                <input type="hidden" id="material_id" name="material_id" value="38219" />
              </div>

              <div class="field">
                <label for="brand_name">Brand</label>
                <input id="brand_name" name="brand_name" list="brand-list" placeholder="Type to search brands" required />
                <datalist id="brand-list">
                  <option data-id="65535" value="Generic"></option>
                  <option data-id="35123" value="Bambu Lab"></option>
                  <option data-id="46392" value="Prusa"></option>
                  <option data-id="50604" value="Polymaker"></option>
                  <option data-id="47930" value="eSun"></option>
                  <option data-id="51857" value="Sunlu"></option>
                  <option data-id="46203" value="Overture"></option>
                  <option data-id="3132" value="Hatchbox"></option>
                  <option data-id="26956" value="Creality"></option>
                  <option data-id="15962" value="Anycubic"></option>
                  <option data-id="57632" value="Elegoo"></option>
                  <option data-id="7812" value="Jayo"></option>
                  <option data-id="52222" value="ColorFabb"></option>
                  <option data-id="7980" value="Fillamentum"></option>
                  <option data-id="8182" value="Fiberlogy"></option>
                  <option data-id="53043" value="FormFutura"></option>
                  <option data-id="58410" value="AzureFilm"></option>
                  <option data-id="4344" value="MatterHackers"></option>
                  <option data-id="2" value="Proto-Pasta"></option>
                  <option data-id="58231" value="IC3D"></option>
                  <option data-id="39652" value="3DXTech"></option>
                  <option data-id="51443" value="BASF"></option>
                  <option data-id="9798" value="AMOLEN"></option>
                  <option data-id="28940" value="Eryone"></option>
                  <option data-id="63340" value="Flashforge"></option>
                  <option data-id="8384" value="Taulman3D"></option>
                </datalist>
                <input type="hidden" id="brand_id" name="brand_id" value="65535" />
                <div style="font-size:11px;color:var(--muted);margin-top:4px">
                  Brand not listed? Use Generic. <a href="https://github.com/TigerTag-Project/TigerTag-RFID-Guide/issues" target="_blank" rel="noopener noreferrer" style="color:var(--blue)">Request a new brand &rarr;</a>
                </div>
              </div>

              <div class="field">
                <label for="weight_g">Weight (g)</label>
                <input id="weight_g" name="weight_g" type="number" min="1" max="16777215" step="1" placeholder="1000" required />
              </div>

              <div class="field">
                <label>Color</label>
                <div class="color-row">
                  <input id="colorPicker" type="color" value="#ffffff" />
                  <input id="colorHex" name="color" type="text" value="#FFFFFF" maxlength="7" placeholder="#FFFFFF" required />
                </div>
              </div>
            </div>
          </section>

          <section>
            <h2 class="section-title">Properties</h2>
            <div class="grid-3">
              <div class="field">
                <label for="diameter_id">Diameter</label>
                <select id="diameter_id" name="diameter_id">
                  <option value="56">1.75 mm</option>
                  <option value="221">2.85 mm</option>
                </select>
              </div>

              <div class="field">
                <label for="aspect1_id">Aspect 1</label>
                <select id="aspect1_id" name="aspect1_id">
                  <option value="255">None</option>
                  <option value="104">Basic</option>
                  <option value="247">Matt</option>
                  <option value="129">Gloss</option>
                  <option value="134">Satin</option>
                  <option value="92">Silk</option>
                  <option value="126">Pearl</option>
                  <option value="64">Glitter</option>
                  <option value="67">Translucent</option>
                  <option value="21">Clear</option>
                  <option value="91">Glow</option>
                  <option value="216">Neon</option>
                  <option value="220">Pastel</option>
                  <option value="232">Marble</option>
                  <option value="238">Carbon</option>
                  <option value="123">Wood</option>
                  <option value="173">Stone</option>
                  <option value="145">Rainbow</option>
                  <option value="252">Bicolor</option>
                  <option value="24">Tricolor</option>
                  <option value="168">Thermoreactif</option>
                  <option value="97">Lithophane</option>
                </select>
              </div>

              <div class="field">
                <label for="aspect2_id">Aspect 2</label>
                <select id="aspect2_id" name="aspect2_id">
                  <option value="255">None</option>
                  <option value="104">Basic</option>
                  <option value="247">Matt</option>
                  <option value="129">Gloss</option>
                  <option value="134">Satin</option>
                  <option value="92">Silk</option>
                  <option value="126">Pearl</option>
                  <option value="64">Glitter</option>
                  <option value="67">Translucent</option>
                  <option value="21">Clear</option>
                  <option value="91">Glow</option>
                  <option value="216">Neon</option>
                  <option value="220">Pastel</option>
                  <option value="232">Marble</option>
                  <option value="238">Carbon</option>
                  <option value="123">Wood</option>
                  <option value="173">Stone</option>
                  <option value="145">Rainbow</option>
                  <option value="252">Bicolor</option>
                  <option value="24">Tricolor</option>
                  <option value="168">Thermoreactif</option>
                  <option value="97">Lithophane</option>
                </select>
              </div>
            </div>
          </section>

          <section>
            <button type="button" class="advanced-toggle" id="advancedToggle" aria-expanded="false">
              <span>Temperature settings</span>
              <span data-toggle-text>Show</span>
            </button>

            <div class="advanced-box hidden" id="advancedBox">
              <div class="grid-3">
                <div class="field">
                  <label for="nozzle_min">Nozzle Min (&deg;C)</label>
                  <input id="nozzle_min" name="nozzle_min" type="number" min="0" max="500" step="1" placeholder="190" />
                </div>
                <div class="field">
                  <label for="nozzle_max">Nozzle Max (&deg;C)</label>
                  <input id="nozzle_max" name="nozzle_max" type="number" min="0" max="500" step="1" placeholder="220" />
                </div>
                <div class="field">
                  <label for="bed_min">Bed Min (&deg;C)</label>
                  <input id="bed_min" name="bed_min" type="number" min="0" max="255" step="1" placeholder="55" />
                </div>
                <div class="field">
                  <label for="bed_max">Bed Max (&deg;C)</label>
                  <input id="bed_max" name="bed_max" type="number" min="0" max="255" step="1" placeholder="65" />
                </div>
                <div class="field">
                  <label for="dry_temp">Dry Temp (&deg;C)</label>
                  <input id="dry_temp" name="dry_temp" type="number" min="0" max="255" step="1" placeholder="45" />
                </div>
                <div class="field">
                  <label for="dry_time">Dry Time (hrs)</label>
                  <input id="dry_time" name="dry_time" type="number" min="0" max="255" step="1" placeholder="4" />
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
              Extra data Spoolman stores that TigerTag format cannot &mdash; saved to Spoolman on write.
            </p>
            <div class="grid-2">
              <div class="field">
                <label for="enrich-density">Density (g/cm&sup3;)</label>
                <input id="enrich-density" type="number" placeholder="1.24" min="0.1" max="5" step="0.001" />
              </div>
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
        <h1 class="card-title">Writing TigerTag</h1>
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
                <div class="step-sub">Place an NTAG213 or NTAG215 on the scanner.</div>
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
                <div class="step-sub">Writing TigerTag binary to pages 4-13.</div>
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

    <div class="footer-note">TigerTag V1.0 Maker format &mdash; 40 bytes, pages 4-13</div>
  </div>

  <script src="/js/shared.js?v=)rawliteral" FIRMWARE_VERSION R"rawliteral("></script>
  <script>
    var writerForm = document.getElementById('writerForm');

    syncColorPicker('colorPicker', 'colorHex');
    setupAdvancedToggle('advancedToggle', 'advancedBox');

    // Sync brand name input → hidden brand_id
    var brandNameEl = document.getElementById('brand_name');
    var brandIdEl = document.getElementById('brand_id');
    var brandList = document.getElementById('brand-list');
    if (brandNameEl && brandIdEl && brandList) {
      brandNameEl.addEventListener('input', function() {
        var opts = brandList.querySelectorAll('option');
        brandIdEl.value = '65535'; // default to Generic
        for (var i = 0; i < opts.length; i++) {
          if (opts[i].value === brandNameEl.value) {
            brandIdEl.value = opts[i].dataset.id;
            break;
          }
        }
      });
    }

    // Fetch TigerTag API data — fallback to hardcoded options on failure
    var materialData = {};  // Store full material data for auto-fill on selection

    function validateResponse(data, requiredFields) {
      if (!Array.isArray(data)) return null;
      var valid = data.filter(function(item) {
        if (typeof item !== 'object' || item === null) return false;
        return requiredFields.every(function(f) {
          return f.type === 'number' ? (typeof item[f.key] === 'number' && isFinite(item[f.key]))
               : f.type === 'string' ? (typeof item[f.key] === 'string' && item[f.key].trim().length > 0)
               : true;
        });
      });
      return valid;
    }

    function populateSelect(selectId, items, valueFn, labelFn) {
      var el = document.getElementById(selectId);
      var currentVal = el.value;
      var hardcodedCount = el.options.length;
      if (items.length < hardcodedCount) return;
      var frag = document.createDocumentFragment();
      items.forEach(function(item) {
        var opt = document.createElement('option');
        opt.value = valueFn(item);
        opt.textContent = labelFn(item);
        frag.appendChild(opt);
      });
      el.innerHTML = '';
      el.appendChild(frag);
      if (currentVal) {
        el.value = currentVal;
        if (!el.value) el.selectedIndex = 0;
      }
    }

    // Sync material search → hidden material_id
    var materialSearchEl = document.getElementById('material_search');
    var materialIdEl = document.getElementById('material_id');
    var materialListEl = document.getElementById('material-list');

    function syncMaterialId() {
      var opts = materialListEl.querySelectorAll('option');
      materialIdEl.value = '38219'; // default PLA
      for (var i = 0; i < opts.length; i++) {
        if (opts[i].value === materialSearchEl.value) {
          materialIdEl.value = opts[i].dataset.id;
          break;
        }
      }
    }

    trackAutoFill(['nozzle_min','nozzle_max','bed_min','bed_max','dry_temp','dry_time']);

    function ttSetOrClear(el, value) {
      if (!el || el.dataset.autoFilled === 'false') return;
      el.value = (value !== undefined && value !== null) ? value : '';
      el.dataset.autoFilled = 'true';
    }

    function autoFillFromMaterial() {
      syncMaterialId();
      var id = materialIdEl.value;
      var m = materialData[id];
      var r = (m && m.recommended) ? m.recommended : {};
      ttSetOrClear(document.getElementById('nozzle_min'), r.nozzleTempMin);
      ttSetOrClear(document.getElementById('nozzle_max'), r.nozzleTempMax);
      ttSetOrClear(document.getElementById('bed_min'), r.bedTempMin);
      ttSetOrClear(document.getElementById('bed_max'), r.bedTempMax);
      ttSetOrClear(document.getElementById('dry_temp'), r.dryTemp);
      ttSetOrClear(document.getElementById('dry_time'), r.dryTime);
    }

    materialSearchEl.addEventListener('input', autoFillFromMaterial);

    // Pre-fill from scanned tag if present
    prefillFromTag({
      material: 'material_search',
      color: 'colorHex',
      colorPicker: 'colorPicker',
      manufacturer: 'brand_search',
      weight: 'weight_g',
      nozzle_min: 'nozzle_min',
      nozzle_max: 'nozzle_max',
      bed_min: 'bed_min',
      bed_max: 'bed_max',
      dry_temp: 'dry_temp',
      dry_time: 'dry_time'
    }).then(function(d) {
      if (!d) return;
      // Sync hidden material_id from material name
      if (d.tigertag_material_id !== undefined) {
        document.getElementById('material_id').value = d.tigertag_material_id;
      } else {
        syncMaterialId();
      }
      // Sync hidden brand_id from brand name
      if (d.tigertag_brand_id !== undefined) {
        document.getElementById('brand_id').value = d.tigertag_brand_id;
      }
    });

    (async function loadTigerTagAPI() {
      try {
        var resp = await fetch('https://raw.githubusercontent.com/TigerTag-Project/TigerTag-RFID-Guide/main/database/id_material.json');
        if (resp.ok) {
          var raw = await resp.json();
          var materials = validateResponse(raw, [
            {key: 'id', type: 'number'},
            {key: 'label', type: 'string'}
          ]);
          if (materials) {
            materials.forEach(function(m) { materialData[m.id] = m; });
            materials.sort(function(a, b) { return a.label.localeCompare(b.label); });
            var dl = document.getElementById('material-list');
            if (dl && materials.length > dl.options.length) {
              dl.innerHTML = '';
              materials.forEach(function(m) {
                var opt = document.createElement('option');
                opt.value = m.label;
                opt.dataset.id = m.id;
                dl.appendChild(opt);
              });
            }
          }
        }
      } catch(e) { /* keep hardcoded fallback */ }

      try {
        var resp2 = await fetch('https://raw.githubusercontent.com/TigerTag-Project/TigerTag-RFID-Guide/main/database/id_brand.json');
        if (resp2.ok) {
          var raw2 = await resp2.json();
          var brands = validateResponse(raw2, [
            {key: 'id', type: 'number'},
            {key: 'name', type: 'string'}
          ]);
          if (brands) {
            brands.sort(function(a, b) { return a.name.localeCompare(b.name); });
            var dl = document.getElementById('brand-list');
            if (dl && brands.length > dl.options.length) {
              dl.innerHTML = '';
              brands.forEach(function(b) {
                var opt = document.createElement('option');
                opt.value = b.name;
                opt.dataset.id = b.id;
                dl.appendChild(opt);
              });
            }
          }
        }
      } catch(e) { /* keep hardcoded fallback */ }
    })();

    function intVal(id, fallback) {
      var raw = document.getElementById(id).value.trim();
      if (raw === '') return fallback;
      var n = parseInt(raw, 10);
      return isNaN(n) ? fallback : n;
    }

    function buildPayload(uid) {
      var color = normalizeHex(document.getElementById('colorHex').value);
      if (!color) throw new Error('Color must be a valid 6-digit hex value');

      var r = parseInt(color.substr(1,2), 16);
      var g = parseInt(color.substr(3,2), 16);
      var b = parseInt(color.substr(5,2), 16);

      return {
        uid: uid || '',
        material_id: intVal('material_id', 38219),
        brand_id: intVal('brand_id', 65535),
        weight_g: intVal('weight_g', 1000),
        color_r: r,
        color_g: g,
        color_b: b,
        color_a: 255,
        diameter_id: intVal('diameter_id', 56),
        aspect1_id: intVal('aspect1_id', 255),
        aspect2_id: intVal('aspect2_id', 255),
        nozzle_min: intVal('nozzle_min', 0),
        nozzle_max: intVal('nozzle_max', 0),
        bed_min: intVal('bed_min', 0),
        bed_max: intVal('bed_max', 0),
        dry_temp: intVal('dry_temp', 0),
        dry_time: intVal('dry_time', 0)
      };
    }

    var ENRICHMENT_FIELDS = ['enrich-remaining', 'enrich-density'];

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
        endpoint: '/api/write-tigertag',
        formatName: 'TigerTag',
        allowReplace: true,
        buildPayload: buildPayload,
        verify: function(status, payload) {
          if (status.tag_kind !== 'TigerTag' || !status.tigertag) return false;
          var tt = status.tigertag;
          return tt.material_id === payload.material_id &&
                 tt.brand_id === payload.brand_id &&
                 tt.weight_g === payload.weight_g;
        },
        afterSuccess: function(uid) {
          return saveEnrichmentToSpoolman(uid, {
            tagFormat: 'tigertag',
            enrichmentFieldIds: ENRICHMENT_FIELDS,
            getFields: function() {
              var nozzleMin = parseInt(document.getElementById('nozzle_min').value) || 0;
              var nozzleMax = parseInt(document.getElementById('nozzle_max').value) || 0;
              var bedMin = parseInt(document.getElementById('bed_min').value) || 0;
              var bedMax = parseInt(document.getElementById('bed_max').value) || 0;
              return {
                manufacturer: document.getElementById('brand_name').value.trim(),
                material: document.getElementById('material_search').value.trim(),
                colorHex: document.getElementById('colorHex').value || '',
                remainingG: parseFloat(document.getElementById('enrich-remaining').value) || 0,
                density: parseFloat(document.getElementById('enrich-density').value) || 0,
                nozzleTemp: (nozzleMin && nozzleMax) ? Math.round((nozzleMin + nozzleMax) / 2) : (nozzleMin || nozzleMax),
                bedTemp: (bedMin && bedMax) ? Math.round((bedMin + bedMax) / 2) : (bedMin || bedMax),
                diameterMm: (parseInt(document.getElementById('diameter_id').value) || 56) === 221 ? 2.85 : 1.75
              };
            }
          });
        }
      });
    });

    renderSpoolmanPicker('spoolmanPicker', {
      material: 'material_search',
      manufacturer: 'brand_name',
      color: 'colorHex',
      colorPicker: 'colorPicker',
      remaining: 'weight_g',
      diameter: 'diameter_id',
      nozzle_min: 'nozzle_min',
      nozzle_max: 'nozzle_max',
      bed_min: 'bed_min',
      bed_max: 'bed_max',
      density: 'enrich-density'
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
      expectedKind: 'TigerTag',
      wrongKindMsg: 'wrong format \u2014 expected TigerTag',
      showMatchBadge: showMatchBadge,
      fillForm: function(status) {
        var tt = status.tigertag || {};
        setVal('material_search', tt.material_name || '');
        setVal('brand_name', tt.brand_name || '');
        if (tt.color_hex) {
          var c = tt.color_hex.startsWith('#') ? tt.color_hex : '#' + tt.color_hex;
          setVal('colorHex', c);
          setVal('colorPicker', c);
        }
        if (tt.weight_g) setVal('weight_g', tt.weight_g);
        if (tt.nozzle_temp_min) setVal('nozzle_min', tt.nozzle_temp_min);
        if (tt.nozzle_temp_max) setVal('nozzle_max', tt.nozzle_temp_max);
        if (tt.bed_temp_min) setVal('bed_min', tt.bed_temp_min);
        if (tt.bed_temp_max) setVal('bed_max', tt.bed_temp_max);
        if (tt.dry_temp) setVal('dry_temp', tt.dry_temp);
        if (tt.dry_time_hours) setVal('dry_time', tt.dry_time_hours);
        if (tt.material_id !== undefined) document.getElementById('material_id').value = tt.material_id;
        if (tt.brand_id !== undefined) document.getElementById('brand_id').value = tt.brand_id;
        if (tt.diameter_mm) {
          var dVal = tt.diameter_mm < 2 ? '56' : '221';
          document.getElementById('diameter_id').value = dVal;
        }
        function selectByText(selId, name) {
          if (!name) return;
          var sel = document.getElementById(selId);
          for (var i = 0; i < sel.options.length; i++) {
            if (sel.options[i].text === name) { sel.value = sel.options[i].value; return; }
          }
        }
        selectByText('aspect1_id', tt.aspect1_name);
        selectByText('aspect2_id', tt.aspect2_name);
        syncMaterialId();
      },
      fillEnrichment: function(status) {
        var sp = status.spoolman || {};
        if (sp.remaining_g !== undefined) setVal('enrich-remaining', sp.remaining_g.toFixed(1));
        if (sp.density && sp.density > 0) setVal('enrich-density', sp.density);
      }
    });

  </script>
</body>
</html>
)rawliteral";
