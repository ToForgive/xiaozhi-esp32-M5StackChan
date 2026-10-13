#!/usr/bin/env python3
"""Compile the K151 board and shared OTA/NVS changes to an ESP-IDF archive.

This is an API/target compilation check, not a linked XiaoZhi firmware build.
Run in an activated ESP-IDF 6.1 environment. --fetch only retrieves public Git sources.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "docs/stackchan-k151/native-reference-lock.json"

def run(command, **kwargs):
    subprocess.run(command, check=True, **kwargs)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--allow-ota", action="store_true", help="Also compile the opt-in K151 OTA policy branch")
    args = parser.parse_args()
    references = args.reference_root.resolve()
    references.mkdir(parents=True, exist_ok=True)
    for name, ref in json.loads(LOCK.read_text()).items():
        path = references / name
        if not path.exists() and args.fetch:
            run(["git", "clone", "--filter=blob:none", "--no-checkout", ref["url"], str(path)])
            if ref.get("sparse"):
                run(["git", "-C", str(path), "sparse-checkout", "set", *ref["sparse"]])
            run(["git", "-C", str(path), "checkout", "--detach", ref["sha"]])
        if not path.is_dir():
            parser.error(f"Missing {path}; provide the pinned checkout or use --fetch")
        actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        if actual != ref["sha"]:
            parser.error(f"{name} is {actual}, expected {ref['sha']}; existing work is never reset")
    work = args.work_dir.resolve()
    marker = work / ".k151-native-check"
    if work.exists() and any(work.iterdir()) and not marker.exists():
        parser.error("Work directory is not an owned K151 native check; choose an empty directory")
    work.mkdir(parents=True, exist_ok=True)
    marker.touch()
    (work / "main").mkdir(exist_ok=True)
    generated = work / "generated/assets"
    for locale in ("zh-CN", "en-US"):
        dest = generated / "locales" / locale
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "main/assets/locales" / locale / "language.json", dest / "language.json")
    run([sys.executable, str(ROOT / "scripts/gen_lang.py"), "--language", "zh-CN", "--output", str(generated / "lang_config.h")])
    # Quoted CMake paths support spaces. The selected Git references provide headers;
    # only local IDF/LVGL dependencies are linked to the archive target.
    (work / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "{references}/lvgl-reference")
include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(k151-native-check)
''')
    include_dirs = [
        ROOT / "main", ROOT / "main/audio", ROOT / "main/audio/demuxer",
        ROOT / "main/boards/common", ROOT / "main/protocols", ROOT / "main/display",
        ROOT / "main/display/lvgl_display", ROOT / "main/display/lvgl_display/jpg", work / "generated",
        references / "cjson-reference", references / "ml307-reference/include",
        references / "esp-sr-reference/src/include", references / "esp-sr-reference/include/esp32s3",
        references / "esp-bsp-reference/components/esp_lvgl_port/include",
        references / "esp-bsp-reference/components/lcd/esp_lcd_ili9341/include",
        references / "esp-adf-reference/components/esp_codec_dev/include",
        references / "esp-adf-reference/components/esp_codec_dev/interface",
        references / "esp-adf-reference/components/esp_codec_dev/device/include",
        references / "esp-video-reference/esp_video/include", references / "esp-video-reference/esp_cam_sensor/include",
        references / "esp-adf-libs-reference/esp_audio_codec/include",
        references / "esp-adf-libs-reference/esp_audio_codec/include/encoder",
        references / "esp-adf-libs-reference/esp_audio_codec/include/encoder/impl",
        references / "esp-adf-libs-reference/esp_audio_codec/include/decoder",
        references / "esp-adf-libs-reference/esp_audio_codec/include/decoder/impl",
        references / "esp-adf-libs-reference/esp_audio_effects/include",
    ]
    includes = "\n".join(f'"{path}"' for path in include_dirs)
    cmake = f'''set(K151 "{ROOT}/main/boards/m5stack/stackchan-k151")
set(XIAOZHI "{ROOT}/main")
file(GLOB_RECURSE CHECK_SOURCES ${{K151}}/*.cpp ${{K151}}/hal/*.cc ${{K151}}/hal/*.c ${{K151}}/stackchan/*.c)
list(APPEND CHECK_SOURCES ${{K151}}/k151_display.cc ${{K151}}/k151_ir.cc ${{K151}}/stackchan_k151.cc ${{K151}}/cores3_audio_codec.cc ${{K151}}/k151_application.cc ${{XIAOZHI}}/ota.cc ${{XIAOZHI}}/main.cc)
idf_component_register(SRCS ${{CHECK_SOURCES}} INCLUDE_DIRS ${{K151}} ${{K151}}/vendor/uitk ${{K151}}/vendor/uitk/lvgl
{includes}
REQUIRES lvgl-reference mbedtls esp_driver_i2c esp_driver_uart esp_driver_rmt esp_driver_cam esp_driver_i2s esp_lcd nvs_flash esp_pm esp_psram esp_timer app_update esp_app_format spi_flash esp_netif esp_wifi efuse)
set_property(TARGET ${{COMPONENT_LIB}} PROPERTY CXX_STANDARD 23)
target_compile_options(${{COMPONENT_LIB}} PRIVATE -Wno-missing-field-initializers)
target_compile_definitions(${{COMPONENT_LIB}} PRIVATE CONFIG_BOARD_TYPE_M5STACK_STACKCHAN_K151=1 CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE=1 CONFIG_CODEC_ES7210_SUPPORT=1 CONFIG_CODEC_AW88298_SUPPORT=1 CONFIG_OTA_URL="https://api.tenclass.net/xiaozhi/ota/")
'''
    if args.allow_ota:
        cmake += 'target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_K151_ALLOW_OTA=1)\n'
    (work / "main/CMakeLists.txt").write_text(cmake)
    (work / "sdkconfig.defaults").write_text('''CONFIG_IDF_TARGET="esp32s3"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_LV_USE_CLIB_MALLOC=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_24=y
CONFIG_LV_USE_LODEPNG=y
CONFIG_LV_USE_QRCODE=y
CONFIG_LV_BUILD_EXAMPLES=n
CONFIG_LV_BUILD_DEMOS=n
''')
    env = dict(os.environ, IDF_COMPONENT_MANAGER="0")
    run(["idf.py", "-DIDF_TARGET=esp32s3", "reconfigure"], cwd=work, env=env)
    run(["cmake", "--build", "build", "--target", "__idf_main", "-j", "4"], cwd=work, env=env)
    print(f"Compiled archive: {work}/build/esp-idf/main/libmain.a (NOT a linked firmware)")

if __name__ == "__main__":
    main()
