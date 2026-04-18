#pragma once

// Device configuration page served at GET /config
// Allows changing WiFi, MQTT, Spoolman, and hardware settings via web UI.

const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Configuration &mdash; SpoolSense</title>
  <link rel="stylesheet" href="/css/shared.css?v=)rawliteral" FIRMWARE_VERSION R"rawliteral(" />
  <style>
    .toggle-row{
      display:flex;align-items:center;justify-content:space-between;
      padding:12px 14px;border:1px solid var(--border);border-radius:12px;
      background:rgba(255,255,255,.02);
    }
    .toggle-label{font-size:14px;font-weight:700;color:#e5e7eb}
    .toggle-switch{position:relative;width:44px;height:24px;cursor:pointer}
    .toggle-switch input{opacity:0;width:0;height:0}
    .toggle-track{
      position:absolute;top:0;left:0;right:0;bottom:0;
      background:#23262f;border-radius:12px;border:1px solid var(--border);
      transition:background .2s;
    }
    .toggle-track:before{
      content:'';position:absolute;width:18px;height:18px;left:3px;top:2px;
      background:#fff;border-radius:50%;transition:transform .2s;
    }
    .toggle-switch input:checked+.toggle-track{background:var(--green);border-color:var(--green)}
    .toggle-switch input:checked+.toggle-track:before{transform:translateX(19px)}
    .pass-wrap{position:relative}
    .pass-wrap input{padding-right:48px}
    .pass-toggle{
      position:absolute;right:12px;top:50%;transform:translateY(-50%);
      background:none;border:none;color:var(--muted);cursor:pointer;
      font-size:12px;font-weight:700;padding:4px;
    }
  </style>
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

    <div id="apBanner" class="hidden" style="background:#f59e0b;color:#000;padding:12px 16px;border-radius:12px;margin-bottom:16px;text-align:center;font-weight:700;">
      Setup Mode &mdash; Connect to your WiFi network below.<br>
      <span id="apBannerSSID" style="font-weight:400;font-size:0.9em;"></span>
    </div>

    <section class="card">
      <div class="card-head">
        <h1 class="card-title">Device Configuration</h1>
        <p class="card-subtitle">Change settings and save to apply. Device will reboot after saving.</p>
      </div>

      <div class="card-body">
        <form id="configForm">
          <section>
            <h2 class="section-title">Network</h2>
            <div class="field">
              <label for="hostname">Hostname</label>
              <input id="hostname" type="text" placeholder="spoolsense" maxlength="32"
                     pattern="[a-z0-9](?:[a-z0-9\-]{0,30}[a-z0-9])?" title="Lowercase letters, numbers, hyphens (1-32 chars, no leading/trailing hyphens)" />
              <div style="font-size:11px;color:#71717A;margin-top:4px">mDNS hostname (e.g. spoolsense-lane1). Access at http://&lt;hostname&gt;.local</div>
            </div>
            <div class="field">
              <label for="low_spool_g">Low Spool Threshold (g)</label>
              <input id="low_spool_g" type="number" min="0" max="5000" value="100" />
              <div style="font-size:11px;color:#71717A;margin-top:4px">LED breathes when remaining weight is at or below this value</div>
            </div>
            <h3 style="font-size:13px;color:#A1A1AA;margin:16px 0 8px;font-weight:600">WiFi</h3>
            <div class="grid-2">
              <div class="field">
                <label for="wifi_ssid">SSID</label>
                <input id="wifi_ssid" type="text" placeholder="Your WiFi network" required />
              </div>
              <div class="field">
                <label for="wifi_pass">Password</label>
                <div class="pass-wrap">
                  <input id="wifi_pass" type="password" placeholder="Leave blank to keep current" />
                  <button type="button" class="pass-toggle" onclick="togglePass('wifi_pass',this)">Show</button>
                </div>
              </div>
            </div>
            <div class="toggle-row" style="margin-top:10px">
              <div>
                <span id="wifi_keep_awake_label" class="toggle-label">Keep WiFi radio awake</span>
                <div style="font-size:11px;color:#71717A;margin-top:4px">Disables modem sleep. Improves RSSI and response time on weak signals, uses slightly more power.</div>
              </div>
              <label class="toggle-switch">
                <input type="checkbox" id="wifi_keep_awake" aria-labelledby="wifi_keep_awake_label" />
                <span class="toggle-track"></span>
              </label>
            </div>
          </section>

          <section>
            <h2 class="section-title">MQTT / Home Assistant</h2>
            <div class="grid-2">
              <div class="field">
                <label for="mqtt_host">Broker Host</label>
                <input id="mqtt_host" type="text" placeholder="192.168.1.100" />
              </div>
              <div class="field">
                <label for="mqtt_port">Port</label>
                <input id="mqtt_port" type="number" min="1" max="65535" value="1883" />
              </div>
              <div class="field">
                <label for="mqtt_user">Username</label>
                <input id="mqtt_user" type="text" placeholder="Optional" />
              </div>
              <div class="field">
                <label for="mqtt_pass">Password</label>
                <div class="pass-wrap">
                  <input id="mqtt_pass" type="password" placeholder="Leave blank to keep current" />
                  <button type="button" class="pass-toggle" onclick="togglePass('mqtt_pass',this)">Show</button>
                </div>
              </div>
            </div>
          </section>

          <section>
            <h2 class="section-title">Spoolman</h2>
            <div class="toggle-row" style="margin-bottom:14px">
              <span id="spoolman_on_label" class="toggle-label">Enable Spoolman</span>
              <label class="toggle-switch">
                <input type="checkbox" id="spoolman_on" aria-labelledby="spoolman_on_label" />
                <span class="toggle-track"></span>
              </label>
            </div>
            <div class="field" id="spoolman_url_field">
              <label for="spoolman_url">Spoolman URL</label>
              <input id="spoolman_url" type="text" placeholder="http://spoolman.local:7912" />
            </div>
          </section>

          <section>
            <h2 class="section-title">Klipper / Moonraker</h2>
            <div class="hint" style="margin-bottom:12px">Connect to a Klipper printer via Moonraker for keypad-based tool assignment (ASSIGN_SPOOL). Leave blank if not used.</div>
            <div class="field">
              <label for="moonraker_url">Moonraker URL</label>
              <input id="moonraker_url" type="text" maxlength="127" placeholder="http://printer.local:7125" />
            </div>
          </section>

          <section>
            <h2 class="section-title">PrusaLink</h2>
            <div class="hint" style="margin-bottom:12px">Connect to a Prusa printer for automatic filament tracking. Get the API key from your printer's web interface.</div>
            <div class="toggle-row" style="margin-bottom:14px">
              <span id="prusalink_on_label" class="toggle-label">Enable PrusaLink</span>
              <label class="toggle-switch">
                <input type="checkbox" id="prusalink_on" aria-labelledby="prusalink_on_label" />
                <span class="toggle-track"></span>
              </label>
            </div>
            <div id="prusalink_fields" style="display:none">
              <div class="field">
                <label for="prusalink_url">PrusaLink URL</label>
                <input id="prusalink_url" type="text" placeholder="http://192.168.1.100" />
              </div>
              <div class="field">
                <label for="prusalink_api_key">API Key</label>
                <input id="prusalink_api_key" type="password" placeholder="Enter PrusaLink API key" />
              </div>
            </div>
          </section>

          <section>
            <h2 class="section-title">Automation</h2>
            <div class="field">
              <label for="auto_mode">Mode</label>
              <select id="auto_mode">
                <option value="0">Self Directed &mdash; scanner auto-deducts filament weight</option>
                <option value="1">Controlled by HA &mdash; Home Assistant controls deduction</option>
              </select>
            </div>
          </section>

          <section>
            <h2 class="section-title">Hardware</h2>
            <div class="hint" style="margin-bottom:12px">Enable or disable optional hardware peripherals. Changes take effect after reboot.</div>
            <div style="display:grid;gap:10px">
              <div class="toggle-row">
                <span id="lcd_enabled_label" class="toggle-label">LCD Display</span>
                <label class="toggle-switch">
                  <input type="checkbox" id="lcd_enabled" aria-labelledby="lcd_enabled_label" />
                  <span class="toggle-track"></span>
                </label>
              </div>
              <div class="toggle-row">
                <span id="led_enabled_label" class="toggle-label">Status LED</span>
                <label class="toggle-switch">
                  <input type="checkbox" id="led_enabled" aria-labelledby="led_enabled_label" />
                  <span class="toggle-track"></span>
                </label>
              </div>
              <div class="toggle-row">
                <span id="keypad_enabled_label" class="toggle-label">3x4 Matrix Keypad</span>
                <label class="toggle-switch">
                  <input type="checkbox" id="keypad_enabled" aria-labelledby="keypad_enabled_label" />
                  <span class="toggle-track"></span>
                </label>
              </div>
              <div class="toggle-row">
                <span id="tft_enabled_label" class="toggle-label">TFT Display (240x240)</span>
                <label class="toggle-switch">
                  <input type="checkbox" id="tft_enabled" aria-labelledby="tft_enabled_label" />
                  <span class="toggle-track"></span>
                </label>
              </div>
              <div class="toggle-row" id="tft_driver_row" style="display:none">
                <span id="tft_driver_label" class="toggle-label">TFT Driver</span>
                <select id="tft_driver" aria-labelledby="tft_driver_label" style="padding:6px 10px;border-radius:6px;border:1px solid var(--border);background:var(--card);color:var(--text);font-size:0.95em">
                  <option value="st7789">ST7789 (square)</option>
                  <option value="gc9a01">GC9A01 (round)</option>
                </select>
              </div>
            </div>
            <div style="display:grid;gap:10px">
              <div class="toggle-row">
                <span id="bambu_dashboard_label" class="toggle-label">Bambu AMS Dashboard (TFT)</span>
                <label class="toggle-switch">
                  <input type="checkbox" id="bambu_dashboard" aria-labelledby="bambu_dashboard_label" />
                  <span class="toggle-track"></span>
                </label>
              </div>
              <div class="toggle-row">
                <span id="nfc_reader_label" class="toggle-label">NFC Reader</span>
                <select id="nfc_reader" aria-labelledby="nfc_reader_label" style="padding:6px 10px;border-radius:6px;border:1px solid var(--border);background:var(--card);color:var(--text);font-size:0.95em">
                  <option value="pn5180">PN5180 (ISO15693 + ISO14443A)</option>
                  <option value="pn532">PN532 (ISO14443A only)</option>
                </select>
              </div>
            </div>
          </section>

          <div id="saveResult" class="hidden"></div>

          <div class="actions">
            <button type="submit" class="btn-primary" id="saveBtn">Save &amp; Reboot</button>
          </div>
        </form>
      </div>
    </section>

    <div class="footer-note">Settings are saved to NVS and persist across OTA updates.</div>
  </div>

  <script src="/js/shared.js?v=)rawliteral" FIRMWARE_VERSION R"rawliteral("></script>
  <script>
    function togglePass(id, btn) {
      var el = document.getElementById(id);
      if (el.type === 'password') { el.type = 'text'; btn.textContent = 'Hide'; }
      else { el.type = 'password'; btn.textContent = 'Show'; }
    }

    // Load current config
    api('/api/config').then(function(cfg) {
      maybeSetValue('hostname', cfg.hostname);
      if (cfg.low_spool_threshold_g !== undefined) document.getElementById('low_spool_g').value = cfg.low_spool_threshold_g;
      maybeSetValue('wifi_ssid', cfg.wifi_ssid);
      maybeSetValue('mqtt_host', cfg.mqtt_host);
      maybeSetValue('mqtt_port', cfg.mqtt_port);
      maybeSetValue('mqtt_user', cfg.mqtt_user);
      maybeSetValue('spoolman_url', cfg.spoolman_url);
      maybeSetValue('auto_mode', cfg.auto_mode);
      document.getElementById('spoolman_on').checked = !!cfg.spoolman_on;
      document.getElementById('prusalink_on').checked = !!cfg.prusalink_on;
      maybeSetValue('prusalink_url', cfg.prusalink_url);
      if (cfg.prusalink_key_set) document.getElementById('prusalink_api_key').placeholder = '(set) Leave blank to keep';
      document.getElementById('prusalink_fields').style.display = cfg.prusalink_on ? '' : 'none';
      document.getElementById('lcd_enabled').checked = !!cfg.lcd_enabled;
      document.getElementById('led_enabled').checked = !!cfg.led_enabled;
      document.getElementById('keypad_enabled').checked = !!cfg.keypad_enabled;
      document.getElementById('tft_enabled').checked = !!cfg.tft_enabled;
      if (cfg.tft_driver) document.getElementById('tft_driver').value = cfg.tft_driver;
      document.getElementById('tft_driver_row').style.display = cfg.tft_enabled ? '' : 'none';
      document.getElementById('tft_enabled').addEventListener('change', function() {
        document.getElementById('tft_driver_row').style.display = this.checked ? '' : 'none';
      });
      if (cfg.nfc_reader) document.getElementById('nfc_reader').value = cfg.nfc_reader;
      document.getElementById('bambu_dashboard').checked = !!cfg.bambu_dashboard;
      document.getElementById('wifi_keep_awake').checked = !!cfg.wifi_keep_awake;
      maybeSetValue('moonraker_url', cfg.moonraker_url);
      // Password placeholders
      if (cfg.wifi_pass_set) document.getElementById('wifi_pass').placeholder = '(set) Leave blank to keep';
      if (cfg.mqtt_pass_set) document.getElementById('mqtt_pass').placeholder = '(set) Leave blank to keep';
      // Show AP mode banner
      if (cfg.ap_mode) {
        var banner = document.getElementById('apBanner');
        banner.classList.remove('hidden');
        var ssidText = cfg.ap_ssid ? ('Network: ' + cfg.ap_ssid + ' | ') : '';
        document.getElementById('apBannerSSID').textContent = ssidText + 'IP: 192.168.4.1';
      }
    }).catch(function() {});

    // Show/hide Spoolman URL based on toggle
    document.getElementById('spoolman_on').addEventListener('change', function() {
      document.getElementById('spoolman_url_field').style.display = this.checked ? '' : 'none';
    });

    // Show/hide PrusaLink fields based on toggle
    document.getElementById('prusalink_on').addEventListener('change', function() {
      document.getElementById('prusalink_fields').style.display = this.checked ? '' : 'none';
    });

    function normalizeHostname(v) {
      return (v || '').trim().toLowerCase().replace(/[^a-z0-9-]/g, '').replace(/^-+|-+$/g, '').slice(0, 32) || 'spoolsense';
    }

    document.getElementById('configForm').addEventListener('submit', function(e) {
      e.preventDefault();
      var normalizedHostname = normalizeHostname(document.getElementById('hostname').value);
      document.getElementById('hostname').value = normalizedHostname;
      var btn = document.getElementById('saveBtn');
      var result = document.getElementById('saveResult');
      btn.disabled = true;
      btn.textContent = 'Saving...';

      var body = {
        hostname: normalizedHostname,
        low_spool_threshold_g: parseInt(document.getElementById('low_spool_g').value) || 100,
        wifi_ssid: document.getElementById('wifi_ssid').value.trim(),
        wifi_pass: document.getElementById('wifi_pass').value,
        mqtt_host: document.getElementById('mqtt_host').value.trim(),
        mqtt_port: parseInt(document.getElementById('mqtt_port').value) || 1883,
        mqtt_user: document.getElementById('mqtt_user').value.trim(),
        mqtt_pass: document.getElementById('mqtt_pass').value,
        spoolman_on: document.getElementById('spoolman_on').checked ? 1 : 0,
        spoolman_url: document.getElementById('spoolman_url').value.trim(),
        auto_mode: parseInt(document.getElementById('auto_mode').value) || 0,
        lcd_enabled: document.getElementById('lcd_enabled').checked ? 1 : 0,
        led_enabled: document.getElementById('led_enabled').checked ? 1 : 0,
        keypad_enabled: document.getElementById('keypad_enabled').checked ? 1 : 0,
        tft_enabled: document.getElementById('tft_enabled').checked ? 1 : 0,
        tft_driver: document.getElementById('tft_driver').value,
        nfc_reader: document.getElementById('nfc_reader').value,
        moonraker_url: document.getElementById('moonraker_url').value.trim(),
        prusalink_on: document.getElementById('prusalink_on').checked ? 1 : 0,
        prusalink_url: document.getElementById('prusalink_url').value.trim(),
        prusalink_api_key: document.getElementById('prusalink_api_key').value,
        bambu_dashboard: document.getElementById('bambu_dashboard').checked ? 1 : 0,
        wifi_keep_awake: document.getElementById('wifi_keep_awake').checked ? 1 : 0
      };

      fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      }).then(function(r) { return r.json(); })
        .then(function(data) {
          if (data.success) {
            var isAP = !document.getElementById('apBanner').classList.contains('hidden');
            if (isAP) {
              result.textContent = 'Saved! Rebooting... reconnect to your WiFi and check http://' + normalizedHostname + '.local in 30 seconds.';
              result.className = 'result success';
              result.classList.remove('hidden');
            } else {
              result.textContent = 'Settings saved! Rebooting in 3 seconds...';
              result.className = 'result success';
              result.classList.remove('hidden');
              setTimeout(function() {
                result.textContent += '\nReloading page in 10 seconds...';
              }, 3000);
              setTimeout(function() { window.location.reload(); }, 13000);
            }
          } else {
            throw new Error(data.error || 'Save failed');
          }
        })
        .catch(function(err) {
          btn.disabled = false;
          btn.textContent = 'Save & Reboot';
          result.textContent = err.message || 'Failed to save settings';
          result.className = 'result error';
          result.classList.remove('hidden');
        });
    });
  </script>
</body>
</html>
)rawliteral";
