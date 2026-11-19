# Qt Designer plugin (designer/okapiwidgetplugin.cpp) registering the app's
# promoted custom widgets. Optional: only built when Qt6 Designer (qttools)
# is available; install with:
#   cmake --install build --component designer
# or copy libokapiwidgets.so to ~/.local/share/qt6/plugins/designer/.
find_package(Qt6 6.5 QUIET COMPONENTS Designer)
if(NOT Qt6Designer_FOUND)
    message(STATUS "Qt6 Designer not found: designer plugin not built (optional)")
    return()
endif()

add_library(okapiwidgets MODULE okapiwidgetplugin.cpp okapiwidgetplugin.h)
set_target_properties(okapiwidgets PROPERTIES PREFIX "")
target_link_libraries(okapiwidgets PRIVATE okapi-ui Qt6::Designer)

install(TARGETS okapiwidgets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/plugins/designer
    COMPONENT designer)
