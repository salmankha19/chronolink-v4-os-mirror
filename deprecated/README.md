# Deprecated

Files here are kept for reference but are not compiled or linked.
They are not in any CMakeLists.txt SRCS list.

## json_display_executor.{c,h}

Removed from the build in PR 1. It was never compiled:
components/display_manager/CMakeLists.txt lists only src/display_manager.c.
The active UI path is the dm_frame_t queue in display_manager.{c,h}.
The README in display_manager/ used to describe a JSON command interface
that was only ever implemented here, in the uncompiled file. Retained in
case a JSON-driven display path is wanted later.
