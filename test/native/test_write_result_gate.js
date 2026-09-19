// Node harness for the OpenTag3D write-result gate/verify logic (#316 review,
// fix round 1). Not part of `make test` — the C/C++ suites there don't touch
// the browser JS embedded in SharedJS.h / OpenTag3DWriterHTML.h. This parses
// the ACTUAL shipped source out of those headers (rather than re-implementing
// the logic) so a future edit that drifts from what's tested here fails loud.
//
// Run: node test/native/test_write_result_gate.js

const fs = require('fs');
const path = require('path');

const SRC_DIR = path.join(__dirname, '..', '..', 'src');
const sharedJs = fs.readFileSync(path.join(SRC_DIR, 'SharedJS.h'), 'utf8');
const writerHtml = fs.readFileSync(path.join(SRC_DIR, 'OpenTag3DWriterHTML.h'), 'utf8');
const nfcManagerH = fs.readFileSync(path.join(SRC_DIR, 'NFCManager.h'), 'utf8');
const webServerCpp = fs.readFileSync(path.join(SRC_DIR, 'WebServerManager.cpp'), 'utf8');

// ---- Extract the poll-loop gate from SharedJS.h ----------------------------
// Pulls the exact "var mine = ...;" line and the "if ((status.present ||
// mine) && config.verify(...)) {" condition out of the shipped source.
const gateMatch = sharedJs.match(
  /var mine = (status\.write_result[^\n]*);\s*\n\s*if \(([^\n]*config\.verify\([^\n]*)\) \{/
);
if (!gateMatch) {
  console.error('FAIL: could not locate the write_result gate in SharedJS.h — source shape changed');
  process.exit(1);
}
const mineExpr = gateMatch[1];
const gateCondition = gateMatch[2];
const gateFn = new Function(
  'status', 'payload', 'requestId', 'config',
  `var mine = ${mineExpr};\nreturn (${gateCondition});`
);

// ---- Extract the browser's poll deadline from SharedJS.h -------------------
const deadlineMatch = sharedJs.match(/var verifyDeadline = Date\.now\(\) \+ (\d+);/);
if (!deadlineMatch) {
  console.error('FAIL: could not locate the poll deadline in SharedJS.h — source shape changed');
  process.exit(1);
}
const pollDeadlineMs = parseInt(deadlineMatch[1], 10);

// ---- Extract the write-outcome ring constants from NFCManager.h ------------
const ringSizeMatch = nfcManagerH.match(/WRITE_OUTCOME_RING_SIZE\s*=\s*(\d+)/);
const ttlMatch = nfcManagerH.match(/WRITE_OUTCOME_TTL_MS\s*=\s*(\d+)/);
if (!ringSizeMatch || !ttlMatch) {
  console.error('FAIL: could not locate the write-outcome ring constants in NFCManager.h — source shape changed');
  process.exit(1);
}
const writeOutcomeRingSize = parseInt(ringSizeMatch[1], 10);
const writeOutcomeTtlMs = parseInt(ttlMatch[1], 10);

// ---- Extract the OpenTag3D verify() callback from OpenTag3DWriterHTML.h ---
// Brace-counts from the known anchor to its matching close, so this tracks
// the real function body regardless of internal formatting changes.
const anchor = 'verify: function (status, payload, requestId) {';
const start = writerHtml.indexOf(anchor);
if (start === -1) {
  console.error('FAIL: could not locate the OpenTag3D verify callback in OpenTag3DWriterHTML.h');
  process.exit(1);
}
const bodyStart = start + anchor.length;
let depth = 1;
let i = bodyStart;
for (; i < writerHtml.length && depth > 0; i++) {
  if (writerHtml[i] === '{') depth++;
  else if (writerHtml[i] === '}') depth--;
}
const verifyBody = writerHtml.slice(bodyStart, i - 1);
const verifyFn = new Function('status', 'payload', 'requestId', verifyBody);

// ---- Extract the OpenTag3D writable verdict from OpenTag3DWriterHTML.h -----
// Same brace-counting strategy as the verify callback above.
const verdictAnchor = 'function ot3dWritableVerdict(status) {';
const verdictStart = writerHtml.indexOf(verdictAnchor);
if (verdictStart === -1) {
  console.error('FAIL: could not locate ot3dWritableVerdict in OpenTag3DWriterHTML.h');
  process.exit(1);
}
const verdictBodyStart = verdictStart + verdictAnchor.length;
let verdictDepth = 1;
let vi = verdictBodyStart;
for (; vi < writerHtml.length && verdictDepth > 0; vi++) {
  if (writerHtml[vi] === '{') verdictDepth++;
  else if (writerHtml[vi] === '}') verdictDepth--;
}
const verdictFn = new Function('status', writerHtml.slice(verdictBodyStart, vi - 1));

// ---- Extract writePollVerdict() from SharedJS.h -----------------------------
// Same brace-counting strategy as the verify callback above.
const pollAnchor = 'function writePollVerdict(status, requestId) {';
const pollStart = sharedJs.indexOf(pollAnchor);
if (pollStart === -1) {
  console.error('FAIL: could not locate writePollVerdict in SharedJS.h');
  process.exit(1);
}
const pollBodyStart = pollStart + pollAnchor.length;
let pollDepth = 1;
let pi = pollBodyStart;
for (; pi < sharedJs.length && pollDepth > 0; pi++) {
  if (sharedJs[pi] === '{') pollDepth++;
  else if (sharedJs[pi] === '}') pollDepth--;
}
const pollVerdictFn = new Function('status', 'requestId', sharedJs.slice(pollBodyStart, pi - 1));

// ---- Assertions -------------------------------------------------------------
let passed = 0, failed = 0;
function check(name, actual, expected) {
  const ok = Object.is(actual, expected);
  console.log((ok ? '  PASS  ' : '  FAIL  ') + name + (ok ? '' : ` (expected ${expected}, got ${actual})`));
  if (ok) passed++; else failed++;
}
function checkThrows(name, fn) {
  let threw = false;
  try { fn(); } catch (e) { threw = true; }
  console.log((threw ? '  PASS  ' : '  FAIL  ') + name);
  if (threw) passed++; else failed++;
}

// Finding 3: request_id matches, uid does not — must be rejected.
check(
  'verify rejects matching request_id with mismatched uid',
  verifyFn(
    { write_result: { request_id: 42, uid: 'AABBCCDD', ok: true } },
    { uid: 'DEADBEEF' },
    42
  ),
  false
);

// Finding 1: outcome arrives while status.present is false — must be accepted
// by the gate (the presence check alone would otherwise wait forever).
check(
  'gate accepts a matching outcome while present is false',
  gateFn(
    { present: false, write_result: { request_id: 42, uid: 'AABBCCDD', ok: true } },
    { uid: 'AABBCCDD' },
    42,
    { verify: verifyFn }
  ),
  true
);

// Sanity: unrelated outcome (different request_id) while present is false —
// gate must NOT call verify and must stay false (this is the pre-existing
// wait behaviour for the other three writers, and for OpenTag3D before its
// own result has arrived).
{
  let verifyCalled = false;
  const spy = (...args) => { verifyCalled = true; return verifyFn(...args); };
  const result = gateFn(
    { present: false, write_result: { request_id: 99, uid: 'AABBCCDD', ok: true } },
    { uid: 'AABBCCDD' },
    42,
    { verify: spy }
  );
  check('gate short-circuits (does not call verify) for someone else\'s result while absent', verifyCalled, false);
  check('gate stays false for someone else\'s result while absent', result, false);
}

// Success path: matching id + uid + ok — verify returns true.
check(
  'verify accepts matching request_id and uid on success',
  verifyFn(
    { write_result: { request_id: 7, uid: '0123456789ABCDEF', ok: true } },
    { uid: '0123456789ABCDEF' },
    7
  ),
  true
);

// Failure path: matching id + uid but ok:false — verify throws with the
// server's short error string, which sharedWriteFlow's catch surfaces to resultBox.
checkThrows(
  'verify throws on matching id/uid with ok:false',
  () => verifyFn(
    { write_result: { request_id: 7, uid: 'AA11', ok: false, error: 'tag removed' } },
    { uid: 'AA11' },
    7
  )
);

// Not-yet-arrived: no write_result at all — must not throw, must return false.
check(
  'verify returns false (not throws) when write_result has not arrived yet',
  verifyFn({}, { uid: 'AA11' }, 7),
  false
);

// #316 ring sizing/TTL: the ring must not be smaller than the write queue
// (xQueueCreate(8, ...)), and outcomes must outlive the browser's own poll
// deadline or a slow poller can lose a result before ever seeing it.
check('ring holds at least the write queue depth', writeOutcomeRingSize >= 8, true);
check('outcome TTL outlasts the browser polling deadline', writeOutcomeTtlMs > pollDeadlineMs, true);

// #316 read-only tag: only an explicit writable:false turns writing off;
// any other shape (true, missing, no opentag3d, null status) keeps it on.
check('writable false turns writing off', verdictFn({ opentag3d: { writable: false } }), false);
check('writable true keeps writing on', verdictFn({ opentag3d: { writable: true } }), true);
check('a missing writable field keeps writing on (older firmware)', verdictFn({ opentag3d: {} }), true);
check('no opentag3d object keeps writing on', verdictFn({ present: true }), true);
check('a null status keeps writing on', verdictFn(null), true);

// Source-shape checks: the hint element and the raised transmission
// distance form limit must exist in the shipped page.
check('read-only hint element exists', writerHtml.indexOf('id="readOnlyHint"') !== -1, true);
check('transmission distance form limit is 255', /id="transmission_distance"[^>]*max="255"/.test(writerHtml), true);

// Source-shape checks: ending a read must respect the page's Write veto, and
// the OpenTag3D page must supply that veto.
check('shared setReadWaiting respects the page writeAllowed veto',
  sharedJs.indexOf('config.writeAllowed ? !config.writeAllowed() : false') !== -1, true);
check('OpenTag3D page passes a writeAllowed callback',
  writerHtml.indexOf('writeAllowed: function () { return ot3dWriteAllowed; }') !== -1, true);

// #316 round 2: every write route reports its request_id and the shared flow
// must trust the firmware's verdict over its own field comparison.
check('poll verdict: our id, ok false is fail',
  pollVerdictFn({ write_result: { request_id: 42, ok: false } }, 42), 'fail');
check('poll verdict: our id, ok true is check',
  pollVerdictFn({ write_result: { request_id: 42, ok: true } }, 42), 'check');
check('poll verdict: id issued, no write_result yet is wait',
  pollVerdictFn({}, 42), 'wait');
check('poll verdict: id issued, another id result is wait',
  pollVerdictFn({ write_result: { request_id: 99, ok: true } }, 42), 'wait');
check('poll verdict: no id, no write_result is check (older firmware)',
  pollVerdictFn({}, 0), 'check');
check('poll verdict: no id, unrelated failed write_result is check',
  pollVerdictFn({ write_result: { request_id: 99, ok: false } }, 0), 'check');
check('poll verdict: null status with an id is wait',
  pollVerdictFn(null, 42), 'wait');
check('shared flow honours the wait verdict',
  sharedJs.indexOf("if (verdict === 'wait') continue;") !== -1, true);
const sendAcceptedCount = (webServerCpp.match(/sendWriteAccepted\(/g) || []).length;
check('four write routes + helper definition reference sendWriteAccepted',
  sendAcceptedCount >= 5, true);

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
