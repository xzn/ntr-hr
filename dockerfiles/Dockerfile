FROM docker.io/library/archlinux:base-devel-20260823.0.578598

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV DEVKITPPC=/opt/devkitpro/devkitPPC
ENV PATH="$PATH:/opt/devkitpro/tools/bin"

RUN pacman -Syuu llvm clang rustup wget cmake ninja python git lld unzip vim zip nvm --noconfirm --needed && \
    export MAKEROM_VERSION=0.18.4 && \
    wget "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v$MAKEROM_VERSION/makerom-v$MAKEROM_VERSION-ubuntu_x86_64.zip" && \
    unzip "makerom-v$MAKEROM_VERSION-ubuntu_x86_64.zip" && \
    chmod +x makerom && \
    rm "makerom-v$MAKEROM_VERSION-ubuntu_x86_64.zip" && \
    export BANNERTOOL_COMMIT=3767e03b0492654bdff7be7beacf32b5b6a60415 && \
    git clone --revision=$BANNERTOOL_COMMIT --depth=1 https://github.com/carstene1ns/3ds-bannertool.git && \
    cd 3ds-bannertool && \
        cmake . && \
        cmake --build . && \
        cmake --install . && \
        cd .. && \
    rm -rf 3ds-bannertool && \
    export CLANG_VERSION=`clang --version | grep version | cut -d' ' -f3` && \
    wget "https://github.com/llvm/llvm-project/releases/download/llvmorg-$CLANG_VERSION/llvm-project-$CLANG_VERSION.src.tar.xz" && \
    tar xf "llvm-project-$CLANG_VERSION.src.tar.xz" && \
    mkdir compiler-rt-build && \
    cd compiler-rt-build && \
        cmake "../llvm-project-$CLANG_VERSION.src/compiler-rt" -G Ninja \
            -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
            -DCOMPILER_RT_OS_DIR="baremetal" \
            -DCOMPILER_RT_BUILD_BUILTINS=ON \
            -DCOMPILER_RT_BUILD_SANITIZERS=OFF \
            -DCOMPILER_RT_BUILD_XRAY=OFF \
            -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
            -DCOMPILER_RT_BUILD_PROFILE=OFF \
            -DCOMPILER_RT_BUILD_MEMPROF=OFF \
            -DCMAKE_C_COMPILER=/usr/bin/clang \
            -DCMAKE_C_COMPILER_TARGET="arm-none-eabi" \
            -DCMAKE_ASM_COMPILER_TARGET="arm-none-eabi" \
            -DCMAKE_AR=/usr/bin/llvm-ar \
            -DCMAKE_NM=/usr/bin/llvm-nm \
            -DCMAKE_RANLIB=/usr/bin/llvm-ranlib \
            -DCOMPILER_RT_BAREMETAL_BUILD=ON \
            -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
            -DLLVM_CONFIG_PATH=/usr/bin/llvm-config \
            -DCMAKE_C_FLAGS="-march=armv6k -mfloat-abi=hard" \
            -DCMAKE_ASM_FLAGS="-march=armv6k -mfloat-abi=hard" \
            -DCOMPILER_RT_INCLUDE_TESTS=OFF && \
        ninja && \
        ninja install && \
        cd .. && \
    rm -rf "llvm-project-$CLANG_VERSION.src.tar.xz" "llvm-project-$CLANG_VERSION.src" compiler-rt-build && \
    export RUST_VERSION=2026-08-02 && \
    rustup toolchain install nightly-$RUST_VERSION && \
    rustup component add rust-src && \
    pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com && \
    pacman-key --init && \
    pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0 && \
    pacman -U https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst --noconfirm --needed && \
    echo '[dkp-libs]' >> /etc/pacman.conf && \
    echo 'Server = https://pkg.devkitpro.org/packages' >> /etc/pacman.conf && \
    echo '[dkp-linux]' >> /etc/pacman.conf && \
    echo 'Server = https://pkg.devkitpro.org/packages/linux/$arch/' >> /etc/pacman.conf && \
    pacman -Syuu devkitARM devkitarm-rules 3dstools citro3d libctru picasso 3ds-libpng --noconfirm --needed && \
    pacman -Scc --noconfirm && \
    export CLANG_VERSION_MAJOR=`echo $CLANG_VERSION | cut -d'.' -f1` && \
    mkdir -p /usr/lib/clang/$CLANG_VERSION_MAJOR/lib/arm-unknown-none-eabi && \
    ln -s /usr/local/lib/baremetal/libclang_rt.builtins-arm.a /usr/lib/clang/$CLANG_VERSION_MAJOR/lib/arm-unknown-none-eabi/libclang_rt.builtins.a && \
    ln -s /makerom /opt/devkitpro/tools/bin/
