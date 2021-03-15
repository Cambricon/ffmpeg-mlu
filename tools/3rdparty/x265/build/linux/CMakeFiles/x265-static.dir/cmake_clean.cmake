file(REMOVE_RECURSE
  "libx265.pdb"
  "libx265.a"
)

# Per-language clean rules from dependency scanning.
foreach(lang ASM_NASM CXX)
  include(CMakeFiles/x265-static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
