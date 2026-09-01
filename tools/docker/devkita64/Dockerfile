# nx build environment: devkitPro's devkitA64 image, pinned by tag and digest,
# plus the Python modules the cook and packaging tools import. tools/build.py
# builds and uses this when the host has no devkitPro of its own.
# See docs/nx/BUILD.md.
FROM devkitpro/devkita64:20260219@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282

RUN apt-get update && \
    apt-get install -y --no-install-recommends python3-pil python3-jsonschema && \
    rm -rf /var/lib/apt/lists/*
