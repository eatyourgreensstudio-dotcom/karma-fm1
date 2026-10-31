// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FM-1 .fwsc packages in the browser: the package identity and the image the
// device reads during an update.

const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;

// the package identity ("FM-1_904"): one marker byte after each of the first 20 blocks
export function productOf(fwsc) {
  if (fwsc.length < BLOCKS * BLK) throw new Error("not an FM-1 package (too short)");
  let s = "";
  for (let i = 0; i < BLOCKS; i++) {
    const m = fwsc[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

// the image the device addresses during an update: the .fwsc without the 20 marker bytes
export function logicalImage(fwsc) {
  const out = new Uint8Array(fwsc.length - BLOCKS);
  for (let i = 0; i < BLOCKS; i++) out.set(fwsc.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(fwsc.subarray(BLOCKS * BLK), BLOCKS * KEEP);
  return out;
}

// The official FM-1 firmware, for the return to it: only these exact files are accepted (the SHA-256 pins every
// byte: the loader, the app, the layout; the name of the file proves nothing). V15: the digest Felucca 1.0 pins.
// V16 (2026-10-21; M-VAVE's site offers only V16 now): a new layout, the same official update loader. An FM-1 goes
// back only to the official version whose flash head it has (the version it last ran): with the other one it
// refuses in step 1, before anything is written (firmware/src/ota.c: the official loader writes the head).
export const STOCK = [
  { version: "V15", sha256: "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a", size: 699956, product: "FM-1_015" },
  { version: "V16", sha256: "d017fb65e70aca97ee29d944470c3515a74581df5c0576bf70de917c7b2b61d5", size: 718196, product: "FM-1_016" },
];
export const STOCK_V15_SHA256 = STOCK[0].sha256;
export const STOCK_V15_SIZE = STOCK[0].size;
export const STOCK_V15_PRODUCT = STOCK[0].product;

export async function sha256hex(bytes) {
  const d = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

export async function validateStockPackage(bytes) {
  const cand = STOCK.filter((x) => x.size === bytes.length);
  const sha = cand.length ? await sha256hex(bytes) : "";
  const k = cand.find((x) => x.sha256 === sha && productOf(bytes) === x.product);
  if (!k) throw new Error("select the unmodified official FM-1 V15 or V16 file (FM-1.fwsc)");
  return { product: k.product, version: k.version, image: logicalImage(bytes) };
}
