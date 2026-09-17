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

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
