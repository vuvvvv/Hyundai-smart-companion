// محاكي شاشة SSD1306 128x64 لصور README: نفس الخطوط والصور النقطية وطريقة الرسم في Adafruit_GFX.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Collections.Generic;

public class GfxGlyph { public int Offset, W, H, XAdv, XOff, YOff; }
public class GfxFont {
  public byte[] Bitmap; public Dictionary<int, GfxGlyph> Glyphs = new Dictionary<int, GfxGlyph>();
}

public class OledSim {
  public const int W = 128, H = 64;
  public bool[,] Px = new bool[W, H];
  public int CursorX, CursorY;
  byte[] classic;                       // glcdfont 5x7
  public OledSim(byte[] classicFont) { classic = classicFont; }

  public void Set(int x, int y, bool on) { if (x >= 0 && x < W && y >= 0 && y < H) Px[x, y] = on; }
  public void Clear() { Px = new bool[W, H]; }

  // Adafruit drawBitmap: صفوف، البت الأعلى = الأيسر
  public void Bitmap(byte[] d, int w, int h, int x, int y, bool on) {
    int bpr = (w + 7) / 8;
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++)
        if ((d[j * bpr + i / 8] & (0x80 >> (i & 7))) != 0) Set(x + i, y + j, on);
  }

  public void FillRect(int x, int y, int w, int h, bool on) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) Set(x + i, y + j, on);
  }
  public void Rect(int x, int y, int w, int h) {
    for (int i = 0; i < w; i++) { Set(x + i, y, true); Set(x + i, y + h - 1, true); }
    for (int j = 0; j < h; j++) { Set(x, y + j, true); Set(x + w - 1, y + j, true); }
  }
  public void Circle1(int x, int y) { Set(x, y - 1, true); Set(x, y + 1, true); Set(x - 1, y, true); Set(x + 1, y, true); }

  // الخط الصغير المدمج (6x8 لكل حرف × الحجم)، cp437
  public void Small(string s, int x, int y, int size, bool on) {
    CursorX = x;
    foreach (char ch in s) {
      int c = ch;
      for (int i = 0; i < 5; i++) {
        int line = classic[c * 5 + i];
        for (int j = 0; j < 8; j++, line >>= 1)
          if ((line & 1) != 0) FillRect(CursorX + i * size, y + j * size, size, size, on);
      }
      CursorX += 6 * size;
    }
  }

  // getTextBounds لخط مخصص: يرجع x1 والعرض
  public void Bounds(GfxFont f, string s, out int x1, out int w) {
    int cx = 0, minx = int.MaxValue, maxx = int.MinValue;
    foreach (char ch in s) {
      GfxGlyph g; if (!f.Glyphs.TryGetValue(ch, out g)) continue;
      if (g.W > 0 && g.H > 0) { minx = Math.Min(minx, cx + g.XOff); maxx = Math.Max(maxx, cx + g.XOff + g.W - 1); }
      cx += g.XAdv;
    }
    if (maxx < minx) { x1 = 0; w = 0; } else { x1 = minx; w = maxx - minx + 1; }
  }

  public void Big(GfxFont f, string s, int x, int baseline) {
    int cx = x;
    foreach (char ch in s) {
      GfxGlyph g; if (!f.Glyphs.TryGetValue(ch, out g)) continue;
      int bit = 0, bo = g.Offset; byte bits = 0;
      for (int yy = 0; yy < g.H; yy++)
        for (int xx = 0; xx < g.W; xx++) {
          if ((bit++ & 7) == 0) bits = f.Bitmap[bo++];
          if ((bits & 0x80) != 0) Set(cx + g.XOff + xx, baseline + g.YOff + yy, true);
          bits <<= 1;
        }
      cx += g.XAdv;
    }
  }

  // صورة الشاشة: بكسلات صفراء (الصفوف 0..15) وزرقاء، داخل إطار
  public Bitmap Render(int scale) {
    int pad = 6 * scale, bw = W * scale + pad * 2, bh = H * scale + pad * 2;
    var img = new Bitmap(bw, bh);
    using (var g = Graphics.FromImage(img)) {
      g.SmoothingMode = SmoothingMode.AntiAlias;
      g.Clear(Color.Transparent);
      using (var frame = new SolidBrush(Color.FromArgb(18, 20, 26)))
        g.FillRectangle(frame, 0, 0, bw, bh);
      using (var edge = new Pen(Color.FromArgb(55, 60, 72), Math.Max(2, scale / 2)))
        g.DrawRectangle(edge, 1, 1, bw - 3, bh - 3);
      g.SmoothingMode = SmoothingMode.None;
      Color yellow = Color.FromArgb(255, 214, 64), blue = Color.FromArgb(80, 200, 255);
      Color off = Color.FromArgb(10, 13, 18);
      using (var bOff = new SolidBrush(off)) using (var bY = new SolidBrush(yellow)) using (var bB = new SolidBrush(blue))
        for (int y = 0; y < H; y++)
          for (int x = 0; x < W; x++) {
            var br = !Px[x, y] ? bOff : (y < 16 ? bY : bB);
            g.FillRectangle(br, pad + x * scale, pad + y * scale, scale - 1, scale - 1);
          }
    }
    return img;
  }
}
