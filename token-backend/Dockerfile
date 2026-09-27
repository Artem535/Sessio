FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ninja-build curl zip unzip tar pkg-config \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release -DPCM_TB_BUILD_TESTS=OFF \
    && cmake --build build

FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --shell /usr/sbin/nologin token-backend \
    && mkdir -p /data && chown token-backend:token-backend /data
COPY --from=build /src/build/pcm_token_backend /usr/local/bin/pcm_token_backend
ENV DB_PATH=/data/token-backend.sqlite3
VOLUME ["/data"]
EXPOSE 8080
USER token-backend
ENTRYPOINT ["/usr/local/bin/pcm_token_backend"]
