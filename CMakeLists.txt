cmake_minimum_required(VERSION 3.10)

project(FriendSh3ep C)
set(CMAKE_C_STANDARD 11)

# Assembler output for debug; picked up by the network subdirectory via scope.
list(APPEND AMIGA_COMPILER_OPTIONS -save-temps -Wall)
# -mhard-float -ffast-math -msoft-float
list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -O2)
#  -O3 -fno-expensive-optimizations -freorder-blocks-algorithm=simple
# -fno-expensive-optimizations -freorder-blocks-algorithm=simple
# infer a few bugs:
#list(APPEND AMIGA_COMPILER_OPTIONS -mcpu=68020 -O3 -fno-tree-loop-vectorize)


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../EmojiGear/sdk/include)

#needed for OpenURL.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../amigacommonlibs/extrasdk/OpenURL/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../amigacommonlibs/extrasdk/ OpenURL/include)


# Debug log (bdbprintf/bdbprintf_now, see bdbprintf.h) for BOTH the GUI and
# the network library -- the network process can't printf itself, its lines
# go out through the main process's console (pr_COS), so launch from a Shell
# to see them. Configure with -DFS3E_DEBUG_LOG=ON.
option(FS3E_DEBUG_LOG "Enable bdbprintf debug output (GUI + network)" OFF)

# ---- Network process static library --------------------------------------
# Standalone, no GUI includes; runs in its own CreateNewProc() task.
add_subdirectory(network_fs3e)

# ---- GUI sources -----------------------------------------------------------
# BOOPSI window.class + layout.gadget GUI, structured after
# EmojiGear/emojigear.c (see ARCHITECTURE.md). More gadgets/rendering land in
# later phases.

set(FRIENDSH3EP_SOURCES
#	bdbprintf.c
    avatarimages.c
    bmimage.c
    fs3etextwrap.c
    fs3ethumb.c
    rgbscale.c
    rgbimage.c
    scalepixelarraybilinear.c
    fs3eblurhash.c
    clipboard.c
    patch9.c
    stylefile.c
    fs3etbdefaultbtn.c
    friendsh3ep.c
    fs3erequests.c
    fs3eaccounts.c
    fs3enetworkhelper.c
    fs3estyle.c
    fs3eboopsimainwindow.c
    fs3eboopsimessage.c
    fs3eloginview.c
    fs3etootview.c
    fs3eunicodetricks.c
    fs3eemojibox.c
    fs3ethemeview.c
    fs3esettingsview.c
    fs3eprogressview.c
    fs3enetworkview.c
    fs3emediaview.c
    fs3elocale.c
    fs3eoslocale.c
    fs3emenu.c
    fs3eaction.c
    fs3esettings.c
    fs3emanageurl.c
    fs3erequester.c
    fs3emachineid.c
    fs3eaudio.c
    fs3etimer.c
    tooltypepref.c
    UniButtonP9/unibuttonp9.c
    UniButtonP9/unibuttonp9_dispatch.c
    UniButtonP9/unibuttonp9_attribs.c
    UniButtonP9/unibuttonp9_render.c
    UniButtonP9/unibuttonp9_input.c
    UniButtonBGBM/unibuttonbgbm.c
    UniButtonBGBM/unibuttonbgbm_dispatch.c
    UniButtonBGBM/unibuttonbgbm_attribs.c
    UniButtonBGBM/unibuttonbgbm_render.c
    UniButtonBGBM/unibuttonbgbm_input.c
    UniButtonBGBM/offscreenbm.c
    TitleBarLayout/fs3etitlebar.c
    NavBarLayout/fs3enavbar.c
    SearchBarLayout/fs3esearchbar.c
    TootTimeline/fs3etoottimeline.c
    TootTimeline/fs3etoottimeline_attribs.c
    TootTimeline/fs3etoottimeline_posts.c
    TootTimeline/fs3etoottimeline_tiles.c
    TootTimeline/fs3etoottimeline_render.c
    TootTimeline/fs3etoottimeline_input.c
    TootTimeline/fs3etoottimeline_profile.c
    TootTimeline/fs3etoottimeline_notif.c
    TootTimeline/fs3etoottimeline_accountrow.c
    TootTimeline/fs3etoottimeline_instance.c
    TootTimeline/fs3etoottimeline_news.c
)

# Listed so IDEs (Qt Creator, etc.) show them in the project tree.
set(FRIENDSH3EP_HEADERS
    bmimage.h
    fs3etextwrap.h
    clipboard.h
    patch9.h
    stylefile.h
    fs3etbdefaultbtn.h
    friendsh3ep.h
    fs3erequests.h
    fs3eaccounts.h
    fs3enetworkhelper.h
    fs3estyle.h
    fs3eboopsimainwindow.h
    fs3eboopsimessage.h
    fs3eloginview.h
    fs3etootview.h
    fs3eemojibox.h
    fs3ethemeview.h
    fs3esettingsview.h
    fs3eprogressview.h
    fs3enetworkview.h
    fs3emediaview.h
    fs3elocale.h
    fs3emenu.h
    fs3eaction.h
    fs3esettings.h
    fs3emachineid.h
    fs3eaudio.h
    fs3egadgetid.h
    compilers.h
    bdbprintf.h
    UniButtonP9/unibuttonp9.h
    UniButtonBGBM/unibuttonbgbm.h
    TitleBarLayout/fs3etitlebar.h
    NavBarLayout/fs3enavbar.h
    SearchBarLayout/fs3esearchbar.h
    TootTimeline/fs3etoottimeline.h
    TootTimeline/fs3etoottimeline_private.h
)

if(FS3E_DEBUG_LOG)
    list(APPEND FRIENDSH3EP_SOURCES bdbprintf.c)
endif()

add_executable(FriendSh3ep ${FRIENDSH3EP_SOURCES} ${FRIENDSH3EP_HEADERS})

if(FS3E_DEBUG_LOG)
    target_compile_definitions(FriendSh3ep PRIVATE USE_DEBUG_BDBPRINT=1)
endif()

target_link_libraries(FriendSh3ep PRIVATE fs3enet)

target_compile_options(FriendSh3ep PRIVATE
    ${AMIGA_COMPILER_OPTIONS}
)

install(TARGETS FriendSh3ep ARCHIVE DESTINATION bin)


# target_compile_definitions(FriendSh3ep PRIVATE USE_DEBUG_BDBPRINT=1)
