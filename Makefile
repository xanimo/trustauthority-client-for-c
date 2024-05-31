SHELL := /bin/bash
ORGNAME := taas
APPNAME := trustauthority-client
REPO := localhost:5000
DCAP_VERSION := 1.19.100.3-jammy1
PSW_VERSION := 2.22.100.3
USE_AZURE_TDX_ADAPTER := OFF
TDX_TOKEN_BUILD_PREFIX := intel

# By default set the build in release mode
ENABLE_DEBUG ?= Release
ifeq ($(DEBUG),1)
    ENABLE_DEBUG = Debug
endif

COMMIT := $(shell git rev-parse --short HEAD)
VERSION := v1.0.0
BUILDDATE := $(shell TZ=UTC date +%Y-%m-%dT%H:%M:%S%z)
PROXY_EXISTS := $(shell if [[ "${https_proxy}" || "${http_proxy}" || "${no_proxy}" ]]; then echo 1; else echo 0; fi)
DOCKER_PROXY_FLAGS := ""
ifeq ($(PROXY_EXISTS),1)
    DOCKER_PROXY_FLAGS = --build-arg http_proxy="${http_proxy}" --build-arg https_proxy="${https_proxy}" --build-arg no_proxy="${no_proxy}"
	DOCKER_RUN_PROXY_FLAGS = -e HTTP_PROXY="${http_proxy}" -e HTTPS_PROXY="${https_proxy}" -e NO_PROXY="${no_proxy}"
else
    DOCKER_PROXY_FLAGS =
	DOCKER_RUN_PROXY_FLAGS = 
endif

MAKEFILE_PATH := $(realpath $(lastword $(MAKEFILE_LIST)))
MAKEFILE_DIR := $(dir $(MAKEFILE_PATH))

.PHONY: all clean
.DEFAULT: ubuntu_22

all: ubuntu_22 sgx_token_docker tdx_token_docker azure_tdx_token_docker

ubuntu_22: 
	docker buildx build \
		--build-arg ENABLE_DEBUG=${ENABLE_DEBUG} \
		${DOCKER_PROXY_FLAGS} \
		-f docker/Dockerfile.ubuntu_22 \
		--output ${MAKEFILE_DIR}bin/ubuntu_22 \
		--target  export-stage \
		-t $(ORGNAME)/$(APPNAME)-ubuntu_22:$(VERSION) \
		--build-arg DCAP_VERSION=${DCAP_VERSION} \
		--build-arg PSW_VERSION=${PSW_VERSION} \
		--build-arg VERSION=${VERSION} \
		--build-arg COMMIT=${COMMIT} . --load

test-image:
	docker buildx build \
		-t $(ORGNAME)/$(APPNAME)-ubuntu_22-unit-test:$(VERSION) \
		${DOCKER_PROXY_FLAGS} \
		-f docker/Dockerfile.ubuntu_22 \
		--build-arg VERSION=${VERSION} \
		--build-arg COMMIT=${COMMIT} \
		--target test-image \
		. --load

test-coverage: test-image
	docker run -i ${DOCKER_RUN_PROXY_FLAGS} --rm $(ORGNAME)/$(APPNAME)-ubuntu_22-unit-test:$(VERSION) \
		/bin/bash -c "lcov --list /tmp/filtered_coverage.info"
	docker rmi $(ORGNAME)/$(APPNAME)-ubuntu_22-unit-test:$(VERSION) || true

sgx_token_docker:
	docker buildx build \
		--build-arg ENABLE_DEBUG=${ENABLE_DEBUG} \
		${DOCKER_PROXY_FLAGS} \
		-f examples/sgx_token/Dockerfile \
		-t $(ORGNAME)/sgx_token:$(VERSION) \
		--build-arg DCAP_VERSION=${DCAP_VERSION} \
		--build-arg PSW_VERSION=${PSW_VERSION} \
		--build-arg MAKEFILE_DIR=${MAKEFILE_DIR} \
		--build-arg VERSION=${VERSION} \
		--build-arg COMMIT=${COMMIT} . --load

tdx_token_docker:
	docker buildx build \
		--build-arg ENABLE_DEBUG=${ENABLE_DEBUG} \
		${DOCKER_PROXY_FLAGS} \
		-f examples/tdx_token/Dockerfile \
		--target ${TDX_TOKEN_BUILD_PREFIX}_tdx_token \
		-t $(ORGNAME)/${TDX_TOKEN_BUILD_PREFIX}_tdx_token:$(VERSION) \
		--build-arg USE_AZURE_TDX_ADAPTER=${USE_AZURE_TDX_ADAPTER} \
		--build-arg DCAP_VERSION=${DCAP_VERSION} \
		--build-arg MAKEFILE_DIR=${MAKEFILE_DIR} \
		--build-arg VERSION=${VERSION} \
		--build-arg COMMIT=${COMMIT} . --load

azure_tdx_token_docker: USE_AZURE_TDX_ADAPTER = ON
azure_tdx_token_docker: TDX_TOKEN_BUILD_PREFIX = azure
azure_tdx_token_docker: tdx_token_docker

clean:
	rm -rf ${MAKEFILE_DIR}bin
