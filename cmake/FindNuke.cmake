cmake_minimum_required(VERSION 3.10)

set(_NUKE_SEARCH_DIRS
    "/Applications"
    "C:/Program Files"
    "/usr/local"
)

set(NUKE_FOUND FALSE)

foreach(_dir IN LISTS _NUKE_SEARCH_DIRS)
    if(EXISTS "${_dir}")
        file(GLOB _possible_dirs RELATIVE "${_dir}" "${_dir}/Nuke*")
        foreach(_subdir IN LISTS _possible_dirs)
            if(_subdir MATCHES "^Nuke[0-9.]+v[0-9]+$")
                set(_full_path "${_dir}/${_subdir}")

                string(REGEX MATCH "Nuke[0-9.]+v[0-9]+" _version "${_subdir}")

                if(WIN32)
                    file(GLOB _executables "${_full_path}/Nuke*.exe")
                    if(_executables)
                        list(GET _executables 0 _nuke_exec)
                    endif()
                elseif(APPLE)
                    set(_nuke_exec "${_full_path}/${_version}.app")
                else()
                    set(_nuke_exec "${_full_path}/${_version}")
                endif()

                if(DEFINED _nuke_exec AND EXISTS "${_nuke_exec}")
                    set(NUKE_FOUND TRUE)
                    set(NUKE_EXECUTABLE "${_nuke_exec}")
                endif()
            endif()
        endforeach()
    endif()
endforeach()

if(NUKE_FOUND)
    message(STATUS "Found Nuke: ${NUKE_EXECUTABLE}")
else()
    message(STATUS "Nuke was not found at expected path: ${_nuke_exec}!")
endif()