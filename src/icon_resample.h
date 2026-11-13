#pragma once
// icon_resample.h — 16x16 점 그림을 다른 크기(20·24·32 …)로 키우는 고품질 필터 (windows.h 가 필요 없는 순수 C).
// src/dst 는 0xAARRGGBB (비-미리곱). 크기 n(원본)·m(목표)는 정사각형.
void IconResample(const unsigned *src, int n, unsigned *dst, int m);
