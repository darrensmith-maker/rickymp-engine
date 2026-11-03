if (NOT DEFINED OPENMW_MYGUI_FILES_ROOT)
    return()
endif()

# Copy the fixed core textures into the build directory.
set(DDIRRELATIVE resources/vfs/textures)
set(TEXTURE_FILES
    textures/crosshair.dds
    textures/ownership.dds
    textures/sneak.dds
    textures/omw_menu_scroll_down.dds
    textures/omw_menu_scroll_up.dds
    textures/omw_menu_scroll_left.dds
    textures/omw_menu_scroll_right.dds
    textures/omw_menu_scroll_center_h.dds
    textures/omw_menu_scroll_center_v.dds
    textures/menu_arenamw.png
)
copy_all_resource_files(${CMAKE_CURRENT_SOURCE_DIR} ${OPENMW_MYGUI_FILES_ROOT} ${DDIRRELATIVE} "${TEXTURE_FILES}")

# Built-in animation, prop and interaction UI resources. The engine mounts
# resources/vfs as its first data directory, so these files are available
# without enabling a separate OpenMW content package.
file(GLOB_RECURSE BUILTIN_VFS_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    animations/*.kf
    animations/*.nif
    animations/*.yaml
    animations/*.txt
    meshes/*.nif
    sound/*.wav
    sound/*.mp3
    textures/vfx/*.dds
    textures/vfx/*.tga
    textures/exploot/food/*.tga
    textures/ui/arenamw/*.png
    l10n/arenamp/*.ini
    icons/inventoryextender/*.dds
)

# Consuming Animated pipe/smoke models reference these texture names directly.
# Keep them at the VFS texture root to preserve the paths embedded in the NIFs.
list(APPEND BUILTIN_VFS_FILES
    "textures/cigar logo.dds"
    "textures/cigar wrap.dds"
    "textures/metal_30w.dds"
    "textures/pc_misc_pipe_cob.dds"
)

# Cached incremental builds may contain an older reduced set. Clear only the
# generated directories owned by this source tree before copying the full set.
file(REMOVE_RECURSE "${OPENMW_MYGUI_FILES_ROOT}/resources/vfs/animations")
file(REMOVE_RECURSE "${OPENMW_MYGUI_FILES_ROOT}/resources/vfs/meshes/InteractionsAnimated")
foreach (f ${BUILTIN_VFS_FILES})
    copy_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${OPENMW_MYGUI_FILES_ROOT}" "resources/vfs/${f}")
endforeach()
