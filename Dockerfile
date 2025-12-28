# =============================================================================
# Dockerfile - Musl Static Build for Bench
# =============================================================================
FROM alpine:latest AS builder

# 1. Install Build Tools
# Tambahin 'xxd' disini wajib!
RUN apk add --no-cache \
    clang \
    lld \
    llvm \
    libc++-static \
    libc++-dev \
    compiler-rt \
    cmake \
    make \
    linux-headers \
    llvm-libunwind-static \
    perl \
    bash \
    xxd 

# Set working directory
WORKDIR /src

# Copy source files
COPY CMakeLists.txt ./
COPY cmake/ ./cmake/
COPY include/ ./include/
COPY src/ ./src/

# 2. Configure and build
# Kita pake Clang + LLVM Linker (lld) + libc++ static
RUN mkdir build && cd build && \
    CC=clang CXX=clang++ cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_AR=/usr/bin/llvm-ar \
        -DCMAKE_RANLIB=/usr/bin/llvm-ranlib \
        -DCMAKE_EXE_LINKER_FLAGS="-static -fuse-ld=lld -rtlib=compiler-rt" \
        -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
        -DOPENSSL_USE_STATIC_LIBS=ON && \
    make -j$(nproc)

# Strip binary biar size makin kecil (buang debug symbols)
RUN strip build/bench

# =============================================================================
# Runtime Stage - PURE SCRATCH (Kosong melompong)
# =============================================================================
FROM scratch AS runtime

# Copy binary doang.
# GAK PERLU copy ca-certificates.crt lagi (karena udah embedded)
COPY --from=builder /src/build/bench /bench

ENTRYPOINT ["/bench"]