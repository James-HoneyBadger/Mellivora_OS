FROM scratch
LABEL org.opencontainers.image.title="Mellivora OS"
LABEL org.opencontainers.image.description="Mellivora OS - a bare-metal 32-bit x86 hobby OS in NASM assembly"
LABEL org.opencontainers.image.source="https://github.com/James-HoneyBadger/Mellivora_OS"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.opencontainers.image.version="1.15.0"
COPY mellivora.img /mellivora.img
