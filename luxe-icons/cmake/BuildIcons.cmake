# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)
if(NOT SOURCE_DIR OR NOT BUILD_ROOT OR NOT OUTPUT_DIR)
  message(FATAL_ERROR "SOURCE_DIR, BUILD_ROOT and OUTPUT_DIR are required")
endif()
if(NOT ICON_DEDUPLICATOR OR NOT EXISTS "${ICON_DEDUPLICATOR}")
  message(FATAL_ERROR "ICON_DEDUPLICATOR must name the configured host executable")
endif()

file(REAL_PATH "${SOURCE_DIR}" source_real)
file(REAL_PATH "${BUILD_ROOT}" build_real)
get_filename_component(output_parent "${OUTPUT_DIR}" DIRECTORY)
file(REAL_PATH "${output_parent}" output_parent_real)
get_filename_component(output_name "${OUTPUT_DIR}" NAME)
set(output_real "${output_parent_real}/${output_name}")
set(final_output "${output_real}")
cmake_path(IS_PREFIX output_real "${source_real}" NORMALIZE output_contains_source)
if(NOT IS_DIRECTORY "${build_real}" OR NOT output_parent_real STREQUAL build_real
   OR NOT output_name STREQUAL "theme" OR output_contains_source OR IS_SYMLINK "${OUTPUT_DIR}")
  message(FATAL_ERROR "OUTPUT_DIR must be the build root's non-symlink theme directory, outside source ancestors")
endif()

set(marker "${OUTPUT_DIR}/.luxe-icons-generated")
if(EXISTS "${OUTPUT_DIR}")
  if(NOT IS_DIRECTORY "${OUTPUT_DIR}" OR NOT EXISTS "${marker}" OR IS_SYMLINK "${marker}")
    message(FATAL_ERROR "Refusing to replace an unowned icon output: ${OUTPUT_DIR}")
  endif()
  file(READ "${marker}" marker_text LIMIT 64)
  if(NOT marker_text STREQUAL "luxe-icons-generated-v1\n")
    message(FATAL_ERROR "Invalid icon output ownership marker: ${marker}")
  endif()
endif()

# Verify every input before touching a prior generated tree.
file(READ "${SOURCE_DIR}/sources.lock.json" lock_json)
string(JSON source_count LENGTH "${lock_json}")
math(EXPR source_last "${source_count} - 1")
foreach(index RANGE 0 ${source_last})
  string(JSON archive_name GET "${lock_json}" ${index} archive)
  string(JSON expected_hash GET "${lock_json}" ${index} sha256)
  set(archive "${SOURCE_DIR}/${archive_name}")
  file(SHA256 "${archive}" actual_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(FATAL_ERROR "Checksum mismatch for ${archive_name}")
  endif()
endforeach()

# Generate beside the current output. Any extraction, pruning, or coverage
# failure leaves the last complete icon tree in place.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef stage_nonce)
set(OUTPUT_DIR "${build_real}/.theme-stage-${stage_nonce}")
set(backup_dir "${build_real}/.theme-backup-${stage_nonce}")
set(marker "${OUTPUT_DIR}/.luxe-icons-generated")
if(EXISTS "${OUTPUT_DIR}" OR IS_SYMLINK "${OUTPUT_DIR}" OR EXISTS "${backup_dir}")
  message(FATAL_ERROR "Refusing to reuse an existing icon staging path")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}/sources" "${OUTPUT_DIR}/usr/share/icons")
file(WRITE "${marker}" "luxe-icons-generated-v1\n")
foreach(index RANGE 0 ${source_last})
  string(JSON source_name GET "${lock_json}" ${index} name)
  string(JSON archive_name GET "${lock_json}" ${index} archive)
  set(archive "${SOURCE_DIR}/${archive_name}")
  set(unpack "${OUTPUT_DIR}/sources/${source_name}")
  file(MAKE_DIRECTORY "${unpack}")
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${unpack}")
  file(GLOB children LIST_DIRECTORIES true "${unpack}/*")
  list(GET children 0 source_root)
  set(${source_name}_ROOT "${source_root}")
endforeach()

set(icons "${OUTPUT_DIR}/usr/share/icons")
set(theme "${icons}/Luxe")
set(light "${icons}/Luxe-Light")
set(dark "${icons}/Luxe-Dark")
file(COPY "${Papirus_ROOT}/Papirus" DESTINATION "${icons}")
file(RENAME "${icons}/Papirus" "${theme}")
file(COPY "${Papirus_ROOT}/Papirus-Dark" DESTINATION "${icons}")
file(RENAME "${icons}/Papirus-Dark" "${dark}")
file(MAKE_DIRECTORY "${theme}/scalable/luxe-apps" "${theme}/scalable/extra-apps"
  "${icons}/hicolor/scalable/apps")

