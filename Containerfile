FROM scratch
LABEL org.opencontainers.image.title="Mellivora OS"
LABEL org.opencontainers.image.description="Mellivora OS - a bare-metal 32-bit x86 hobby OS in NASM assembly"
LABEL org.opencontainers.image.source="https://github.com/James-HoneyBadger/Mellivora_OS"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.opencontainers.image.version="13.0.0"
LABEL org.opencontainers.image.documentation="https://github.com/James-HoneyBadger/Mellivora_OS/blob/main/docs/INSTALL.md"
# The payload is a complete Mellivora disk image (BIOS boot: sector 0, stage 2,
# kernel, HBFS). The publish workflow builds it with
#   make ISO_IMAGE_MB=256 mellivora-install.img && cp mellivora-install.img mellivora.img
# so the layer is the size the installer disc uses. Building it with `make full`
# instead produces a 2 GB image that is 99.7% empty.
COPY mellivora.img /mellivora.img
