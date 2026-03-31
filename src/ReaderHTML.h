#pragma once

// Tag Reader page served at GET /reader
// Auto-detects tag format and displays all data read-only.

const char READER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Tag Reader &mdash; SpoolSense</title>
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

    <section class="card">
      <div class="card-head">
        <h1 class="card-title">Tag Reader</h1>
        <p class="card-subtitle">Place a tag on the scanner to read its contents.</p>
      </div>

      <div class="card-body">
        <!-- No tag state -->
        <div id="noTag">
          <div class="status-banner">Waiting for tag&hellip;</div>
        </div>

        <!-- Tag present -->
        <div id="tagView" class="hidden">
          <div class="tag-info" id="tagFields"></div>
        </div>

        <!-- Spool picker for NFC+ link/re-assign -->
        <div id="spoolPicker" class="hidden" style="margin-top:16px">
          <div style="display:flex;gap:8px;align-items:center;margin-bottom:10px">
            <input type="text" id="spoolSearch" placeholder="Search spools..." style="flex:1;padding:8px 12px;border-radius:8px;border:1px solid var(--border);background:var(--card);color:var(--text);font-size:0.95em" />
          </div>
          <div id="spoolResults" style="max-height:240px;overflow-y:auto"></div>
          <div id="linkResult" class="hidden" style="margin-top:10px;padding:10px;border-radius:8px;text-align:center;font-weight:600"></div>
        </div>
      </div>
    </section>

    <div class="actions" style="margin-top:16px;justify-content:center">
      <button type="button" class="btn-primary hidden" id="scanBtn">Scan Again</button>
    </div>

    <div class="footer-note">Stops polling once a tag is detected.</div>
  </div>

  <script src="/js/shared.js"></script>
  <script>
    var noTag = document.getElementById('noTag');
    var tagView = document.getElementById('tagView');
    var tagFields = document.getElementById('tagFields');
    var lastJson = '';
    var pollTimer = null;
    var tagFound = false;

    function row(label, value) {
      return '<div class="tag-row"><span class="label">' + label + '</span><span class="value">' + value + '</span></div>';
    }

    function colorValue(hex) {
      if (!hex) return '&mdash;';
      return '<span class="color-swatch" style="background:' + hex + '"></span>' + hex;
    }

    function renderOpenPrintTag(s) {
      var html = '';
      html += row('Format', 'OpenPrintTag');
      html += row('UID', s.uid || '&mdash;');
      html += row('Material Type', s.material_name || ('Type ' + s.material_type));
      if (s.color) html += row('Color', colorValue(s.color));
      if (s.manufacturer) html += row('Manufacturer', s.manufacturer);
      html += row('Initial Weight', (s.initial_weight_g !== undefined ? s.initial_weight_g + ' g' : '&mdash;'));
      html += row('Remaining', (s.remaining_g !== undefined ? s.remaining_g.toFixed(1) + ' g' : '&mdash;'));
      if (s.density) html += row('Density', s.density + ' g/cm\u00B3');
      if (s.diameter_mm) html += row('Diameter', s.diameter_mm + ' mm');
      if (s.min_print_temp) html += row('Print Temp', s.min_print_temp + ' \u2013 ' + (s.max_print_temp || '?') + ' \u00B0C');
      if (s.preheat_temp) html += row('Preheat', s.preheat_temp + ' \u00B0C');
      if (s.min_bed_temp) html += row('Bed Temp', s.min_bed_temp + ' \u2013 ' + (s.max_bed_temp || '?') + ' \u00B0C');
      if (s.spoolman_id > 0) html += row('Spoolman ID', s.spoolman_id);
      return html;
    }

    function renderTigerTag(s) {
      var t = s.tigertag || {};
      var html = '';
      html += row('Format', 'TigerTag');
      html += row('UID', s.uid || '&mdash;');
      if (t.material_name) html += row('Material', t.material_name);
      if (t.brand_name) html += row('Brand', t.brand_name);
      if (t.color_hex) html += row('Color', colorValue(t.color_hex));
      if (t.weight_g !== undefined) html += row('Weight', t.weight_g + ' g');
      if (t.diameter_mm) html += row('Diameter', t.diameter_mm + ' mm');
      if (t.aspect1_name && t.aspect1_name !== 'Unknown') html += row('Aspect', t.aspect1_name + (t.aspect2_name && t.aspect2_name !== 'Unknown' ? ' / ' + t.aspect2_name : ''));
      if (t.nozzle_temp_min) html += row('Nozzle Temp', t.nozzle_temp_min + ' \u2013 ' + (t.nozzle_temp_max || '?') + ' \u00B0C');
      if (t.bed_temp_min) html += row('Bed Temp', t.bed_temp_min + ' \u2013 ' + (t.bed_temp_max || '?') + ' \u00B0C');
      if (t.dry_temp) html += row('Dry', t.dry_temp + ' \u00B0C / ' + (t.dry_time_hours || '?') + ' hrs');
      return html;
    }

    function renderOpenTag3D(s) {
      var t = s.opentag3d || {};
      var html = '';
      html += row('Format', 'OpenTag3D');
      html += row('UID', s.uid || '&mdash;');
      if (t.base_material) html += row('Material', t.base_material + (t.modifiers ? ' (' + t.modifiers + ')' : ''));
      if (t.manufacturer) html += row('Manufacturer', t.manufacturer);
      if (t.color_hex) html += row('Color', colorValue(t.color_hex) + (t.color_name ? ' ' + t.color_name : ''));
      if (t.target_weight_g !== undefined) html += row('Weight', t.target_weight_g + ' g');
      if (t.measured_weight_g) html += row('Measured Weight', t.measured_weight_g + ' g');
      if (t.diameter_mm) html += row('Diameter', t.diameter_mm + ' mm');
      if (t.density) html += row('Density', t.density.toFixed(3) + ' g/cm\u00B3');
      if (t.print_temp) {
        var tempStr = t.print_temp + ' \u00B0C';
        if (t.min_print_temp && t.max_print_temp) tempStr = t.min_print_temp + ' \u2013 ' + t.max_print_temp + ' \u00B0C';
        html += row('Print Temp', tempStr);
      }
      if (t.bed_temp) {
        var bedStr = t.bed_temp + ' \u00B0C';
        if (t.min_bed_temp && t.max_bed_temp) bedStr = t.min_bed_temp + ' \u2013 ' + t.max_bed_temp + ' \u00B0C';
        html += row('Bed Temp', bedStr);
      }
      if (t.dry_temp) html += row('Dry', t.dry_temp + ' \u00B0C / ' + (t.dry_time_hours || '?') + ' hrs');
      if (t.serial_number) html += row('Serial', t.serial_number);
      if (t.empty_spool_g) html += row('Empty Spool', t.empty_spool_g + ' g');
      return html;
    }

    function renderGenericUid(s) {
      var html = '';
      html += row('Format', tagKindLabel(s.tag_kind));
      html += row('UID', s.uid || '&mdash;');
      if (s.material_name) html += row('Material', s.material_name);
      if (s.manufacturer)  html += row('Manufacturer', s.manufacturer);
      if (s.color)         html += row('Color', '<span class="color-swatch" style="background:' + s.color + '"></span> ' + s.color);
      if (s.remaining_g !== undefined) html += row('Remaining', s.remaining_g.toFixed(1) + ' g');
      if (s.spoolman_id > 0) html += row('Spoolman ID', s.spoolman_id);
      if (s.extruder_temp > 0) html += row('Extruder Temp', s.extruder_temp + ' &deg;C');
      if (s.bed_temp > 0) html += row('Bed Temp', s.bed_temp + ' &deg;C');
      if (!s.material_name && !s.tag_data_valid) html += row('Data', '<em>Looking up in Spoolman&hellip; keep tag on reader</em>');
      // Link/Re-assign button — show once Spoolman lookup is done (success or fail)
      if (s.uid && (s.material_name || s.tag_data_valid === false)) {
        var btnLabel = s.spoolman_id > 0 ? 'Re-assign Spool' : 'Link to Spool';
        html += '<div style="margin-top:14px;text-align:center"><button onclick="showSpoolPicker(\'' + (s.uid || '') + '\',' + (s.spoolman_id || -1) + ')" class="btn-primary" style="font-size:0.9em;padding:8px 20px">' + btnLabel + '</button></div>';
      }
      return html;
    }

    // --- Spool picker for NFC+ link/re-assign ---
    var spoolmanUrl = '';
    var allSpools = [];
    var currentTagUid = '';
    var currentSpoolmanId = -1;

    // Fetch Spoolman URL from scanner config
    api('/api/config').then(function(cfg) {
      if (cfg.spoolman_url) spoolmanUrl = cfg.spoolman_url.replace(/\/+$/, '');
    }).catch(function(){});

    function fetchSpools() {
      return fetch('/api/spoolman/spools')
        .then(function(r) { return r.ok ? r.json() : []; })
        .catch(function() { return []; });
    }

    function esc(s) {
      var d = document.createElement('div'); d.textContent = s; return d.innerHTML;
    }

    function renderSpoolRow(spool) {
      var fil = spool.filament || {};
      var vendor = fil.vendor ? esc(fil.vendor.name) : '';
      var color = (fil.color_hex || '').replace(/[^0-9a-fA-F]/g, '');
      var colorSwatch = color ? '<span class="color-swatch" style="background:#' + color + ';display:inline-block;width:14px;height:14px;border-radius:50%;vertical-align:middle;margin-right:6px"></span>' : '';
      var remaining = spool.remaining_weight ? Math.round(spool.remaining_weight) + 'g' : '?';
      var label = colorSwatch + '#' + spool.id + ' ' + (vendor ? vendor + ' ' : '') + esc(fil.material || fil.name || '?') + ' — ' + remaining;
      return '<div style="display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border-bottom:1px solid var(--border)">'
           + '<span style="font-size:0.9em">' + label + '</span>'
           + '<button onclick="linkSpool(' + spool.id + ')" style="padding:4px 14px;border-radius:6px;border:1px solid var(--accent);background:transparent;color:var(--accent);cursor:pointer;font-size:0.85em;font-weight:600">'
           + (currentSpoolmanId > 0 ? 'Re-assign' : 'Link') + '</button>'
           + '</div>';
    }

    function showSpoolPicker(uid, spoolmanId) {
      currentTagUid = uid;
      currentSpoolmanId = spoolmanId;
      var picker = document.getElementById('spoolPicker');
      var results = document.getElementById('spoolResults');
      var linkResult = document.getElementById('linkResult');
      linkResult.classList.add('hidden');
      picker.classList.remove('hidden');

      fetchSpools().then(function(spools) {
        allSpools = spools;
        filterSpools('');
      });
    }

    function filterSpools(query) {
      var results = document.getElementById('spoolResults');
      var q = query.toLowerCase();
      var filtered = allSpools.filter(function(s) {
        var fil = s.filament || {};
        var vendor = fil.vendor ? fil.vendor.name : '';
        var text = (vendor + ' ' + (fil.material || '') + ' ' + (fil.name || '')).toLowerCase();
        return !q || text.indexOf(q) >= 0;
      });
      if (filtered.length === 0) {
        results.innerHTML = '<div style="padding:12px;opacity:0.5;text-align:center">No spools found</div>';
      } else {
        results.innerHTML = filtered.slice(0, 20).map(renderSpoolRow).join('');
      }
    }

    document.getElementById('spoolSearch').addEventListener('input', function() {
      filterSpools(this.value);
    });

    window.linkSpool = function(newSpoolId) {
      if (!currentTagUid) return;
      var linkResult = document.getElementById('linkResult');
      linkResult.textContent = 'Linking...';
      linkResult.className = '';
      linkResult.classList.remove('hidden');

      fetch('/api/spoolman/link', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          spool_id: newSpoolId,
          nfc_id: currentTagUid,
          old_spool_id: currentSpoolmanId
        })
      }).then(function(r) {
        return r.json().then(function(data) {
          if (data.success) {
            linkResult.textContent = 'Linked! Remove and re-scan tag to verify.';
            linkResult.style.color = '#22c55e';
            document.getElementById('spoolResults').innerHTML = '';
            document.getElementById('spoolSearch').value = '';
          } else {
            throw new Error(data.error || 'Link failed');
          }
        });
      }).catch(function(err) {
        linkResult.textContent = 'Failed: ' + err.message;
        linkResult.style.color = '#ef4444';
      });
    };

    function render(s) {
      if (!s.present) {
        noTag.classList.remove('hidden');
        tagView.classList.add('hidden');
        return;
      }

      noTag.classList.add('hidden');
      tagView.classList.remove('hidden');

      var html;
      var kind = s.tag_kind || '';

      if (kind === 'TigerTag' && s.tigertag) {
        html = renderTigerTag(s);
      } else if (kind === 'OpenTag3D' && s.opentag3d) {
        html = renderOpenTag3D(s);
      } else if (kind === 'OpenPrintTag' || (s.tag_data_valid && !kind)) {
        html = renderOpenPrintTag(s);
      } else {
        html = renderGenericUid(s);
      }

      tagFields.innerHTML = html;
    }

    function stopPolling() {
      if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
    }

    function startPolling() {
      stopPolling();
      tagFound = false;
      lastJson = '';
      noTag.classList.remove('hidden');
      tagView.classList.add('hidden');
      scanBtn.classList.add('hidden');
      document.getElementById('spoolPicker').classList.add('hidden');
      pollTimer = setInterval(poll, 1000);
      poll();
    }

    function poll() {
      api('/api/status').then(function(s){
        if (s.present) {
          render(s);
          // For generic UID tags, keep polling until Spoolman data arrives
          var isGenericPending = (s.tag_kind === 'GenericUidTag') && !s.material_name;
          if (!tagFound && !isGenericPending) {
            tagFound = true;
            stopPolling();
            scanBtn.classList.remove('hidden');
          } else if (!tagFound) {
            // Still waiting for Spoolman lookup — keep polling
            render(s);
          }
        } else if (!tagFound) {
          render(s);
        }
      }).catch(function(){});
    }

    var scanBtn = document.getElementById('scanBtn');
    scanBtn.addEventListener('click', startPolling);

    startPolling();
  </script>
</body>
</html>
)rawliteral";
