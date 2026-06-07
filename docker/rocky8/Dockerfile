# syntax=docker/dockerfile:1
#
# Build a release cadd0040 binary on Rocky Linux 8 (glibc, RHEL-compatible).
#
#   docker build -f docker/rocky8/Dockerfile -t cadd0040-rocky8 .
#
# The default target copies the stripped binary to /opt/cadd0040/cadd0040.
# Use scripts/docker-build-rocky8.sh to build the image and extract it locally.

FROM rockylinux:8 AS builder

RUN dnf -y install epel-release \
    && /usr/bin/crb enable \
    && dnf -y install \
        gcc \
        gcc-c++ \
        cmake \
        make \
        git \
    && dnf clean all

WORKDIR /src

COPY CMakeLists.txt Makefile ./
COPY src/ src/
COPY tests/ tests/

RUN cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build-release --parallel "$(nproc)" \
    && strip build-release/cadd0040

FROM scratch AS export
COPY --from=builder /src/build-release/cadd0040 /cadd0040

FROM rockylinux:8 AS runtime
COPY --from=builder /src/build-release/cadd0040 /opt/cadd0040/cadd0040
RUN chmod 755 /opt/cadd0040/cadd0040
ENTRYPOINT ["/opt/cadd0040/cadd0040"]
