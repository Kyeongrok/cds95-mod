# 게임 화면에 직접 그리기 — 스프라이트 함수 0x48AC30

WindArrowKR(항해 화면 풍향·해류 화살표)을 만들며 뜯은 것이다. 요지는 **게임 화면에 무언가
얹고 싶으면 서피스를 직접 만질 필요가 없다** 는 것 — 게임이 배와 말을 찍는 함수를 그대로
부르면 된다.

## 그리는 함수 — `0x0048AC30`

```c
// __thiscall, ret 0x1C. this = 지도 객체(0x61B2D0) — 해상 렌더러의 this 와 같다.
void Draw(void* this, void* surface, const void* bitmap, int frame,
          int x, int y, int w, int h);
```

| 인자 | 뜻 |
|---|---|
| `surface` | 아틀라스를 담은 IDirectDrawSurface. **화면효과 켠** 갈래가 쓴다 |
| `bitmap` | 그 프레임의 8bpp 그림 포인터(연속 `w*h`). **화면효과 끈** 갈래가 쓴다 |
| `frame` | 세로로 쌓인 아틀라스의 몇 번째 장. 서피스 쪽 `srcRect.top = frame * h` |
| `x, y` | 화면 점. **y 에 +0x20 을 함수가 알아서 더한다**(위 32점은 상태 띠) |
| `w, h` | 한 장의 크기 |

공짜로 붙는 것 셋.

- **갈래를 안 가려도 된다.** 첫머리에서 `[0x5A4D1D]&1 && [0x5A4D1A]&2 && !(this+0xA0)&1`
  을 보고 DirectDraw `Blt` 와 소프트 복사 중 하나를 고른다. 그래서 두 벌(서피스 + 메모리)만
  넘겨 두면 화면효과를 켜든 끄든 같은 그림이 나온다.
- **색인 0 이 투명이다.** Blt 갈래는 `DDBLT_KEYSRCOVERRIDE`(0x10000)에 빈 DDBLTFX 를 넘겨
  컬러키 0 을 쓴다. 게임 스프라이트(배 48x48)와 같은 규칙이다.
- **클리핑도 한다.** `[0x5AA2D0..0x5AA2DC]`(뷰포트/화면 크기)로 잘라 낸다.

호출 예가 렌더러 안에 둘 있다 — 배(`0x48A8AA` 부근, 아틀라스 `0x5D68C8` / 서피스
`[0x569FE8]`)와 말(`0x48A7BC`, `0x6092D0`).

## 아틀라스 서피스 만들기 — `0x004BA5F4`

```c
// __cdecl. 0 이면 성공. 속은 [0x62D360] 의 IDirectDraw 로 CreateSurface(vtable+0x18).
int MakeSurface(DDSURFACEDESC* desc, IDirectDrawSurface** out, int zero);
```

게임이 말 아틀라스를 올리는 자리(`0x489EF8`~`0x489FCC`)가 본보기다.

```
desc.dwSize        = 0x6C
desc.dwFlags       = 7        (CAPS | HEIGHT | WIDTH)
desc.dwHeight      = 한 장 높이 x 장수
desc.dwWidth       = 한 장 폭
desc.ddsCaps.dwCaps= 0x840    (OFFSCREENPLAIN | SYSTEMMEMORY)
MakeSurface(&desc, &surface, 0)
Lock(vtable+0x64) → desc+0x10 = pitch, desc+0x24 = 픽셀 → 줄 단위로 옮긴다 → Unlock(vtable+0x80)
```

`pitch` 가 폭보다 넓을 수 있으니 통째 `memcpy` 하지 말고 줄 단위로 옮길 것. 시스템 메모리
서피스라 로스트를 걱정하지 않아도 된다.

## 화면 좌표 ↔ 지도 칸

해상 렌더러 `0x0048A1E0`(`__thiscall`, ret 4)의 두 겹 루프에서 그대로 읽었다.

```
원점 칸       [0x5B63A8] (경도축) · [0x5B63AC] (위도축)      ← 칸 단위다
한 화면 타일 수 [0x61B2D0+0xEC] x [0x61B2D0+0xF0]
지도 칸       x = (원점x + 화면열) mod 2500 ,  y = 원점y + 화면행
화면 점       (화면열 x 16, 화면행 x 16)                     ← y 의 +32 는 그리는 함수 몫
칸 부류       0x426710(x, y)  __thiscall ret 8 — 0·1 이 바다, 2 이상이 뭍
```

렌더러의 `this` 가 곧 지도 객체(`0x61B2D0`)다 — 그리는 함수와 부류 판정이 같은 `this` 를 쓴다.

## 어디에 훅을 거나

`0x48A1E0` 리턴 직후가 맞다. 타일·함대·구름·상태표시를 다 그린 뒤라 얹은 것이 맨 위에 온다.
첫머리 `0x48A213` 의 `test byte [0x5A4D18], 0x18` 이 서지 않으면 지도를 아예 안 그리고
단색으로 칠하고 나가므로, 그 갈래에서는 우리도 손을 떼야 한다.

## 곁길로 샜던 것

- 처음엔 백버퍼 `[0x569FF0]` 을 직접 Lock 해서 픽셀을 찍을 생각이었다. 되기는 하지만
  화면효과 끈 갈래(소프트 경로, `0x62B2F0` 캔버스)를 따로 다뤄야 하고 팔레트 근사색도
  우리가 골라야 한다. `0x48AC30` 은 그 둘을 다 덮는다.
- 타일 번호를 갈아끼우는 수(`GetCell` call-site `0x48A402` 리다이렉트)도 있었다. 스크롤·
  좌표 변환이 공짜라는 점은 좋은데 화살표가 16x16 격자에 갇히고, 바다 타일 위에 합성하려면
  결국 아틀라스 서피스를 Lock 해야 해서 이득이 없었다.

관련: 옵시디안 [[12.분석-WORLD.CDS 화면 그리기(런타임)]] · [[16.분석-풍향·풍속·해류]] ·
[[31.분석-바다 물결과 구름]]
