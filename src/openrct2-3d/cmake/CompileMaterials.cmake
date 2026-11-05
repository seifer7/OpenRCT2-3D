if(NOT DEFINED FILAMENT_SDK OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FILAMENT_SDK and OUTPUT_DIR are required")
endif()
if(CMAKE_HOST_WIN32)
    set(tool_suffix ".exe")
endif()
set(matc "${FILAMENT_SDK}/bin/matc${tool_suffix}")
set(resgen "${FILAMENT_SDK}/bin/resgen${tool_suffix}")
get_filename_component(source_dir "${CMAKE_CURRENT_LIST_DIR}/../materials" ABSOLUTE)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(compile_material name source)
    execute_process(COMMAND "${matc}" -p desktop -a all -S ${ARGN}
        -o "${OUTPUT_DIR}/${name}.filamat" "${source_dir}/${source}.mat"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Filament material compilation failed: ${name}")
    endif()
endfunction()

compile_material(mesh_opaque mesh -TBLENDING=opaque)
compile_material(mesh_masked mesh -TBLENDING=masked)
compile_material(mesh_blend mesh -TBLENDING=transparent)
compile_material(sprite sprite)
compile_material(terrain terrain -TBLENDING=masked)
compile_material(water terrain -TBLENDING=transparent)
execute_process(COMMAND "${resgen}" -q -c -p view3d_materials -x "${OUTPUT_DIR}"
    "${OUTPUT_DIR}/mesh_opaque.filamat" "${OUTPUT_DIR}/mesh_masked.filamat"
    "${OUTPUT_DIR}/mesh_blend.filamat" "${OUTPUT_DIR}/sprite.filamat"
    "${OUTPUT_DIR}/terrain.filamat" "${OUTPUT_DIR}/water.filamat"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Filament material embedding failed")
endif()
