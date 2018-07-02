# ##########   #######   ############
FROM ecr.vip.ebayc3.com/sds/sds_cpp_base:1.4
LABEL description="Automated compilation for SDS HomeStore"

ARG CONAN_CHANNEL
ARG CONAN_USER
ENV CONAN_USER=${CONAN_USER:-sds}
ENV CONAN_CHANNEL=${CONAN_CHANNEL:-testing}

COPY conanfile.py /tmp/source/
COPY CMakeLists.txt /tmp/source/
COPY LICENSE.md /tmp/source/
COPY cmake/ /tmp/source/cmake
COPY src/ /tmp/source/src
COPY test_package/ /tmp/source/test_package

RUN conan create /tmp/source "${CONAN_USER}"/"${CONAN_CHANNEL}";
RUN conan create -pr debug /tmp/source "${CONAN_USER}"/"${CONAN_CHANNEL}";

ARG CONAN_PASS=${CONAN_USER}
RUN conan user -r origin -p "${CONAN_PASS}" sds;

CMD set -eux; \
    PKG_VERSION=$(grep 'version =' /tmp/source/conanfile.py | awk '{print $3}'); \
    PKG_VERSION="${PKG_VERSION%\"}"; \
    PKG_VERSION="${PKG_VERSION#\"}"; \
    conan upload homestore/${PKG_VERSION}@"${CONAN_USER}"/"${CONAN_CHANNEL}" --all -r origin;
# ##########   #######   ############

