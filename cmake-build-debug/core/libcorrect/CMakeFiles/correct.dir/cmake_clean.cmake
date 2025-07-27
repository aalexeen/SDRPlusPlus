file(REMOVE_RECURSE
  "lib/libcorrect.pdb"
  "lib/libcorrect.so"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/correct.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
