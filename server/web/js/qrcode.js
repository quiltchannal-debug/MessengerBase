/* OrangeM — компактный QR-энкодер без зависимостей.
 *
 * Byte mode (UTF-8), уровень коррекции ошибок M, автоматический выбор
 * минимальной версии. Маска выбирается перебором всех 8 шаблонов по
 * стандартным штрафным правилам.
 *
 * Публичный API (глобальный, без модулей):
 *   OM.qr.matrix(text) -> boolean[][]   true = тёмный модуль
 *   OM.qr.svg(text, size) -> string     готовая строка <svg>...</svg>
 *
 * ES5/ES2015-совместимо, без импортов и внешних библиотек.
 */
window.OM = window.OM || {};

/* Локальная привязка: в браузере OM — это и так window.OM, но при загрузке
 * файла вне браузера (например, в Node-тестах) глобальной переменной OM нет. */
var OM = window.OM;

OM.qr = (function () {
  'use strict';

  /* ------------------------------------------------------------------ *
   *  Таблицы стандарта ISO/IEC 18004
   * ------------------------------------------------------------------ */

  /* Уровень M: [ecCodewordsPerBlock, blocksGroup1, dataGroup1,
   *             blocksGroup2, dataGroup2]  — индекс = версия 1..40 */
  var ECC_M = [
    null,
    [10, 1, 16, 0, 0],   /*  1 */
    [16, 1, 28, 0, 0],   /*  2 */
    [26, 1, 44, 0, 0],   /*  3 */
    [18, 2, 32, 0, 0],   /*  4 */
    [24, 2, 43, 0, 0],   /*  5 */
    [16, 4, 27, 0, 0],   /*  6 */
    [18, 4, 31, 0, 0],   /*  7 */
    [22, 2, 38, 2, 39],  /*  8 */
    [22, 3, 36, 2, 37],  /*  9 */
    [26, 4, 43, 1, 44],  /* 10 */
    [30, 1, 50, 4, 51],  /* 11 */
    [22, 6, 36, 2, 37],  /* 12 */
    [22, 8, 37, 1, 38],  /* 13 */
    [24, 4, 40, 5, 41],  /* 14 */
    [24, 5, 41, 5, 42],  /* 15 */
    [28, 7, 45, 3, 46],  /* 16 */
    [28, 10, 46, 1, 47], /* 17 */
    [26, 9, 43, 4, 44],  /* 18 */
    [26, 3, 44, 11, 45], /* 19 */
    [26, 3, 41, 13, 42], /* 20 */
    [26, 17, 42, 0, 0],  /* 21 */
    [28, 17, 46, 0, 0],  /* 22 */
    [28, 4, 47, 14, 48], /* 23 */
    [28, 6, 45, 14, 46], /* 24 */
    [28, 8, 47, 13, 48], /* 25 */
    [28, 19, 46, 4, 47], /* 26 */
    [28, 22, 45, 3, 46], /* 27 */
    [28, 3, 45, 23, 46], /* 28 */
    [28, 21, 45, 7, 46], /* 29 */
    [28, 19, 47, 10, 48],/* 30 */
    [28, 2, 46, 29, 47], /* 31 */
    [28, 10, 46, 23, 47],/* 32 */
    [28, 14, 46, 21, 47],/* 33 */
    [28, 14, 46, 23, 47],/* 34 */
    [28, 12, 47, 26, 48],/* 35 */
    [28, 6, 47, 34, 48], /* 36 */
    [28, 29, 46, 14, 47],/* 37 */
    [28, 13, 46, 32, 47],/* 38 */
    [28, 40, 47, 7, 48], /* 39 */
    [28, 18, 47, 31, 48] /* 40 */
  ];

  /* Центры alignment-паттернов — индекс = версия 1..40 */
  var ALIGN = [
    null,
    [],                          /*  1 */
    [6, 18],                     /*  2 */
    [6, 22],                     /*  3 */
    [6, 26],                     /*  4 */
    [6, 30],                     /*  5 */
    [6, 34],                     /*  6 */
    [6, 22, 38],                 /*  7 */
    [6, 24, 42],                 /*  8 */
    [6, 26, 46],                 /*  9 */
    [6, 28, 50],                 /* 10 */
    [6, 30, 54],                 /* 11 */
    [6, 32, 58],                 /* 12 */
    [6, 34, 62],                 /* 13 */
    [6, 26, 46, 66],             /* 14 */
    [6, 26, 48, 70],             /* 15 */
    [6, 26, 50, 74],             /* 16 */
    [6, 30, 54, 78],             /* 17 */
    [6, 30, 56, 82],             /* 18 */
    [6, 30, 58, 86],             /* 19 */
    [6, 34, 62, 90],             /* 20 */
    [6, 28, 50, 72, 94],         /* 21 */
    [6, 26, 50, 74, 98],         /* 22 */
    [6, 30, 54, 78, 102],        /* 23 */
    [6, 28, 54, 80, 106],        /* 24 */
    [6, 32, 58, 84, 110],        /* 25 */
    [6, 30, 58, 86, 114],        /* 26 */
    [6, 34, 62, 90, 118],        /* 27 */
    [6, 26, 50, 74, 98, 122],    /* 28 */
    [6, 30, 54, 78, 102, 126],   /* 29 */
    [6, 26, 52, 78, 104, 130],   /* 30 */
    [6, 30, 56, 82, 108, 134],   /* 31 */
    [6, 34, 60, 86, 112, 138],   /* 32 */
    [6, 30, 58, 86, 114, 142],   /* 33 */
    [6, 34, 62, 90, 118, 146],   /* 34 */
    [6, 30, 54, 78, 102, 126, 150], /* 35 */
    [6, 24, 50, 76, 102, 128, 154], /* 36 */
    [6, 28, 54, 80, 106, 132, 158], /* 37 */
    [6, 32, 58, 84, 110, 136, 162], /* 38 */
    [6, 26, 54, 82, 110, 138, 166], /* 39 */
    [6, 30, 58, 86, 114, 142, 170]  /* 40 */
  ];

  var MIN_VERSION = 1;
  var MAX_VERSION = 40;

  /* ------------------------------------------------------------------ *
   *  UTF-8
   * ------------------------------------------------------------------ */

  function utf8Bytes(str) {
    var s = String(str);
    var out = [];
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c < 0x80) {
        out.push(c);
      } else if (c < 0x800) {
        out.push(0xC0 | (c >> 6), 0x80 | (c & 0x3F));
      } else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.length &&
                 s.charCodeAt(i + 1) >= 0xDC00 && s.charCodeAt(i + 1) <= 0xDFFF) {
        var hi = c;
        var lo = s.charCodeAt(++i);
        var cp = 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
        out.push(0xF0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3F),
                 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F));
      } else {
        out.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F));
      }
    }
    return out;
  }

  /* ------------------------------------------------------------------ *
   *  Арифметика GF(256), полином 0x11D
   * ------------------------------------------------------------------ */

  var GF_EXP = new Array(512);
  var GF_LOG = new Array(256);

  (function () {
    var x = 1;
    for (var i = 0; i < 255; i++) {
      GF_EXP[i] = x;
      GF_LOG[x] = i;
      x <<= 1;
      if (x & 0x100) x ^= 0x11D;
    }
    for (var j = 255; j < 512; j++) GF_EXP[j] = GF_EXP[j - 255];
  })();

  function gfMul(a, b) {
    if (a === 0 || b === 0) return 0;
    return GF_EXP[GF_LOG[a] + GF_LOG[b]];
  }

  /* Порождающий полином степени n (старший коэффициент — индекс 0). */
  function rsGenerator(n) {
    var poly = [1];
    for (var i = 0; i < n; i++) {
      var next = [];
      for (var k = 0; k <= poly.length; k++) next.push(0);
      for (var j = 0; j < poly.length; j++) {
        next[j] ^= poly[j];
        next[j + 1] ^= gfMul(poly[j], GF_EXP[i]);
      }
      poly = next;
    }
    return poly;
  }

  /* Остаток от деления data на порождающий полином (n байт ECC). */
  function rsRemainder(data, n) {
    var gen = rsGenerator(n);
    var res = data.slice();
    for (var z = 0; z < n; z++) res.push(0);
    for (var i = 0; i < data.length; i++) {
      var factor = res[i];
      if (factor === 0) continue;
      for (var j = 0; j < gen.length; j++) {
        res[i + j] ^= gfMul(gen[j], factor);
      }
    }
    return res.slice(data.length);
  }

  /* ------------------------------------------------------------------ *
   *  Служебные данные кода
   * ------------------------------------------------------------------ */

  function dataCodewordCount(version) {
    var e = ECC_M[version];
    return e[1] * e[2] + e[3] * e[4];
  }

  /* Число бит в счётчике символов для byte mode. */
  function charCountBits(version) {
    return version < 10 ? 8 : 16;
  }

  /* Минимальная версия, вмещающая len байт; 0 — не вмещается. */
  function chooseVersion(len) {
    for (var v = MIN_VERSION; v <= MAX_VERSION; v++) {
      var capacityBits = dataCodewordCount(v) * 8;
      var need = 4 + charCountBits(v) + len * 8;
      if (need <= capacityBits) return v;
    }
    return 0;
  }

  /* Байты -> поток данных (mode + счётчик + данные + terminator + padding). */
  function buildDataCodewords(bytes, version) {
    var capacityBits = dataCodewordCount(version) * 8;
    var bits = [];

    function push(value, count) {
      for (var i = count - 1; i >= 0; i--) bits.push((value >> i) & 1);
    }

    push(4, 4);                                        /* byte mode = 0100 */
    push(bytes.length, charCountBits(version));
    for (var i = 0; i < bytes.length; i++) push(bytes[i], 8);

    /* terminator: до 4 нулей */
    var term = Math.min(4, capacityBits - bits.length);
    for (var t = 0; t < term; t++) bits.push(0);
    /* выравнивание до границы байта */
    while (bits.length % 8 !== 0) bits.push(0);

    var codewords = [];
    for (var b = 0; b < bits.length; b += 8) {
      var byteVal = 0;
      for (var k = 0; k < 8; k++) byteVal = (byteVal << 1) | bits[b + k];
      codewords.push(byteVal);
    }
    /* padding-байты */
    var pad = [0xEC, 0x11];
    var p = 0;
    while (codewords.length * 8 < capacityBits) {
      codewords.push(pad[p]);
      p ^= 1;
    }
    return codewords;
  }

  /* Разбиение на блоки, ECC, чередование -> итоговая последовательность
   * кодовых слов в порядке размещения в матрице. */
  function addEccAndInterleave(data, version) {
    var e = ECC_M[version];
    var ecLen = e[0];
    var blocks = [];
    var offset = 0;
    var i, j;

    for (i = 0; i < e[1]; i++) {
      blocks.push(data.slice(offset, offset + e[2]));
      offset += e[2];
    }
    for (i = 0; i < e[3]; i++) {
      blocks.push(data.slice(offset, offset + e[4]));
      offset += e[4];
    }

    var eccs = [];
    for (i = 0; i < blocks.length; i++) eccs.push(rsRemainder(blocks[i], ecLen));

    var maxData = 0;
    for (i = 0; i < blocks.length; i++) {
      if (blocks[i].length > maxData) maxData = blocks[i].length;
    }

    var result = [];
    for (i = 0; i < maxData; i++) {
      for (j = 0; j < blocks.length; j++) {
        if (i < blocks[j].length) result.push(blocks[j][i]);
      }
    }
    for (i = 0; i < ecLen; i++) {
      for (j = 0; j < blocks.length; j++) result.push(eccs[j][i]);
    }
    return result;
  }

  /* ------------------------------------------------------------------ *
   *  Форматная и версионная информация (BCH)
   * ------------------------------------------------------------------ */

  /* 5 бит (2 — уровень M = 00, 3 — маска) -> 15 бит с BCH и маской 0x5412. */
  function formatBits(mask) {
    var data = mask;                 /* ECC M = 0b00 в старших двух битах */
    var rem = data << 10;
    for (var i = 14; i >= 10; i--) {
      if ((rem >> i) & 1) rem ^= 0x537 << (i - 10);
    }
    return ((data << 10) | rem) ^ 0x5412;
  }

  /* 6 бит версии -> 18 бит с BCH (только для версий >= 7). */
  function versionBits(version) {
    var rem = version << 12;
    for (var i = 17; i >= 12; i--) {
      if ((rem >> i) & 1) rem ^= 0x1F25 << (i - 12);
    }
    return (version << 12) | rem;
  }

  /* ------------------------------------------------------------------ *
   *  Построение матрицы
   * ------------------------------------------------------------------ */

  function makeGrid(size) {
    var g = [];
    for (var r = 0; r < size; r++) {
      var row = [];
      for (var c = 0; c < size; c++) row.push(0);
      g.push(row);
    }
    return g;
  }

  function maskFn(mask, r, c) {
    switch (mask) {
      case 0: return (r + c) % 2 === 0;
      case 1: return r % 2 === 0;
      case 2: return c % 3 === 0;
      case 3: return (r + c) % 3 === 0;
      case 4: return (Math.floor(r / 2) + Math.floor(c / 3)) % 2 === 0;
      case 5: return ((r * c) % 2) + ((r * c) % 3) === 0;
      case 6: return (((r * c) % 2) + ((r * c) % 3)) % 2 === 0;
      case 7: return (((r + c) % 2) + ((r * c) % 3)) % 2 === 0;
    }
    return false;
  }

  function buildModules(version, codewords, mask) {
    var size = version * 4 + 17;
    var mod = makeGrid(size);
    var func = makeGrid(size);
    var r, c, i, j;

    function set(y, x, dark, isFunc) {
      if (y < 0 || y >= size || x < 0 || x >= size) return;
      mod[y][x] = dark ? 1 : 0;
      if (isFunc) func[y][x] = 1;
    }

    /* --- finder-паттерны + разделители --- */
    function placeFinder(r0, c0) {
      for (var dy = -1; dy <= 7; dy++) {
        for (var dx = -1; dx <= 7; dx++) {
          var y = r0 + dy, x = c0 + dx;
          if (y < 0 || y >= size || x < 0 || x >= size) continue;
          var dark = (dy >= 0 && dy <= 6 && (dx === 0 || dx === 6)) ||
                     (dx >= 0 && dx <= 6 && (dy === 0 || dy === 6)) ||
                     (dy >= 2 && dy <= 4 && dx >= 2 && dx <= 4);
          set(y, x, dark, true);
        }
      }
    }
    placeFinder(0, 0);
    placeFinder(0, size - 7);
    placeFinder(size - 7, 0);

    /* --- timing-паттерны --- */
    for (i = 8; i < size - 8; i++) {
      var darkTiming = i % 2 === 0;
      if (!func[6][i]) set(6, i, darkTiming, true);
      if (!func[i][6]) set(i, 6, darkTiming, true);
    }

    /* --- alignment-паттерны --- */
    var centers = ALIGN[version];
    for (i = 0; i < centers.length; i++) {
      for (j = 0; j < centers.length; j++) {
        var cy = centers[i], cx = centers[j];
        /* пропускаем те, что накладываются на finder-паттерны */
        if ((cy <= 8 && cx <= 8) || (cy <= 8 && cx >= size - 9) ||
            (cy >= size - 9 && cx <= 8)) continue;
        for (var dy2 = -2; dy2 <= 2; dy2++) {
          for (var dx2 = -2; dx2 <= 2; dx2++) {
            var ring = Math.max(Math.abs(dy2), Math.abs(dx2));
            set(cy + dy2, cx + dx2, ring !== 1, true);
          }
        }
      }
    }

    /* --- тёмный модуль --- */
    set(size - 8, 8, true, true);

    /* --- резерв под форматную информацию --- */
    for (i = 0; i <= 8; i++) {
      set(8, i, mod[8][i] === 1, true);
      set(i, 8, mod[i][8] === 1, true);
    }
    for (i = 0; i < 8; i++) {
      set(8, size - 1 - i, mod[8][size - 1 - i] === 1, true);
      set(size - 1 - i, 8, mod[size - 1 - i][8] === 1, true);
    }

    /* --- резерв под версионную информацию (>= 7) --- */
    if (version >= 7) {
      for (i = 0; i < 6; i++) {
        for (j = 0; j < 3; j++) {
          set(i, size - 11 + j, false, true);
          set(size - 11 + j, i, false, true);
        }
      }
    }

    /* --- размещение данных (зигзаг) --- */
    var totalBits = codewords.length * 8;
    var bitIndex = 0;

    function nextBit() {
      if (bitIndex >= totalBits) return 0;
      var b = (codewords[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1;
      bitIndex++;
      return b;
    }

    var upward = true;
    for (var col = size - 1; col > 0; col -= 2) {
      if (col === 6) col = 5;              /* пропуск колонки timing */
      for (var k = 0; k < size; k++) {
        var row = upward ? size - 1 - k : k;
        for (var t = 0; t < 2; t++) {
          var x2 = col - t;
          if (func[row][x2]) continue;
          mod[row][x2] = nextBit();
        }
      }
      upward = !upward;
    }

    /* --- маскирование данных --- */
    for (r = 0; r < size; r++) {
      for (c = 0; c < size; c++) {
        if (!func[r][c] && maskFn(mask, r, c)) mod[r][c] ^= 1;
      }
    }

    /* --- форматная информация --- */
    var fmt = formatBits(mask);
    function fmtBit(idx) { return (fmt >> idx) & 1; }

    /* первая копия: колонка 8 (строки 0..5,7,8) + строка 8 (столбцы 8..0) */
    for (i = 0; i <= 5; i++) set(i, 8, fmtBit(i) === 1, true);
    set(7, 8, fmtBit(6) === 1, true);
    set(8, 8, fmtBit(7) === 1, true);
    set(8, 7, fmtBit(8) === 1, true);
    for (i = 9; i < 15; i++) set(8, 14 - i, fmtBit(i) === 1, true);

    /* вторая копия: строка 8 (справа) + колонка 8 (снизу) */
    for (i = 0; i < 8; i++) set(8, size - 1 - i, fmtBit(i) === 1, true);
    for (i = 8; i < 15; i++) set(size - 15 + i, 8, fmtBit(i) === 1, true);
    set(size - 8, 8, true, true);

    /* --- версионная информация (>= 7) --- */
    if (version >= 7) {
      var vb = versionBits(version);
      for (i = 0; i < 18; i++) {
        var bit = ((vb >> i) & 1) === 1;
        var a = size - 11 + (i % 3);
        var b = Math.floor(i / 3);
        set(b, a, bit, true);   /* верхний правый блок */
        set(a, b, bit, true);   /* нижний левый блок */
      }
    }

    return mod;
  }

  /* ------------------------------------------------------------------ *
   *  Штрафные правила выбора маски (ISO/IEC 18004 §8.8.2)
   * ------------------------------------------------------------------ */

  var N1 = 3, N2 = 3, N3 = 40, N4 = 10;

  function penalty(mod, size) {
    var result = 0;
    var x, y;

    /* Правило 1: серии одинаковых модулей >= 5 подряд (строки и столбцы) */
    for (y = 0; y < size; y++) {
      var runColor = mod[y][0], runLen = 1;
      for (x = 1; x < size; x++) {
        if (mod[y][x] === runColor) {
          runLen++;
          if (runLen === 5) result += N1;
          else if (runLen > 5) result += 1;
        } else {
          runColor = mod[y][x];
          runLen = 1;
        }
      }
    }
    for (x = 0; x < size; x++) {
      var runColor2 = mod[0][x], runLen2 = 1;
      for (y = 1; y < size; y++) {
        if (mod[y][x] === runColor2) {
          runLen2++;
          if (runLen2 === 5) result += N1;
          else if (runLen2 > 5) result += 1;
        } else {
          runColor2 = mod[y][x];
          runLen2 = 1;
        }
      }
    }

    /* Правило 2: блоки 2x2 одного цвета */
    for (y = 0; y < size - 1; y++) {
      for (x = 0; x < size - 1; x++) {
        var v = mod[y][x];
        if (v === mod[y][x + 1] && v === mod[y + 1][x] && v === mod[y + 1][x + 1]) {
          result += N2;
        }
      }
    }

    /* Правило 3: паттерн 1:1:3:1:1 как у finder-паттерна с 4 светлыми
     * модулями с любой из сторон. Модель qrcodegen: модули за границей
     * символа считаются светлыми. */
    function whiteRunH(row, from, to) {
      if (from < 0) from = 0;
      if (to > size) to = size;
      for (var i = from; i < to; i++) if (mod[row][i]) return false;
      return true;
    }
    function whiteRunV(col, from, to) {
      if (from < 0) from = 0;
      if (to > size) to = size;
      for (var i = from; i < to; i++) if (mod[i][col]) return false;
      return true;
    }
    for (y = 0; y < size; y++) {
      for (x = 0; x < size; x++) {
        if (x + 6 < size &&
            mod[y][x] && !mod[y][x + 1] && mod[y][x + 2] && mod[y][x + 3] &&
            mod[y][x + 4] && !mod[y][x + 5] && mod[y][x + 6] &&
            (whiteRunH(y, x - 4, x) || whiteRunH(y, x + 7, x + 11))) {
          result += N3;
        }
        if (y + 6 < size &&
            mod[y][x] && !mod[y + 1][x] && mod[y + 2][x] && mod[y + 3][x] &&
            mod[y + 4][x] && !mod[y + 5][x] && mod[y + 6][x] &&
            (whiteRunV(x, y - 4, y) || whiteRunV(x, y + 7, y + 11))) {
          result += N3;
        }
      }
    }

    /* Правило 4: баланс тёмных и светлых модулей */
    var dark = 0;
    for (y = 0; y < size; y++) {
      for (x = 0; x < size; x++) dark += mod[y][x];
    }
    var total = size * size;
    var k = Math.ceil(Math.abs(dark * 20 - total * 10) / total) - 1;
    if (k > 0) result += k * N4;

    return result;
  }

  /* ------------------------------------------------------------------ *
   *  Публичный API
   * ------------------------------------------------------------------ */

  function encode(text) {
    var bytes = utf8Bytes(text);
    var version = chooseVersion(bytes.length);
    if (version === 0) throw new Error('too long');

    var data = buildDataCodewords(bytes, version);
    var codewords = addEccAndInterleave(data, version);

    var best = null;
    var bestScore = Infinity;
    for (var mask = 0; mask < 8; mask++) {
      var mod = buildModules(version, codewords, mask);
      var score = penalty(mod, mod.length);
      if (score < bestScore) {
        bestScore = score;
        best = mod;
      }
    }
    return best;
  }

  function matrix(text) {
    var mod = encode(text);
    var size = mod.length;
    var out = [];
    for (var r = 0; r < size; r++) {
      var row = [];
      for (var c = 0; c < size; c++) row.push(mod[r][c] === 1);
      out.push(row);
    }
    return out;
  }

  var QUIET = 4;

  function svg(text, size) {
    var m = matrix(text);
    var n = m.length;
    var span = n + QUIET * 2;
    if (!size) size = span * 4;

    var d = '';
    for (var y = 0; y < n; y++) {
      var x = 0;
      while (x < n) {
        if (!m[y][x]) { x++; continue; }
        var run = 0;
        while (x + run < n && m[y][x + run]) run++;
        d += 'M' + (x + QUIET) + ' ' + (y + QUIET) + 'h' + run + 'v1h-' + run + 'z';
        x += run;
      }
    }

    return '<svg xmlns="http://www.w3.org/2000/svg" width="' + size + '" height="' + size +
           '" viewBox="0 0 ' + span + ' ' + span + '" shape-rendering="crispEdges">' +
           '<rect width="' + span + '" height="' + span + '" fill="#ffffff"/>' +
           '<path d="' + d + '" fill="#000000"/>' +
           '</svg>';
  }

  return { matrix: matrix, svg: svg };
})();
