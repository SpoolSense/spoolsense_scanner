#pragma once

// OpenSpool writer page served at GET /writer/openspool
// Writes NDEF-wrapped OpenSpool JSON payload to NTAG215/216 via POST /api/write-openspool.

const char OPENSPOOL_WRITER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>OpenSpool Writer &mdash; SpoolSense</title>
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
      <a href="/config">Config</a>
    </nav>

    <section class="card" id="createView">
      <div class="card-head">
        <div>
          <h1 class="card-title">Create OpenSpool Tag</h1>
          <p class="card-subtitle">Write filament data to an NTAG215/216 tag in OpenSpool format.</p>
        </div>
      </div>

      <div class="card-body">
        <div id="spoolmanPicker" style="background:var(--card-alt,#1e1e35);border:1px solid var(--border);border-radius:8px;padding:12px;margin-bottom:16px"></div>
        <form id="writerForm">
          <section>
            <h2 class="section-title">Filament</h2>
            <div class="grid-2">
              <div class="field">
                <label for="brand">Brand</label>
                <input id="brand" type="text" placeholder="e.g. Sunlu" required />
              </div>
              <div class="field">
                <label for="type">Material Type</label>
                <select id="type" required>
                  <option value="PLA">PLA</option>
                  <option value="PETG">PETG</option>
                  <option value="ABS">ABS</option>
                  <option value="ASA">ASA</option>
                  <option value="TPU">TPU</option>
                  <option value="PA">Nylon (PA)</option>
                  <option value="PC">PC</option>
                  <option value="PCTG">PCTG</option>
                  <option value="HIPS">HIPS</option>
                  <option value="PVB">PVB</option>
                </select>
              </div>
            </div>
          </section>

          <section>
            <h2 class="section-title">Color</h2>
            <div class="grid-2">
              <div class="field">
                <label>Color</label>
                <div class="color-row">
                  <input id="colorPicker" type="color" value="#FF0000" />
                  <input id="colorHex" type="text" value="#FF0000" maxlength="7" placeholder="#FF0000" required />
                </div>
              </div>
            </div>
          </section>

          <section>
            <h2 class="section-title">Temperature</h2>
            <div class="grid-2">
              <div class="field">
                <label for="min_temp">Min Nozzle Temp (&deg;C)</label>
                <input id="min_temp" type="number" value="210" min="0" max="400" />
              </div>
              <div class="field">
                <label for="max_temp">Max Nozzle Temp (&deg;C)</label>
                <input id="max_temp" type="number" value="230" min="0" max="400" />
              </div>
            </div>
          </section>

          <div id="spoolmanEnrichment" class="hidden" style="margin-top:16px;padding:14px;background:var(--card-alt,#1e1e35);border:1px solid var(--blue,#4a9eff);border-radius:8px;">
            <div style="display:flex;align-items:center;gap:8px;margin-bottom:4px">
              <span style="color:var(--blue,#4a9eff);font-size:11px;text-transform:uppercase;letter-spacing:1px;font-weight:600">Spoolman Enrichment</span>
              <span id="spoolmanMatchBadge" class="hidden" style="background:#4a9eff22;border:1px solid #4a9eff55;border-radius:3px;padding:0 6px;color:#4a9eff;font-size:10px"></span>
            </div>
            <p class="card-subtitle" style="font-size:11px;margin-bottom:10px">
              Extra data Spoolman stores that OpenSpool format cannot &mdash; saved to Spoolman on write.
            </p>
            <div class="grid-2">
              <div class="field">
                <label for="enrich-bed-temp">Bed Temp (&deg;C)</label>
                <input id="enrich-bed-temp" type="number" placeholder="e.g. 60" min="0" max="150" />
              </div>
              <div class="field">
                <label for="enrich-diameter">Diameter (mm)</label>
                <input id="enrich-diameter" type="number" placeholder="1.75" min="0.1" max="5" step="0.01" />
              </div>
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
        <h1 class="card-title">Writing OpenSpool Tag</h1>
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
                <div class="step-sub">Writing OpenSpool NDEF payload to tag.</div>
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

    <div class="footer-note">OpenSpool V1.0 &mdash; NDEF JSON on NTAG215/216</div>
  </div>

  <script src="/js/shared.js?v=)rawliteral" FIRMWARE_VERSION R"rawliteral("></script>
  <script>
    var writerForm = document.getElementById('writerForm');

    syncColorPicker('colorPicker', 'colorHex');

    renderSpoolmanPicker('spoolmanPicker', {
      manufacturer: 'brand',
      material: 'type',
      color: 'colorHex',
      colorPicker: 'colorPicker',
      nozzle_min: 'min_temp',
      nozzle_max: 'max_temp',
      bed_single: 'enrich-bed-temp',
      diameter: 'enrich-diameter',
      density: 'enrich-density',
      remaining: 'enrich-remaining'
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
      expectedKind: 'OpenSpoolTag',
      wrongKindMsg: 'wrong format \u2014 expected OpenSpool',
      showMatchBadge: showMatchBadge,
      fillForm: function(status) {
        var os = status.openspool || {};
        setVal('brand', os.brand || '');
        setVal('type', os.material || '');
        if (os.color_hex) {
          setVal('colorHex', os.color_hex);
          setVal('colorPicker', os.color_hex.startsWith('#') ? os.color_hex : '#' + os.color_hex);
        }
        if (os.min_temp) setVal('min_temp', os.min_temp);
        if (os.max_temp) setVal('max_temp', os.max_temp);
      },
      fillEnrichment: function(status) {
        var sp = status.spoolman || {};
        if (sp.remaining_g !== undefined) setVal('enrich-remaining', sp.remaining_g.toFixed(1));
        if (sp.bed_temp && sp.bed_temp > 0) setVal('enrich-bed-temp', sp.bed_temp);
        if (sp.diameter_mm && sp.diameter_mm > 0) setVal('enrich-diameter', sp.diameter_mm);
        if (sp.density && sp.density > 0) setVal('enrich-density', sp.density);
      }
    });

    function buildPayload(uid) {
      var colorHex = normalizeHex(document.getElementById('colorHex').value).replace('#', '');
      return {
        uid: uid || '',
        protocol: 'openspool',
        version: '1.0',
        type: document.getElementById('type').value,
        color_hex: colorHex,
        brand: document.getElementById('brand').value.trim(),
        min_temp: document.getElementById('min_temp').value,
        max_temp: document.getElementById('max_temp').value
      };
    }

    var ENRICHMENT_FIELDS = ['enrich-remaining', 'enrich-bed-temp', 'enrich-diameter', 'enrich-density'];

    function showCreateView() {
      document.getElementById('statusView').classList.add('hidden');
      document.getElementById('createView').classList.remove('hidden');
    }

    document.getElementById('backBtn').addEventListener('click', showCreateView);
    document.getElementById('anotherBtn').addEventListener('click', function() {
      showCreateView();
      writerForm.reset();
      document.getElementById('colorPicker').value = '#FF0000';
      document.getElementById('colorHex').value = '#FF0000';
    });

    writerForm.addEventListener('submit', function(e) {
      e.preventDefault();
      sharedWriteFlow({
        stepIds: ['step-wait', 'step-detect', 'step-write', 'step-verify'],
        endpoint: '/api/write-openspool',
        formatName: 'OpenSpool',
        allowReplace: true,
        buildPayload: buildPayload,
        verify: function(status, payload) {
          if (status.tag_kind !== 'OpenSpoolTag' || !status.openspool) return false;
          var os = status.openspool;
          return os.brand === payload.brand && os.material === payload.type &&
                 (os.color_hex || '').replace('#','').toUpperCase() === (payload.color_hex || '').toUpperCase();
        },
        afterSuccess: function(uid) {
          return saveEnrichmentToSpoolman(uid, {
            tagFormat: 'openspool',
            enrichmentFieldIds: ENRICHMENT_FIELDS,
            getFields: function() {
              var nozzleMin = parseInt(document.getElementById('min_temp').value) || 0;
              var nozzleMax = parseInt(document.getElementById('max_temp').value) || 0;
              return {
                manufacturer: document.getElementById('brand').value.trim(),
                material: document.getElementById('type').value,
                colorHex: document.getElementById('colorHex').value || '',
                remainingG: parseFloat(document.getElementById('enrich-remaining').value) || 0,
                bedTemp: parseInt(document.getElementById('enrich-bed-temp').value) || 0,
                diameterMm: parseFloat(document.getElementById('enrich-diameter').value) || 1.75,
                density: parseFloat(document.getElementById('enrich-density').value) || 0,
                nozzleTemp: (nozzleMin && nozzleMax) ? Math.round((nozzleMin + nozzleMax) / 2) : (nozzleMin || nozzleMax)
              };
            }
          });
        }
      });
    });
  </script>
</body>
</html>
)rawliteral";
