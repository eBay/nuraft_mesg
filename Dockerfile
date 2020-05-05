# ##########   #######   ############
FROM ecr.vip.ebayc3.com/sds/sds_cpp_base:3.9-dev
LABEL description="Automated SDS compilation"

ARG BRANCH_NAME
ARG BUILD_TYPE
ARG COVERAGE_ON
ARG CONAN_CHANNEL
ARG ARTIFACTORY_PASS=${ARTIFACTORY_PASS}
ARG CONAN_USER
ENV BRANCH_NAME=${BRANCH_NAME:-unknown}
ENV BUILD_TYPE=${BUILD_TYPE:-default}
ENV COVERAGE_ON=${COVERAGE_ON:-false}
ENV ARTIFACTORY_PASS=${ARTIFACTORY_PASS:-password}
ENV CONAN_USER=${CONAN_USER:-sds}
ENV CONAN_CHANNEL=${CONAN_CHANNEL:-develop}
ENV SOURCE_PATH=/tmp/source/

COPY .git/ ${SOURCE_PATH}.git
RUN cd ${SOURCE_PATH}; git reset --hard

WORKDIR /output
ENV ASAN_OPTIONS=detect_leaks=0

RUN conan create -pr ${BUILD_TYPE} ${SOURCE_PATH} "${CONAN_USER}"/"${CONAN_CHANNEL}"

CMD set -eux; \
    eval $(grep 'name =' ${SOURCE_PATH}conanfile.py | sed 's, ,,g' | sed 's,name,PKG_NAME,'); \
    eval $(grep 'version =' ${SOURCE_PATH}conanfile.py | sed 's, ,,g' | sed 's,version,PKG_VERSION,'); \
    conan user -r ebay-local -p "${ARTIFACTORY_PASS}" _service_sds; \
    conan upload ${PKG_NAME}/${PKG_VERSION}@"${CONAN_USER}"/"${CONAN_CHANNEL}" -c --all -r ebay-local;
# ##########   #######   ############
