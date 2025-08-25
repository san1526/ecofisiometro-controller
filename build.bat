@ECHO OFF
SETLOCAL EnableDelayedExpansion
SETLOCAL

IF NOT DEFINED CMD_BAT ( SET CMD_BAT="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" )
IF NOT DEFINED DEBUG ( SET DEBUG=1 )
IF NOT DEFINED EXE_NAME ( SET EXE_NAME=controller-app )
IF NOT DEFINED RUN_AFTER_BUILD ( SET RUN_AFTER_BUILD=1 )

REM Set the visual studio console/vars
SET __VSCMD_ARG_no_logo=1
SET VSCMD_SKIP_SENDTELEMETRY=1
CALL %CMD_BAT% -arch=x64 -host_arch=x64
REM CALL "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

REM Setup compile_flags header
ECHO -std=c++20 > compile_flags.txt
ECHO -Ithird-party/implot >> compile_flags.txt
ECHO -Ithird-party/imgui >> compile_flags.txt
ECHO -Ithird-party/imgui/backends >> compile_flags.txt
ECHO -Ithird-party/GLFW/include >> compile_flags.txt
ECHO -Ithird-party/sqlite >> compile_flags.txt
ECHO -D_CRT_SECURE_NO_WARNINGS >> compile_flags.txt

SET FILES=^
    third-party/imgui/backends/imgui_impl_glfw.cpp^
    third-party/imgui/backends/imgui_impl_opengl3.cpp^
    third-party/imgui/imgui.cpp^
    third-party/imgui/imgui_widgets.cpp^
    third-party/imgui/imgui_draw.cpp^
    third-party/imgui/imgui_tables.cpp^
    third-party/implot/implot.cpp^
    third-party/implot/implot_items.cpp^
    third-party/sqlite/sqlite3.c^
    app.cpp^
    db.cpp^
    log.cpp^
    ui.cpp^
    main.cpp

SET ASAN=0
SET ANALIZE=0
SET RELEASE_SYMBOLS=0
SET PROFILE=0

SET INCLUDES=^
    /Ithird-party/imgui^
    /Ithird-party/imgui/backends/^
    /Ithird-party/implot^
    /Ithird-party/sqlite^
    /Ithird-party/GLFW/include

SET CFLAGS=^
    /nologo^
    /DSQLITE_OMIT_DEPRECATED^
    /DSQLITE_OMIT_DECLTYPE^
    /D_CRT_SECURE_NO_WARNINGS^
    /Fe:build\%EXE_NAME%^
    /Fo:build\^
    /Fd:build\^
    /MD^
    /MP6^
    /EHsc^
    /W4^
    /std:c++20^
    /utf-8^
    /Zc:tlsGuards-^
    /wd4312

REM /Bt

REM /Wd4312 this disables the 'type cast': conversion from 's32' to 'void *' of greater size warning

REM the tlsGUards- flag is so MSVC does not generate redundant checks for TLS variables
REM as we don't really have DLL issues here. See https://learn.microsoft.com/en-us/cpp/build/reference/zc-tlsguards?view=msvc-170

REM /d2cgsummary

IF %ANALIZE%==1 (
    SET CFLAGS=!CFLAGS!^
    /analyze
)

IF %ASAN%==1 (
    ECHO -D__SANITIZE_ADDRESS__ >> compile_flags.txt
    SET CFLAGS=!CFLAGS!^
        /fsanitize=address
)


IF %DEBUG%==1 (
    ECHO -DDEBUG_BUILD >> compile_flags.txt
    SET CFLAGS=!CFLAGS!^
        /DDEBUG_BUILD=1^
        /Od^
        /ZI

)else (
    SET CFLAGS=!CFLAGS!^
        /GL^
        /O2

    IF %RELEASE_SYMBOLS%==1 (
        SET CFLAGS=!CFLAGS!^
            /Z7
    )
)

IF %DEBUG%==0 (
    IF %PROFILE%==1 (
        ECHO -IE:\Programming\Projects\utils\tracy\public\tracy >> compile_flags.txt
        ECHO -DPROFILING_ENABLED >> compile_flags.txt
        SET FILES=%FILES%^
            E:\Programming\Projects\utils\tracy\public\TracyClient.cpp
        SET CFLAGS=!CFLAGS!^
            /IE:\Programming\Projects\utils\tracy\public\tracy^
            /DPROFILING_ENABLED^
            /DTRACY_ENABLE
    )
)

SET LFLAGS=^
    /link^
    /SUBSYSTEM:WINDOWS^
    /LIBPATH:third-party/GLFW/lib-vc2022

IF %DEBUG%==1 (
    SET LFLAGS=%LFLAGS%^
        /DEBUG
)

SET LIBS=^
    user32.lib^
    gdi32.lib^
    Advapi32.lib^
    opengl32.lib^
    glfw3.lib

REM [Optional] Replace the pretty formating '    ' with ' '.
SET FILES=%FILES:    = %
SET CFLAGS_D=%CFLAGS:    = %
SET LFLAGS_D=%LFLAGS:    = %
SET LIBS=%LIBS:    = %

DEL *.pdb
cl.exe %FILES% %CFLAGS% %INCLUDES% %DEFINES% %LFLAGS% %LIBS% && IF %RUN_AFTER_BUILD%==1 ( build\%EXE_NAME% )
