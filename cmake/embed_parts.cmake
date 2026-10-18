# Compiles parts/*.txt into a C array so every build (desktop, web, mobile)
# carries the parts library without files on disk.
#   cmake -DOUT=<file.c> -DIN="<a.txt;b.txt>" -P embed_parts.cmake
set(data "")
foreach(f ${IN})
  get_filename_component(name ${f} NAME)
  file(READ ${f} body)
  string(APPEND data "@file ${name}\n${body}\n")
endforeach()
string(LENGTH "${data}" len)
file(WRITE ${OUT}.tmp "${data}")
file(READ ${OUT}.tmp hex HEX)
file(REMOVE ${OUT}.tmp)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" hex "${hex}")
file(WRITE ${OUT} "/* generated from parts/*.txt by cmake/embed_parts.cmake */\n"
  "const unsigned char de_partdata[] = {\n${hex}0x00};\n"
  "const unsigned long de_partdata_size = sizeof(de_partdata) - 1;\n")
