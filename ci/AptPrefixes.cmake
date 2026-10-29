# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED OUTPUT_ROOT OR NOT IS_ABSOLUTE "${OUTPUT_ROOT}")
  message(FATAL_ERROR "Pass an absolute -DOUTPUT_ROOT=<build-input-directory>")
endif()
find_program(DPKG_DEB dpkg-deb REQUIRED)
execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE architecture
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT architecture STREQUAL "amd64")
  message(FATAL_ERROR "Pinned APT prefixes currently support amd64 only")
endif()
file(MAKE_DIRECTORY "${OUTPUT_ROOT}/downloads")
function(extract_apt abi url hash)
  get_filename_component(filename "${url}" NAME)
  set(archive "${OUTPUT_ROOT}/downloads/${filename}")
  file(DOWNLOAD "${url}" "${archive}" EXPECTED_HASH "SHA256=${hash}"
    TLS_VERIFY ON TIMEOUT 180 STATUS status)
  list(GET status 0 result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "APT input download failed: ${status}")
  endif()
  file(MAKE_DIRECTORY "${OUTPUT_ROOT}/apt${abi}")
  execute_process(COMMAND "${DPKG_DEB}" -x "${archive}" "${OUTPUT_ROOT}/apt${abi}"
    COMMAND_ERROR_IS_FATAL ANY)
endfunction()
# Hashes verified against Ubuntu noble and Debian trixie signed APT metadata.
# These are build/test inputs only; no foreign package is installed on the host
# and neither libapt library is bundled inside LuxuryStore's DEB.
extract_apt(6 "https://archive.ubuntu.com/ubuntu/pool/main/a/apt/libapt-pkg-dev_2.7.14build2_amd64.deb"
  bbf1e3881e131b2f1c7885e29ec2d8b2389589b739805adbc999a6a01d867e6a)
extract_apt(6 "https://archive.ubuntu.com/ubuntu/pool/main/a/apt/libapt-pkg6.0t64_2.7.14build2_amd64.deb"
  3915b0cdfbaae551d9089400f47be749b89c30914985982b39e57fd2016be852)
extract_apt(7 "https://deb.debian.org/debian/pool/main/a/apt/libapt-pkg-dev_3.0.3_amd64.deb"
  160c8b31304126b897cc145b95b74d9b660edfa990b805b048c857bf8143c62c)
extract_apt(7 "https://deb.debian.org/debian/pool/main/a/apt/libapt-pkg7.0_3.0.3_amd64.deb"
  fdcc9f780c7de466c7f0038e6e242c73cb63c8f9fe2781a3185a2c810984ab0a)
