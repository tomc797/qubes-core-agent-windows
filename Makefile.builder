ifeq ($(PACKAGE_SET),vm)
WIN_SOURCE_SUBDIRS= .
WIN_PREBUILD_CMD = set_version.bat && powershell -executionpolicy bypass set_version.ps1
WIN_BUILD_DEPS = windows-utils core-vchan-xen
ifeq ($(BACKEND_VMM),xen)
WIN_BUILD_DEPS += vmm-xen-windows-pvdrivers
endif
endif