file(GLOB_RECURSE papirus_svg_files "${theme}/*.svg")
set(papirus_names)
foreach(svg IN LISTS papirus_svg_files)
  get_filename_component(name "${svg}" NAME_WLE)
  list(APPEND papirus_names "${name}")
endforeach()
list(REMOVE_DUPLICATES papirus_names)

set(numix_added 0)
file(GLOB numix_apps "${Numix-circle_ROOT}/Numix-Circle/48/apps/*.svg")
foreach(svg IN LISTS numix_apps)
  get_filename_component(name "${svg}" NAME_WLE)
  if(NOT name IN_LIST papirus_names)
    file(REAL_PATH "${svg}" real_svg)
    file(COPY_FILE "${real_svg}" "${theme}/scalable/extra-apps/${name}.svg")
    math(EXPR numix_added "${numix_added} + 1")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/aliases.json" aliases_json)
string(JSON alias_count LENGTH "${aliases_json}")
math(EXPR alias_last "${alias_count} - 1")
# The application packages own their hicolor fallback; this package owns
# only the overrides inside Luxe/Luxe-Light (and inherited Luxe-Dark).
file(READ "${SOURCE_DIR}/icon-ownership.json" ownership_json)
string(JSON owner_count LENGTH "${ownership_json}" fallback_owners)
math(EXPR owner_last "${owner_count} - 1")
foreach(index RANGE 0 ${owner_last})
  string(JSON alias MEMBER "${ownership_json}" fallback_owners ${index})
  string(JSON owner GET "${ownership_json}" fallback_owners "${alias}")
  string(JSON target ERROR_VARIABLE missing_alias GET "${aliases_json}" "${alias}")
  if(missing_alias)
    message(FATAL_ERROR "Fallback owner ${owner} names unknown alias ${alias}")
  endif()
endforeach()
foreach(index RANGE 0 ${alias_last})
  string(JSON alias MEMBER "${aliases_json}" ${index})
  string(JSON target GET "${aliases_json}" "${alias}")
  if(target MATCHES "^luxe:([a-z][a-z0-9-]*)$")
    set(source_icon "${SOURCE_DIR}/artwork/${CMAKE_MATCH_1}.svg")
    if(NOT EXISTS "${source_icon}")
      message(FATAL_ERROR "Missing Luxe artwork: ${target}")
    endif()
  else()
    set(source_icon "${theme}/48x48/apps/${target}.svg")
  endif()
  if(NOT EXISTS "${source_icon}")
    file(GLOB_RECURSE alias_candidates "${theme}/*/${target}.svg")
    if(NOT alias_candidates)
      message(FATAL_ERROR "Missing alias source: ${target}")
    endif()
    list(GET alias_candidates 0 source_icon)
  endif()
  file(REAL_PATH "${source_icon}" real_icon)
  file(COPY_FILE "${real_icon}" "${theme}/scalable/luxe-apps/${alias}.svg")
  string(JSON fallback_owner ERROR_VARIABLE no_fallback_owner
    GET "${ownership_json}" fallback_owners "${alias}")
  if(no_fallback_owner OR fallback_owner STREQUAL "luxe-icons")
    file(COPY_FILE "${real_icon}" "${icons}/hicolor/scalable/apps/${alias}.svg")
  endif()
endforeach()

file(GLOB_RECURSE dark_entries LIST_DIRECTORIES false "${dark}/*")
foreach(entry IN LISTS dark_entries)
  if(IS_SYMLINK "${entry}")
    file(READ_SYMLINK "${entry}" link_target)
    string(REPLACE "Papirus/" "Luxe/" link_target "${link_target}")
    file(REMOVE "${entry}")
    file(CREATE_LINK "${link_target}" "${entry}" SYMBOLIC)
  endif()
endforeach()

