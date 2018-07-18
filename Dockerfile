# ##########   #######   ############
FROM ecr.vip.ebayc3.com/sds/sds_cpp_base:1.4
LABEL description="Automated SDS compilation"

ARG CONAN_CHANNEL
ARG CONAN_USER
ARG CONAN_PASS=${CONAN_USER}
ENV CONAN_USER=${CONAN_USER:-sds}
ENV CONAN_CHANNEL=${CONAN_CHANNEL:-dev}
ENV CONAN_PASS=${CONAN_PASS:-password}

COPY conanfile.py /tmp/source/
COPY cmake/ /tmp/source/cmake
COPY CMakeLists.txt /tmp/source/
COPY LICENSE.md /tmp/source/
COPY src/ /tmp/source/src
COPY test_package/ /tmp/source/test_package

RUN conan create /tmp/source "${CONAN_USER}"/"${CONAN_CHANNEL}"
RUN conan create -pr debug /tmp/source "${CONAN_USER}"/"${CONAN_CHANNEL}"

CMD set -eux; \
    PKG_NAME=$(grep ' name =' /tmp/source/conanfile.py | awk '{print $3}' | sed 's,",,g') \
    PKG_VERSION=$(grep 'version =' /tmp/source/conanfile.py | awk '{print $3}'); \
    PKG_VERSION="${PKG_VERSION%\"}"; \
    PKG_VERSION="${PKG_VERSION#\"}"; \
    conan user -r origin -p "${CONAN_PASS}" sds; \
    conan upload ${PKG_NAME}/"${PKG_VERSION}"@"${CONAN_USER}"/"${CONAN_CHANNEL}" --all -r origin;
# ##########   #######   ############
