# tools/hex_decoder_extra.py
# PlatformIO extra-script (pre-build) that adds tools/hex_decoder.cpp to the
# [env:hex_decoder] native build.
#
# build_src_filter only operates on src_dir (= src by default), so this script
# is the correct PlatformIO mechanism to compile a file that lives outside
# src/.  It uses BuildSources() to inject the single extra translation unit
# before the link step.
Import("env")  # type: ignore[name-defined]

env.BuildSources(  # type: ignore[name-defined]
    "$BUILD_DIR/tool_src",           # intermediate object directory
    env.subst("$PROJECT_DIR/tools"), # type: ignore[name-defined]  # source directory
    ["+<hex_decoder.cpp>"],          # include only this file
)