# Papirus carries separate SVG drawings for many nominal pixel sizes.  That is
# useful upstream, but it repeats thousands of application, place, device and
# MIME icons in a distribution image.  GTK can scale SVG directly, so retain
# one broad 48px vector set, the pixel-tuned small controls/status artwork, and
# every unique name that would otherwise disappear.
function(add_missing_icons theme_root fallback_dir)
  set(selected_patterns ${ARGN})
  foreach(pattern IN LISTS selected_patterns)
    file(GLOB_RECURSE selected LIST_DIRECTORIES false "${theme_root}/${pattern}")
    foreach(icon IN LISTS selected)
      get_filename_component(filename "${icon}" NAME)
      string(MD5 key "${filename}")
      set("seen_${key}" true)
    endforeach()
  endforeach()

  file(GLOB_RECURSE all_icons LIST_DIRECTORIES false "${theme_root}/*.svg")
  file(MAKE_DIRECTORY "${fallback_dir}")
  foreach(icon IN LISTS all_icons)
    get_filename_component(filename "${icon}" NAME)
    string(MD5 key "${filename}")
    if(NOT DEFINED "seen_${key}" AND EXISTS "${icon}")
      file(REAL_PATH "${icon}" real_icon)
      file(COPY_FILE "${real_icon}" "${fallback_dir}/${filename}")
      set("seen_${key}" true)
    endif()
  endforeach()
endfunction()

set(base_selected
  "8x8/emblems/*.svg"
  "16x16/symbolic/*.svg"
  "18x18/actions/*.svg"
  "24x24/actions/*.svg"
  "24x24/animations/*.svg"
  "24x24/panel/*.svg"
  "24x24/status/*.svg"
  "48x48/apps/*.svg"
  "48x48/devices/*.svg"
  "48x48/emblems/*.svg"
  "48x48/emotes/*.svg"
  "48x48/mimetypes/*.svg"
  "48x48/places/*.svg"
  "48x48/status/*.svg"
  "scalable/luxe-apps/*.svg"
  "scalable/extra-apps/*.svg")
add_missing_icons("${theme}" "${theme}/scalable/fallback" ${base_selected})

set(dark_selected
  "16x16/devices/*.svg"
  "16x16/places/*.svg"
  "18x18/actions/*.svg"
  "24x24/actions/*.svg")
add_missing_icons("${dark}" "${dark}/scalable/fallback" ${dark_selected})

function(prune_children parent)
  set(allowed ${ARGN})
  file(GLOB children LIST_DIRECTORIES true "${parent}/*")
  foreach(child IN LISTS children)
    if(IS_DIRECTORY "${child}" OR IS_SYMLINK "${child}")
      get_filename_component(name "${child}" NAME)
      if(NOT name IN_LIST allowed)
        file(REMOVE_RECURSE "${child}")
      endif()
    endif()
  endforeach()
endfunction()

prune_children("${theme}" 8x8 16x16 18x18 24x24 48x48 scalable)
prune_children("${theme}/8x8" emblems)
prune_children("${theme}/16x16" symbolic symbolic-up-to-20)
prune_children("${theme}/18x18" actions)
prune_children("${theme}/24x24" actions animations panel status)
prune_children("${theme}/48x48" apps categories devices emblems emotes mimetypes places status)
prune_children("${theme}/scalable" luxe-apps extra-apps fallback)

prune_children("${dark}" 16x16 18x18 24x24 scalable)
prune_children("${dark}/16x16" devices places)
prune_children("${dark}/18x18" actions)
prune_children("${dark}/24x24" actions)
prune_children("${dark}/scalable" fallback)

# Some retained aliases point across size/context directories that were
# intentionally removed.  Materialize only those now-broken aliases from the
# pinned upstream source; valid same-directory aliases remain symlinks.
function(repair_broken_aliases theme_root upstream_root)
  file(GLOB_RECURSE entries LIST_DIRECTORIES false "${theme_root}/*")
  foreach(entry IN LISTS entries)
    if(IS_SYMLINK "${entry}" AND NOT EXISTS "${entry}")
      file(RELATIVE_PATH relative "${theme_root}" "${entry}")
      set(upstream_entry "${upstream_root}/${relative}")
      if(EXISTS "${upstream_entry}")
        file(REAL_PATH "${upstream_entry}" real_icon)
        file(REMOVE "${entry}")
        file(COPY_FILE "${real_icon}" "${entry}")
      else()
        message(FATAL_ERROR "Cannot repair compact-theme alias: ${relative}")
      endif()
    endif()
  endforeach()
endfunction()
repair_broken_aliases("${theme}" "${Papirus_ROOT}/Papirus")
repair_broken_aliases("${dark}" "${Papirus_ROOT}/Papirus-Dark")

