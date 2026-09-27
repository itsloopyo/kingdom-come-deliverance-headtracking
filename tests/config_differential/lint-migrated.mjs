// Runs core's canonical config lint over CameraUnlock.ini, the committed file, and
// over every distinct CameraUnlock.ini the differential test migrated (the folder
// it names as the argument).
//
// A migrated file may break one rule the committed file may not: it holds the
// player's own value on a global row wherever that value is not the built-in one,
// which is what the migration is for. The lint's rule that such a row holds
// `default` is for the committed file, so that one finding is allowed in a
// migrated file and nothing else is.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const options = { dialect: "native", perGame: [] };
const PLAYER_VALUE =
  /^lines? [^:]+: \S.* (holds a value|hold values), and data\/config-format\.json per_game (does not list it|lists none of them) for this repo;/;
const failures = [];

for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(repo, "CameraUnlock.ini")), options)) {
  failures.push(`CameraUnlock.ini: ${problem}`);
}

const files = fs.readdirSync(migratedDir).filter((f) => f.endsWith(".ini"));
if (files.length === 0) throw new Error(`${migratedDir} holds no migrated files`);
let withValues = 0;
for (const file of files) {
  let carried = false;
  for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(migratedDir, file)), options)) {
    if (PLAYER_VALUE.test(problem)) {
      carried = true;
      continue;
    }
    failures.push(`${file}: ${problem}`);
  }
  if (carried) withValues++;
}

if (failures.length > 0) {
  for (const f of failures) console.log(`FAIL ${f}`);
  process.exit(1);
}
console.log(
  `canonical config lint: CameraUnlock.ini and ${files.length} migrated files pass` +
    ` (${withValues} hold a player's value on a global row)`,
);
