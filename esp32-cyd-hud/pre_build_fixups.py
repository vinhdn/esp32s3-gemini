"""
LVGL 9.x dong goi lv_blend_helium.S (assembly rieng cho ARM Helium/MVE) khong
co guard __ASSEMBLY__ day du quanh phan include header C (stdint.h...) - tren
target Xtensa (ESP32, khong phai ARM) assembler bao loi "unknown opcode
'typedef'" ngay ca khi LV_USE_DRAW_SW_ASM=LV_DRAW_SW_ASM_NONE (noi dung that
cua file bi vo hieu boi #if nhung phan include header van chay). Day la file
CHET tren kien truc nay (khong ARM MVE) - xoa truoc moi lan build de khong
phai sua tay sau moi lan `pio pkg install`/cai lai thu vien tu dau.
"""
Import("env")
import os
import glob

for path in glob.glob(
    os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                  "lvgl", "src", "draw", "sw", "blend", "helium", "lv_blend_helium.S")
):
    if os.path.exists(path):
        os.remove(path)
        print(f"[pre_build_fixups] Removed dead ARM-only file: {path}")
