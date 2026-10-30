using System; using System.IO; using System.Collections.Generic;
public static class Mp3Scan {
  static int[] BR = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
  static int[] SR = {22050,24000,16000};
  // each frame: offset, length, part2_3_length (audio bits), global_gain
  public static List<int[]> Frames(byte[] d) {
    var res = new List<int[]>(); int i = 0;
    if (d.Length > 10 && d[0]=='I' && d[1]=='D' && d[2]=='3') i = 10 + ((d[6]&0x7f)<<21 | (d[7]&0x7f)<<14 | (d[8]&0x7f)<<7 | (d[9]&0x7f));
    while (i + 13 < d.Length) {
      if (d[i]!=0xFF || (d[i+1]&0xF6)!=0xF2) { i++; continue; }  // MPEG2 Layer III
      int bri=(d[i+2]>>4)&15, sri=(d[i+2]>>2)&3, pad=(d[i+2]>>1)&1;
      if (bri==0||bri==15||sri==3) { i++; continue; }
      int len = 72*BR[bri]*1000/SR[sri] + pad;
      if (i + len > d.Length) break;
      int s = i + 4;   // side info (mono, no CRC): main_data_begin 8, private 1, part2_3_length 12
      int p23 = ((d[s]<<16 | d[s+1]<<8 | d[s+2]) >> 3) & 0xFFF;
      int gg  = ((d[s+3]<<8 | d[s+4]) >> 2) & 0xFF;
      int mdb = d[s];
      res.Add(new int[]{i, len, p23, gg, mdb}); i += len;
    }
    return res;
  }

  // Cut whole frames: keep speech (global_gain >= T) plus tailFrames after it.
  // Leading frames are kept back far enough to hold the bit reservoir of the first speech frame.
  // Frame 0 (LAME Info tag) is dropped because its frame count would be wrong.
  public static byte[] Trim(byte[] d, int T, int leadFrames, int tailFrames, out string info) {
    var fr = Frames(d);
    int vs = -1, ve = -1;
    for (int i = 0; i < fr.Count; i++) if (fr[i][2] > 0 && fr[i][3] >= T) { if (vs < 0) vs = i; ve = i; }
    if (vs < 0) { info = "no voice"; return d; }

    int start = Math.Max(1, vs - leadFrames);
    int need = fr[start][4], have = 0;                 // main_data_begin of first kept frame
    while (have < need && start > 1) { start--; have += fr[start][1] - 4 - 9; }
    int end = Math.Min(fr.Count - 1, ve + tailFrames);

    int from = fr[start][0], to = fr[end][0] + fr[end][1];
    var outb = new byte[to - from];
    Array.Copy(d, from, outb, 0, outb.Length);
    info = string.Format("{0,5}ms -> {1,5}ms", fr.Count * 24, (end - start + 1) * 24);
    return outb;
  }
}
