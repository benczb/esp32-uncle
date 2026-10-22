#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_model.c main/passport_model.c -o /tmp/ai-passport-model-test
    /tmp/ai-passport-model-test
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_features.c main/passport_features.c main/passport_model.c -o /tmp/ai-passport-features-test
    /tmp/ai-passport-features-test
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_features.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_train.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_companion.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_voice.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_speech.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_speech_buffer.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_speech_handoff.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_speech_allocation.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_reply.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_photo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_accounts.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_muse_pairing.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_haze.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_bus.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_bus_cache.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_settings.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_brightness.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_web.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_private_defaults.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    json_dir="${repo_root}/.toolchains/esp-idf-v5.5.3/components/json/cJSON"
    if [[ ! -f "${json_dir}/cJSON.c" && -n "${IDF_PATH:-}" ]]; then json_dir="${IDF_PATH}/components/json/cJSON"; fi
    if [[ -f "${json_dir}/cJSON.c" ]]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${json_dir}" \
            tests/test_passport_public_data.c main/passport_public_data.c main/passport_usage.c "${json_dir}/cJSON.c" -lm \
            -o "${test_dir}/test_passport_public_data"
    elif pkg-config --exists libcjson; then
        local -a cjson_flags
        read -r -a cjson_flags <<< "$(pkg-config --cflags --libs libcjson)"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            tests/test_passport_public_data.c main/passport_public_data.c main/passport_usage.c \
            "${cjson_flags[@]}" -lm -o "${test_dir}/test_passport_public_data"
    else
        echo "ERROR: activate ESP-IDF 5.5.3 or install libcjson-dev and pkg-config for host tests." >&2
        return 1
    fi
    "${test_dir}/test_passport_public_data"
    if [[ -f "${json_dir}/cJSON.c" ]]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${json_dir}" \
            tests/test_passport_train_fetch.c main/passport_train_fetch.c main/passport_train.c "${json_dir}/cJSON.c" \
            -o "${test_dir}/test_passport_train_fetch"
    else
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            tests/test_passport_train_fetch.c main/passport_train_fetch.c main/passport_train.c "${cjson_flags[@]}" \
            -o "${test_dir}/test_passport_train_fetch"
    fi
    "${test_dir}/test_passport_train_fetch"
    if [[ -f "${json_dir}/cJSON.c" ]]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/passport_direct_mocks -Imain -I"${json_dir}" \
            tests/test_passport_direct.c main/passport_public_data.c "${json_dir}/cJSON.c" -lm -o "${test_dir}/test_passport_direct"
    else
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/passport_direct_mocks -Imain \
            tests/test_passport_direct.c main/passport_public_data.c "${cjson_flags[@]}" -lm -o "${test_dir}/test_passport_direct"
    fi
    "${test_dir}/test_passport_direct"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -DCONFIG_BSP_LVGL_DRAW_BUFFER_LINES=16 \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init_small"
    "${test_dir}/test_bsp_lvgl_init_small"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" -Wl,--gc-sections \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
