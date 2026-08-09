set(BEEBEM_ASSETS
        "roms/os12.rom"
        "roms/basic2.rom"
        "roms/dnfs.rom"
        "fonts/teletext.fnt"
        "discs/chuckie_egg.ssd"
        "discs/planetoid.ssd"
        "discs/hopper.ssd"
        "discs/arcadians.ssd"
        "discs/repton.ssd"
        "discs/thrust.ssd"
        "discs/zalaga.ssd"
        "discs/daredevil_dennis.ssd"
        "discs/frak.ssd"
        "discs/repton3.ssd"
        "discs/repton2.ssd"
        "discs/snapper.ssd"
        "discs/killer_gorilla.ssd"
        "discs/mr_ee.ssd"
        "discs/flappy_bird.ssd"
        "discs/painter.ssd"
        "discs/super_breakout.ssd"
        "discs/bbc_tetris.ssd"
        "discs/citadel.ssd"
        "discs/elite.ssd"
        "screenshots/chuckie.rle"
        "screenshots/planetoid.rle"
        "screenshots/hopper.rle"
        "screenshots/arcadians.rle"
        "screenshots/repton.rle"
        "screenshots/thrust.rle"
        "screenshots/zalaga.rle"
        "screenshots/daredevil_dennis.rle"
        "screenshots/frak.rle"
        "screenshots/repton3.rle"
        "screenshots/repton2.rle"
        "screenshots/snapper.rle"
        "screenshots/killer_gorilla.rle"
        "screenshots/mr_ee.rle"
        "screenshots/flappy_bird.rle"
        "screenshots/painter.rle"
        "screenshots/super_breakout.rle"
        "screenshots/bbc_tetris.rle"
        "screenshots/citadel.rle"
        "screenshots/elite.rle")

set(MISSING_ASSETS "")
foreach(ASSET IN LISTS BEEBEM_ASSETS)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${ASSET}")
        list(APPEND MISSING_ASSETS "  components/assets/${ASSET}")
    endif()
endforeach()

if(MISSING_ASSETS)
    list(JOIN MISSING_ASSETS "\n" MISSING_ASSETS_TEXT)
    message(FATAL_ERROR
        "Required user-supplied ROM, disc, or launcher assets are missing:\n"
        "${MISSING_ASSETS_TEXT}\n"
        "See docs/ASSETS.md and run tools/install_assets.py before building.")
endif()

idf_component_register(
    SRCS "assets.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bbc_core
    EMBED_FILES ${BEEBEM_ASSETS})
