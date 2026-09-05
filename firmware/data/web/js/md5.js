// SPDX-License-Identifier: GPL-3.0-or-later
// MD5 (RFC 1321) ueber ein Byte-Array, Ergebnis als Hex-Zeichenkette.
//
// Eigene Fassung, weil WebCrypto auf http:// nicht zur Verfuegung steht -- und die
// Update-Seite die Pruefsumme des Abbilds braucht: Das Geraet verwirft ein Abbild mit
// falscher Summe, bevor es aktiv wird (der Updater selbst prueft nur das erste Byte,
// und dieser Chip hat kein Rollback). Blockweise ueber eine DataView, damit auch
// 450 KB ohne Umweg ueber Zeichenketten gehen. Geprueft in tests/web/test_md5.mjs.
function md5Bytes(bytes) {
  const K = new Int32Array(64);
  for (let i = 0; i < 64; i++) K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296) | 0;
  const S = [
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
  ];
  const n = bytes.length;
  // Auffuellen: 0x80, Nullen bis 56 mod 64, dann die Bitlaenge (64 Bit, little-endian).
  const gesamt = (((n + 8) >> 6) + 1) << 6;
  const puffer = new Uint8Array(gesamt);
  puffer.set(bytes);
  puffer[n] = 0x80;
  const dv = new DataView(puffer.buffer);
  const bits = n * 8;
  dv.setUint32(gesamt - 8, bits >>> 0, true);
  dv.setUint32(gesamt - 4, Math.floor(bits / 4294967296), true);

  let a0 = 0x67452301;
  let b0 = 0xefcdab89 | 0;
  let c0 = 0x98badcfe | 0;
  let d0 = 0x10325476;
  const M = new Int32Array(16);
  for (let off = 0; off < gesamt; off += 64) {
    for (let i = 0; i < 16; i++) M[i] = dv.getInt32(off + i * 4, true);
    let A = a0, B = b0, C = c0, D = d0;
    for (let i = 0; i < 64; i++) {
      let F, g;
      if (i < 16) { F = (B & C) | (~B & D); g = i; }
      else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) & 15; }
      else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) & 15; }
      else { F = C ^ (B | ~D); g = (7 * i) & 15; }
      const tmp = D;
      D = C;
      C = B;
      const x = (A + F + K[i] + M[g]) | 0;
      B = (B + ((x << S[i]) | (x >>> (32 - S[i])))) | 0;
      A = tmp;
    }
    a0 = (a0 + A) | 0;
    b0 = (b0 + B) | 0;
    c0 = (c0 + C) | 0;
    d0 = (d0 + D) | 0;
  }
  const hex = (v) => {
    let s = "";
    for (let i = 0; i < 4; i++) s += ((v >>> (i * 8)) & 0xff).toString(16).padStart(2, "0");
    return s;
  };
  return hex(a0) + hex(b0) + hex(c0) + hex(d0);
}