set(base_sections
  "scalable/luxe-apps|Applications|48"
  "scalable/extra-apps|Applications|48"
  "scalable/fallback|Applications|48"
  "8x8/emblems|Emblems|8"
  "16x16/symbolic/actions|Actions|16"
  "16x16/symbolic/apps|Applications|16"
  "16x16/symbolic/categories|Categories|16"
  "16x16/symbolic/devices|Devices|16"
  "16x16/symbolic/emblems|Emblems|16"
  "16x16/symbolic/emotes|Emotes|16"
  "16x16/symbolic/mimetypes|MimeTypes|16"
  "16x16/symbolic/places|Places|16"
  "16x16/symbolic/status|Status|16"
  "16x16/symbolic/up-to-32|Actions|16"
  "18x18/actions|Actions|18"
  "24x24/actions|Actions|24"
  "24x24/animations|Animations|24"
  "24x24/panel|Status|24"
  "24x24/status|Status|24"
  "48x48/apps|Applications|48"
  "48x48/categories|Categories|48"
  "48x48/devices|Devices|48"
  "48x48/emblems|Emblems|48"
  "48x48/emotes|Emotes|48"
  "48x48/mimetypes|MimeTypes|48"
  "48x48/places|Places|48"
  "48x48/status|Status|48")
set(dark_sections
  "scalable/fallback|Actions|24"
  "16x16/devices|Devices|16"
  "16x16/places|Places|16"
  "18x18/actions|Actions|18"
  "24x24/actions|Actions|24")

function(write_compact_index output name comment inherits)
  set(sections ${ARGN})
  set(directories)
  foreach(spec IN LISTS sections)
    string(REPLACE "|" ";" fields "${spec}")
    list(GET fields 0 path)
    list(APPEND directories "${path}")
  endforeach()
  list(JOIN directories "," directory_line)
  set(index "[Icon Theme]\nName=${name}\nComment=${comment}\nInherits=${inherits}\nFollowsColorScheme=true\nDirectories=${directory_line}\n")
  foreach(spec IN LISTS sections)
    string(REPLACE "|" ";" fields "${spec}")
    list(GET fields 0 path)
    list(GET fields 1 context)
    list(GET fields 2 size)
    # Restore per-directory semantics: fixed pixel directories describe one
    # exact size; only scalable/* directories are truly scalable.
    if(path MATCHES "^([0-9]+)x[0-9]+/")
      string(APPEND index "\n[${path}]\nSize=${size}\nContext=${context}\nType=Fixed\nMinSize=${size}\nMaxSize=${size}\n")
    else()
      string(APPEND index "\n[${path}]\nSize=${size}\nContext=${context}\nType=Scalable\nMinSize=8\nMaxSize=512\n")
    endif()
  endforeach()
  file(WRITE "${output}" "${index}")
endfunction()

write_compact_index("${theme}/index.theme" "Luxe"
  "Flat icons for Luxe: compact Papirus with Numix coverage and Luxe aliases"
  "Adwaita,hicolor" ${base_sections})
write_compact_index("${dark}/index.theme" "Luxe Dark"
  "Luxe icons with dark actions and inherited compact vector coverage"
  "Luxe,Adwaita,hicolor" ${dark_sections})

# Keep an explicit, separately selectable light theme.  Luxe remains as the
# compatibility/base name. Every explicit light lookup path remains present;
# byte-identical artwork is shared through relative file links below, not
# recoloured or hidden behind an inheritance-only theme.
file(COPY "${theme}/" DESTINATION "${light}")
write_compact_index("${light}/index.theme" "Luxe Light"
  "Luxe light icons with compact vector coverage"
  "Adwaita,hicolor" ${base_sections})

execute_process(COMMAND "${CMAKE_COMMAND}" "-DICON_ROOT=${icons}"
  "-DICON_DEDUPLICATOR=${ICON_DEDUPLICATOR}"
  -P "${SOURCE_DIR}/cmake/Deduplicate.cmake" COMMAND_ERROR_IS_FATAL ANY)

find_program(GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)
if(GTK_UPDATE_ICON_CACHE)
  execute_process(COMMAND "${GTK_UPDATE_ICON_CACHE}" -f -t "${theme}" COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${GTK_UPDATE_ICON_CACHE}" -f -t "${light}" COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${GTK_UPDATE_ICON_CACHE}" -f -t "${dark}" COMMAND_ERROR_IS_FATAL ANY)
  # The maintainer scripts generate host caches after install; do not ship
  # build-host caches in the package or live image.
  file(REMOVE "${theme}/icon-theme.cache" "${light}/icon-theme.cache" "${dark}/icon-theme.cache")
endif()
list(LENGTH papirus_names papirus_count)

