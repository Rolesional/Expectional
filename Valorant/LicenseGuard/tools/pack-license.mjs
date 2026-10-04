#!/usr/bin/env node
// =============================================================================
// pack-license.mjs (v2)
//
// license.dat formatı:
//   bytes 0..3   : magic "LIC2"
//   bytes 4..15  : 12-byte AES-GCM IV (random)
//   bytes 16..   : ciphertext || 16-byte GCM tag
//
// Plaintext = license_key (UTF-8). app_id ve ws_url ARTIK BURADA YOK —
// onlar binary'ye LXS ile gömülü.
//
// Anahtar: HKDF-SHA256(BUILD_SECRET, salt="lic-dat-v2", info="license-key", 32)
// BUILD_SECRET = client'taki LXS("...") + server .env WIRE_BUILD_SECRET ile
// AYNI olmalı. Farklıysa client license.dat'ı çözemez.
//
// Kullanım:
//   node pack-license.mjs --key "DEMO1-DEMO2-DEMO3-DEMO4" --out license.dat
//
// (Eskiden gerekli olan --app ve --url artık YOK.)
// =============================================================================

import fs from "node:fs";
import crypto from "node:crypto";
import { argv } from "node:process";

const BUILD_SECRET = "expectional-license-guard-v1"; // <-- Client LXS ile EŞ.

function arg(name, def) {
  const i = argv.indexOf(`--${name}`);
  return i >= 0 ? argv[i + 1] : def;
}

const licenseKey = arg("key", "DEMO1-DEMO2-DEMO3-DEMO4");
const outPath = arg("out", "license.dat");

// HKDF
const key = Buffer.from(
  crypto.hkdfSync(
    "sha256",
    Buffer.from(BUILD_SECRET, "utf8"),
    Buffer.from("lic-dat-v2", "utf8"),
    Buffer.from("license-key", "utf8"),
    32
  )
);

const iv = crypto.randomBytes(12);
const cipher = crypto.createCipheriv("aes-256-gcm", key, iv);
const enc = Buffer.concat([
  cipher.update(Buffer.from(licenseKey, "utf8")),
  cipher.final(),
]);
const tag = cipher.getAuthTag();
const ct = Buffer.concat([enc, tag]);

const final = Buffer.concat([Buffer.from("LIC2", "ascii"), iv, ct]);
fs.writeFileSync(outPath, final);
console.log(`OK: wrote ${outPath} (${final.length} bytes, format LIC2)`);
console.log(`  license_key = ${licenseKey.slice(0, 8)}...`);
console.log(`  ! BUILD_SECRET aynı tutulmalı: client LXS + server .env`);
