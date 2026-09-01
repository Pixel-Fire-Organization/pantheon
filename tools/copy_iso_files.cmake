# copy_iso_files.cmake
# Copies assets/ into the ISO staging directory, honouring ISO_BLACKLIST.
#
# Expected variables (pass via -D on the cmake command line):
#   SRC       – source directory  (assets/)
#   DST       – destination directory (iso_root/)
#   BLACKLIST – semicolon-separated list of file/directory names to exclude

# Build the PATTERN … EXCLUDE argument list accepted by file(COPY …).
set(_exclude_args)
foreach(_item IN LISTS BLACKLIST)
    list(APPEND _exclude_args PATTERN "${_item}" EXCLUDE)
endforeach()

file(COPY "${SRC}/" DESTINATION "${DST}" ${_exclude_args})

