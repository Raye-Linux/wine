/*
 * Which font a space really gives for the names Windows programs ask for,
 * and whether it is monospace or a bitmap. Courier New must come back
 * monospace, MS Sans Serif must not come back as a bitmap.
 */
#include <windows.h>
#include <stdio.h>
static const wchar_t *names[] = { L"MS Shell Dlg", L"MS Shell Dlg 2", L"MS Sans Serif", L"Microsoft Sans Serif",
  L"Tahoma", L"Segoe UI", L"Segoe UI Variable", L"Arial", L"Arial Narrow", L"Times New Roman", L"Courier New",
  L"Calibri", L"Cambria", L"Consolas", L"Verdana", L"Georgia", L"Trebuchet MS", L"Lucida Console",
  L"Comic Sans MS", L"Impact", L"Segoe UI Emoji", L"Segoe UI Symbol", L"Microsoft YaHei", L"SimSun",
  L"MS Gothic", L"Yu Gothic UI", L"Malgun Gothic", L"Meiryo", 0 };
int main(void) {
  HDC dc = GetDC(0); UINT on = 0, type = 0;
  SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &on, 0);
  SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0);
  printf("smoothing=%u type=%u dpi=%d\n", on, type, GetDeviceCaps(dc, LOGPIXELSY));
  for (int i = 0; names[i]; i++) {
    LOGFONTW lf = {0}; lf.lfHeight = -12; lf.lfCharSet = DEFAULT_CHARSET; wcscpy(lf.lfFaceName, names[i]);
    HFONT f = CreateFontIndirectW(&lf); HGDIOBJ o = SelectObject(dc, f);
    wchar_t face[64] = {0}; TEXTMETRICW tm; GetTextFaceW(dc, 64, face); GetTextMetricsW(dc, &tm);
    wchar_t fam[96] = L"?"; char buf[4096]; OUTLINETEXTMETRICW *otm = (OUTLINETEXTMETRICW *)buf;
    if (GetOutlineTextMetricsW(dc, sizeof(buf), otm)) wcsncpy(fam, (wchar_t *)(buf + (UINT_PTR)otm->otmpFamilyName), 95);
    printf("%-22ls -> %-24ls avgw=%ld %s %s\n", names[i], fam, tm.tmAveCharWidth, (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) ? "prop" : "MONO",
           (tm.tmPitchAndFamily & TMPF_TRUETYPE) ? "tt" : (tm.tmPitchAndFamily & TMPF_VECTOR) ? "vec" : "BITMAP");
    SelectObject(dc, o); DeleteObject(f);
  }
  return 0;
}
