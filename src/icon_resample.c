// icon_resample.c — Lanczos-3 (분리형) 리샘플러. 2026-11-13 오너 요청: "16px 기준, 나머지는 초고품질 리사이즈 필터(Lanczos 등)".
//   · 선형광(감마 보정)에서 섞는다 — 어두운 글자와 밝은 바탕이 sRGB 값으로 섞이면 글자가 가늘어 보인다.
//   · 알파는 미리곱해서 섞는다(네 모서리의 투명 점이 번지지 않는다), 가장자리는 바깥 점을 되풀이한다.
//   · 링잉(고리 모양 오버슈트)은 0..1 로 잘라 낸다. 결과는 아이콘 DIB 용 미리곱 BGRA.
#include "icon_resample.h"
#include <math.h>

#define LOBES 3
#define PI_ 3.14159265358979323846

static double Lanczos(double x) {
    if (x < 0) x = -x;
    if (x >= LOBES) return 0.0;
    if (x < 1e-9) return 1.0;
    double px = PI_ * x;
    return LOBES * sin(px) * sin(px / LOBES) / (px * px);
}

static double ToLinear(unsigned v) {
    double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static unsigned ToSrgb(double l) {
    if (l <= 0) return 0;
    if (l >= 1) return 255;
    double c = l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
    int v = (int)(c * 255.0 + 0.5);
    return (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v);
}

#define MAXN 32      // 원본 최대
#define MAXM 64      // 목표 최대 (400% 트레이)
// 목표 한 칸이 원본의 어느 점들에서 얼마씩 섞이는가
static int Weights(int n, int m, int o, int idx[2 * LOBES + 2], double w[2 * LOBES + 2]) {
    double c = (o + 0.5) * n / m - 0.5;   // 원본 좌표계의 중심
    int lo = (int)floor(c) - LOBES + 1, k = 0;
    double sum = 0;
    for (int i = lo; i < lo + 2 * LOBES; i++) {
        double v = Lanczos(c - i);
        idx[k] = i < 0 ? 0 : i >= n ? n - 1 : i;
        w[k++] = v; sum += v;
    }
    for (int i = 0; i < k; i++) w[i] /= sum;
    return k;
}

void IconResample(const unsigned *src, int n, unsigned *dst, int m) {
    if (n < 1 || m < 1) return;
    if (n > MAXN || m > MAXM) {   // 범위 밖 크기는 가장 가까운 점 (스택을 아끼려는 안전장치)
        for (int y = 0; y < m; y++) for (int x = 0; x < m; x++) dst[y * m + x] = src[(y * n / m) * n + x * n / m];
        return;
    }
    static double lin[256];
    static int init;
    double pm[MAXN * MAXN][4];             // 선형광·미리곱 r g b a
    double tmp[MAXN * MAXM][4];            // 가로로 늘린 것 (n 행 x m 열)
    if (!init) { for (int i = 0; i < 256; i++) lin[i] = ToLinear((unsigned)i); init = 1; }
    for (int i = 0; i < n * n; i++) {
        unsigned p = src[i];
        double a = ((p >> 24) & 255) / 255.0;
        pm[i][0] = lin[(p >> 16) & 255] * a; pm[i][1] = lin[(p >> 8) & 255] * a; pm[i][2] = lin[p & 255] * a; pm[i][3] = a;
    }
    int idx[2 * LOBES + 2]; double w[2 * LOBES + 2];
    for (int ox = 0; ox < m; ox++) {
        int k = Weights(n, m, ox, idx, w);
        for (int y = 0; y < n; y++) {
            double acc[4] = { 0, 0, 0, 0 };
            for (int j = 0; j < k; j++) for (int ch = 0; ch < 4; ch++) acc[ch] += w[j] * pm[y * n + idx[j]][ch];
            for (int ch = 0; ch < 4; ch++) tmp[y * m + ox][ch] = acc[ch];
        }
    }
    for (int oy = 0; oy < m; oy++) {
        int k = Weights(n, m, oy, idx, w);
        for (int ox = 0; ox < m; ox++) {
            double acc[4] = { 0, 0, 0, 0 };
            for (int j = 0; j < k; j++) for (int ch = 0; ch < 4; ch++) acc[ch] += w[j] * tmp[idx[j] * m + ox][ch];
            double a = acc[3] < 0 ? 0 : acc[3] > 1 ? 1 : acc[3];
            unsigned r = 0, g = 0, b = 0, A = (unsigned)(a * 255.0 + 0.5);
            if (a > 1e-6) {
                r = ToSrgb(acc[0] / a); g = ToSrgb(acc[1] / a); b = ToSrgb(acc[2] / a);   // 비-미리곱 sRGB
                r = (r * A + 127) / 255; g = (g * A + 127) / 255; b = (b * A + 127) / 255; // 미리곱
            }
            dst[oy * m + ox] = (A << 24) | (r << 16) | (g << 8) | b;
        }
    }
}
