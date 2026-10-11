# CMake's MSVC rules invoke Windows rc.exe/mt.exe to embed manifests.
# LLD embeds them itself, so invoke the linker directly on the Linux host.
foreach(language IN ITEMS C CXX)
    if(CMAKE_AR STREQUAL CMAKE_LINKER
       AND DEFINED CMAKE_${language}_CREATE_STATIC_LIBRARY
       AND NOT CMAKE_${language}_CREATE_STATIC_LIBRARY MATCHES "<CMAKE_AR> /lib ")
        string(REPLACE "<CMAKE_AR> " "<CMAKE_AR> /lib "
            CMAKE_${language}_CREATE_STATIC_LIBRARY "${CMAKE_${language}_CREATE_STATIC_LIBRARY}")
    endif()
    foreach(rule IN ITEMS LINK_EXECUTABLE CREATE_SHARED_LIBRARY CREATE_SHARED_MODULE)
        if(DEFINED CMAKE_${language}_${rule})
            string(REGEX REPLACE "^.*<CMAKE_LINKER>" "<CMAKE_LINKER>"
                CMAKE_${language}_${rule} "${CMAKE_${language}_${rule}}")
        endif()
    endforeach()
endforeach()
