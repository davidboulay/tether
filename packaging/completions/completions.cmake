# Shell completions for the tether CLI, generated at build time from the CLI's
# own option and verb tables ('tether --print-completions <shell>'), so they
# cannot drift from what the binary accepts. Skipped when cross-compiling,
# since the freshly built binary cannot run on the build host.
if(NOT CMAKE_CROSSCOMPILING)
    set(TETHER_COMPLETIONS_DIR "${CMAKE_CURRENT_BINARY_DIR}/completions")
    set(TETHER_COMPLETION_FILES
        "${TETHER_COMPLETIONS_DIR}/tether.bash"
        "${TETHER_COMPLETIONS_DIR}/_tether"
        "${TETHER_COMPLETIONS_DIR}/tether.fish")
    add_custom_command(
        OUTPUT ${TETHER_COMPLETION_FILES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TETHER_COMPLETIONS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C LANGUAGE= $<TARGET_FILE:tether_cli> --print-completions bash > "${TETHER_COMPLETIONS_DIR}/tether.bash"
        COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C LANGUAGE= $<TARGET_FILE:tether_cli> --print-completions zsh > "${TETHER_COMPLETIONS_DIR}/_tether"
        COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C LANGUAGE= $<TARGET_FILE:tether_cli> --print-completions fish > "${TETHER_COMPLETIONS_DIR}/tether.fish"
        DEPENDS tether_cli
        COMMENT "Generating shell completions for tether"
        VERBATIM)
    add_custom_target(tether_completions ALL DEPENDS ${TETHER_COMPLETION_FILES})

    install(FILES "${TETHER_COMPLETIONS_DIR}/tether.bash"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
            RENAME tether)
    install(FILES "${TETHER_COMPLETIONS_DIR}/_tether"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions)
    install(FILES "${TETHER_COMPLETIONS_DIR}/tether.fish"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d)
endif()