# Coverage-count assertion: the final tree must provide every unique icon
# name the suite expects. A silent drop (e.g. an upstream archive change that
# removes names) fails the build instead of shipping a smaller theme.
# NOTE: enumerated with find (not file(GLOB_RECURSE), which also descends
# into symlinked directories and double-counts entries).
file(READ "${SOURCE_DIR}/coverage.json" coverage_json)
string(JSON expected_names GET "${coverage_json}" unique_names)
string(JSON expected_svg GET "${coverage_json}" svg_entries)
find_program(FIND_EXECUTABLE find)
if(NOT FIND_EXECUTABLE)
  message(FATAL_ERROR "find is required for the icon coverage assertion")
endif()
execute_process(
  # Luxe-Light deliberately duplicates the complete light tree.  Preserve the
  # long-standing coverage baseline for the base, dark and hicolor themes;
  # the copy above is deterministic and independently named.
  COMMAND "${FIND_EXECUTABLE}" "${icons}" "(" -path "${light}" -o -path "${light}/*" ")" -prune -o "(" -type f -o -type l ")" -print
  OUTPUT_VARIABLE built_list
  COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" built_entries "${built_list}")
list(REMOVE_ITEM built_entries "")
set(built_names)
set(built_svg 0)
foreach(entry IN LISTS built_entries)
  # Strip only the last extension (icon name semantics): CMake NAME_WE strips
  # the longest suffix, which would merge e.g. "app.0.svg" into "app".
  get_filename_component(entry_file "${entry}" NAME)
  string(REGEX REPLACE "\\.[^.]*$" "" entry_name "${entry_file}")
  list(APPEND built_names "${entry_name}")
  if(entry MATCHES "\\.svg$")
    math(EXPR built_svg "${built_svg} + 1")
  endif()
endforeach()
list(REMOVE_DUPLICATES built_names)
list(LENGTH built_names built_name_count)
if(NOT built_name_count EQUAL expected_names)
  message(FATAL_ERROR "Icon coverage drift: ${built_name_count} unique names, expected ${expected_names} (see coverage.json)")
endif()
if(NOT built_svg EQUAL expected_svg)
  message(FATAL_ERROR "Icon coverage drift: ${built_svg} SVG entries, expected ${expected_svg} (see coverage.json)")
endif()

file(WRITE "${OUTPUT_DIR}/.built" "Papirus names: ${papirus_count}\nNumix additions: ${numix_added}\nLuxe aliases: ${alias_count}\nLayout: compact scalable coverage\n")

# Reproducible mtimes: when SOURCE_DATE_EPOCH is set, normalize every
# generated entry to that epoch so rebuilds are bit-identical.
if(DEFINED ENV{SOURCE_DATE_EPOCH} AND NOT "$ENV{SOURCE_DATE_EPOCH}" STREQUAL "")
  find_program(TOUCH_EXECUTABLE touch)
  find_program(FIND_EXECUTABLE find)
  if(TOUCH_EXECUTABLE AND FIND_EXECUTABLE)
    execute_process(
      COMMAND "${FIND_EXECUTABLE}" "${icons}" -exec "${TOUCH_EXECUTABLE}" -h -d "@$ENV{SOURCE_DATE_EPOCH}" {} +
      COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
      COMMAND "${TOUCH_EXECUTABLE}" -d "@$ENV{SOURCE_DATE_EPOCH}" "${OUTPUT_DIR}/.built"
      COMMAND_ERROR_IS_FATAL ANY)
  endif()
endif()

# Keep the rename sequence recoverable if publication fails. The staged tree
# and final output share a filesystem, so each rename is atomic.
if(EXISTS "${final_output}")
  file(RENAME "${final_output}" "${backup_dir}" RESULT move_result)
  if(NOT move_result STREQUAL "0")
    message(FATAL_ERROR "Could not preserve previous icon output: ${move_result}")
  endif()
endif()
file(RENAME "${OUTPUT_DIR}" "${final_output}" RESULT publish_result)
if(NOT publish_result STREQUAL "0")
  if(EXISTS "${backup_dir}")
    file(RENAME "${backup_dir}" "${final_output}" RESULT restore_result)
    if(NOT restore_result STREQUAL "0")
      message(FATAL_ERROR "Icon publication failed (${publish_result}); previous output restore failed (${restore_result})")
    endif()
  endif()
  message(FATAL_ERROR "Could not publish generated icon output: ${publish_result}")
endif()
file(REMOVE_RECURSE "${backup_dir}")
