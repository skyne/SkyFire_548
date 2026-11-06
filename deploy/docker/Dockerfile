ARG UBUNTU_VERSION=24.04

FROM ubuntu:$UBUNTU_VERSION AS runtime

ARG TZ=Etc/UTC
ARG DEBIAN_FRONTEND=noninteractive
ARG USER_ID=1000
ARG GROUP_ID=1000
ARG DOCKER_USER=skyfire

ENV TZ=$TZ
ENV SF_COMPONENT=undefined
ENV LD_LIBRARY_PATH=/opt/openssl-4.0.0/lib64
ENV PATH="/skyfire/bin:${PATH}"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates tzdata bash \
        libmysqlclient21 libreadline8 libtinfo6 zlib1g \
        libstdc++6 libgcc-s1 g++-14 \
        mysql-client curl unzip \
        adduser \
    && ln -snf "/usr/share/zoneinfo/$TZ" /etc/localtime \
    && echo "$TZ" > /etc/timezone \
    && rm -rf /var/lib/apt/lists/* \
    && (userdel --remove ubuntu || true) \
    && addgroup --gid "$GROUP_ID" "$DOCKER_USER" \
    && adduser --disabled-password --gecos '' --uid "$USER_ID" --gid "$GROUP_ID" "$DOCKER_USER" \
    && mkdir -p /skyfire/bin /skyfire/etc /skyfire/logs /skyfire/data /skyfire/ref/etc \
    && chown -R "$DOCKER_USER:$DOCKER_USER" /skyfire

WORKDIR /skyfire

COPY --chmod=755 deploy/docker/entrypoint.sh /skyfire/entrypoint.sh
COPY --chmod=755 deploy/docker/import-sfdb.sh /skyfire/import-sfdb.sh

USER $DOCKER_USER
ENTRYPOINT ["/usr/bin/env", "bash", "/skyfire/entrypoint.sh"]

FROM runtime AS authserver
ENV SF_COMPONENT=authserver
CMD ["authserver"]

FROM runtime AS worldserver
ENV SF_COMPONENT=worldserver
CMD ["worldserver"]

FROM runtime AS clustergateway
ENV SF_COMPONENT=clustergateway
CMD ["clustergateway"]

FROM runtime AS db-import
ENV SF_COMPONENT=db-import
USER root
ENTRYPOINT ["/usr/bin/env", "bash", "/skyfire/import-sfdb.sh"]
CMD []
