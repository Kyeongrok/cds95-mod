#pragma once

// 훅을 걸고 푼다. 성공하면 1.
int  Overlay_Install(void);
void Overlay_Uninstall(void);

// 화살표를 보일지. 메뉴가 이것만 켜고 끈다.
int  Overlay_Enabled(void);
void Overlay_SetEnabled(int on);
